#!/usr/bin/env python3
"""Offline risk analysis of THE DEVICE'S OWN kernel modules, for init v5/v6.

QUESTION: which finit_module(2) calls made by our init can enter a driver probe
that never returns on this hardware?

WHY THIS IS THE RIGHT QUESTION
  finit_module(2) is not "copy bytes and return": it calls do_init_module(),
  which runs the module's init function *in the caller's context* (= PID 1 for
  us).  For a module that is a platform driver, init = __platform_driver_register
  -> driver_register -> really_probe -> drv->probe(), still in the caller's
  context, unless the driver opts into asynchronous probing.  So any sleep,
  any wait_for_completion(), any rendezvous with a remote processor that is not
  booted, runs with PID 1 as the current task and PID 1 has no other task to
  report anything with.

WHAT THIS SCRIPT MEASURES (on the real .ko files, from the vendor ramdisk):
  * the exact load order v6's init would use (modules.dep + modules.softdep +
    modules.load + modules.load.recovery + the GOALS list, expanded with the
    same depth-first algorithm as candidate-init-v6.c),
  * for every module in that order: whether init_module registers a platform
    driver (=> probe runs inside our syscall),
  * the set of blocking primitives reachable from init_module / *_probe in the
    intra-module call graph (objdump -d -r, relocations included).

Output: module-risk.json + a summary on stdout.  Nothing here is a guess about
hardware behaviour: it is the device's own bytes plus the kernel's call path.
"""
import json
import re
import subprocess
import sys
from collections import defaultdict, deque
from pathlib import Path

MODDIR = Path('/home/user/nx679j-stock/port-work/stock_vendor_ramdisk/lib/modules')
OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v8')
# the HOST objdump cannot read aarch64: use the cross tool (same toolchain as
# the init builds).  Verified: this is what makes the sweep produce functions.
OBJDUMP = 'aarch64-linux-gnu-objdump'

# ---------------------------------------------------------------- symbol sets
# Unbounded blocking: the task cannot make progress by itself, ever.
UNBOUNDED = {
    'wait_for_completion', 'wait_for_completion_interruptible',
    'wait_for_completion_killable', 'wait_for_completion_io',
    'wait_for_completion_killable_timeout',  # killable: needs a signal sender
    'down', 'down_read', 'down_write', 'down_interruptible',
    'mutex_lock', 'mutex_lock_interruptible', 'mutex_lock_killable',
    'flush_workqueue', 'flush_work', 'cancel_work_sync', 'destroy_workqueue',
    'synchronize_rcu', 'synchronize_net', 'synchronize_irq',
    'request_firmware', 'firmware_request_nowarn',
    '__request_module', 'request_module',
    '__mutex_lock_slowpath', '__down', 'rt_mutex_lock',
}
# Bounded but potentially very long: a serial console that nobody drains, a
# hardware timeout that only fires when the hardware answers.
LONG_BOUNDED = {
    'wait_for_completion_timeout', 'wait_for_completion_interruptible_timeout',
    'msleep', 'ssleep', 'msleep_interruptible', 'usleep_range', 'mdelay',
    'readl_poll_timeout', 'readl_poll_timeout_atomic',
    'regmap_read_poll_timeout',
}
# Rendezvous with ANOTHER processor / another subsystem's state machine.  On a
# recovery-style boot those peers (ADSP, CDSP, MPSS, the PMIC firmware) are not
# running, and the handshake completion never comes.
PEER_PREFIXES = (
    'qcom_glink', 'glink_', 'qmi_', 'rpmsg_', 'rproc_', 'q6v5', 'pil_',
    'subsys_', 'pd_', 'spcom_', 'spss_', 'slim_', 'apr_', 'gpr_', 'qdsp',
    'mhi_', 'ipc_router', 'scm_', 'qcom_scm_', 'rpmh_', 'icc_', 'smp2p_',
    'qcom_smd', 'smd_', 'pdr_', 'pmic_glink', 'ucsi_', 'usb_role_',
    'usb_gadget_', 'extcon_',
)
PROBE_RE = re.compile(r'(?:^|_)(probe|pdr_cb|callback|notify)')
SKIP_SUFFIX = ('.cfi_jt', '.cold', '.isra', '.constprop')

FUNC_RE = re.compile(r'^([0-9a-f]+) <([^>]+)>:')
INSN_RE = re.compile(r'^\s*[0-9a-f]+:\s+[0-9a-f ]+\t(\S+)\s*(.*)$')
RELOC_RE = re.compile(r'^\s*[0-9a-f]+:\s+R_AARCH64_(\S+)\s+(\S+)')


def base_sym(s):
    """dwc3_msm_probe$ce72... -> dwc3_msm_probe ; foo.cfi_jt -> foo"""
    s = s.split('$')[0]
    for suf in SKIP_SUFFIX:
        if s.endswith(suf):
            s = s[: -len(suf)]
    return s


def canon(name):
    """Canonical module name: no path, no .ko, '-' -> '_', lower case."""
    b = name.rsplit('/', 1)[-1]
    if b.endswith('.ko'):
        b = b[:-3]
    return b.replace('-', '_').replace('.', '_').lower()


# ------------------------------------------------------- v6 order computation
def v6_order():
    """Reproduce candidate-init-v6.c's DFS emit() over the device metadata."""
    dep_txt = (MODDIR / 'modules.dep').read_text(errors='replace')
    soft_txt = (MODDIR / 'modules.softdep').read_text(errors='replace')
    want_txt = (MODDIR / 'modules.load').read_text(errors='replace').splitlines()
    rec_txt = (MODDIR / 'modules.load.recovery').read_text(errors='replace').splitlines()
    goals = ['qcom_hwspinlock.ko', 'smem.ko', 'qcom_ipc_logging.ko', 'minidump.ko',
             'qcom_glink.ko', 'qcom_glink_smem.ko', 'qcom_smd.ko',
             'rproc_qcom_common.ko', 'qmi_helpers.ko', 'pdr_interface.ko',
             'pmic_glink.ko', 'ucsi_glink.ko', 'debug-regulator.ko',
             'proxy-consumer.ko', 'gdsc-regulator.ko', 'clk-qcom.ko',
             'ssusb-redriver-nb7vpq904m.ko', 'phy-generic.ko', 'eud.ko',
             'phy-msm-snps-hs.ko', 'phy-msm-ssusb-qmp.ko', 'dwc3-msm.ko',
             'phy-qcom-ufs.ko', 'phy-qcom-ufs-qmp-v4-waipio.ko', 'ufs-qcom.ko',
             'ufshcd-crypto-qti.ko', 'crypto-qti-common.ko', 'crypto-qti-hwkm.ko']

    deps = defaultdict(list)
    names = set()
    for line in dep_txt.splitlines():
        if ':' not in line:
            continue
        mod, rest = line.split(':', 1)
        m = canon(mod.strip())
        names.add(m)
        for d in rest.split():
            deps[m].append(canon(d))
            names.add(canon(d))
    pre = defaultdict(list)
    post = defaultdict(list)
    for line in soft_txt.splitlines():
        t = line.strip()
        if not t.startswith('softdep'):
            continue
        toks = t.split()
        if len(toks) < 3:
            continue
        mod = canon(toks[1])
        names.add(mod)
        mode = None
        for tk in toks[2:]:
            if tk == 'pre:':
                mode = pre
            elif tk == 'post:':
                mode = post
            elif mode is not None:
                mode[mod].append(canon(tk))
        names.add(mod)
        for tk in toks[2:]:
            if tk not in ('pre:', 'post:'):
                names.add(canon(tk))

    order = []
    state = {}

    def emit(m, depth=0):
        if depth > 12 or state.get(m) == 2:
            return
        if state.get(m) == 1:
            return
        state[m] = 1
        for d in deps.get(m, []):
            emit(d, depth + 1)
        for d in pre.get(m, []):
            emit(d, depth + 1)
        state[m] = 2
        order.append(m)
        for d in post.get(m, []):
            emit(d, depth + 1)

    want = []
    for src in (want_txt, rec_txt, goals):
        for line in src:
            t = line.strip()
            if not t or t.startswith('#'):
                continue
            want.append(canon(t))
    for m in want:
        emit(m)
    return order, names, want, dict(pre=pre, post=post, deps=deps)


# ------------------------------------------------------------ per-module scan
def analyse(mod):
    ko = MODDIR / f'{mod}.ko'
    if not ko.exists():                     # mixed spelling?
        cands = [p for p in MODDIR.glob('*.ko') if canon(p.name) == mod]
        if not cands:
            return None
        ko = cands[0]
    p = subprocess.run([OBJDUMP, '-dr', str(ko)],
                       capture_output=True, text=True, errors='replace')
    if p.returncode != 0 or not p.stdout.strip():
        raise RuntimeError(f'{OBJDUMP} failed on {ko}: {p.stderr.strip()[:200]}')
    cur = None
    callees = defaultdict(set)     # function -> set of called names
    refs = defaultdict(set)        # function -> set of referenced ext symbols
    funcs = set()
    for line in p.stdout.splitlines():
        m = FUNC_RE.match(line)
        if m:
            cur = base_sym(m.group(2))
            funcs.add(cur)
            continue
        if cur is None:
            continue
        r = RELOC_RE.match(line)
        if r:
            refs[cur].add(r.group(2))
            callees[cur].add(r.group(2))
            continue
        i = INSN_RE.match(line)
        if i:
            mn, ops = i.group(1), i.group(2)
            if mn in ('bl', 'b', 'b.ne', 'b.eq', 'cbz', 'cbnz', 'tbz', 'tbnz'):
                t = re.search(r'<([^>+]+)', ops)
                if t:
                    callees[cur].add(base_sym(t.group(1)))
    # reachability from the load path
    roots = {f for f in funcs if f == 'init_module' or PROBE_RE.search(f)
             or f.endswith('_init')}
    roots &= funcs
    seen, q = set(roots), deque(roots)
    while q:
        f = q.popleft()
        for g in callees.get(f, ()):
            if g not in seen:
                seen.add(g)
                q.append(g)
    def hit(s):
        return sorted({r for f in seen for r in refs.get(f, ())
                       if r == s or (s.endswith('*') and r.startswith(s[:-1]))})
    reached = sorted({r for f in seen for r in refs.get(f, ())
                      if r in UNBOUNDED or r in LONG_BOUNDED
                      or r.startswith(PEER_PREFIXES)})
    whicher = {}
    for sym in reached[:16]:
        whicher[sym] = sorted({f for f in seen if sym in refs.get(f, ())})[:4]
    up_init = sorted(refs.get('init_module', ()))
    return dict(
        ko=str(ko), bytes=ko.stat().st_size,
        n_funcs=len(funcs),
        n_reached=len(seen),
        registers_platform_driver=any('platform_driver_register' in s for s in up_init),
        registers_driver=any('driver_register' in s for s in up_init),
        probe_funcs=sorted(f for f in funcs if PROBE_RE.search(f))[:8],
        unbounded_reachable=[r for r in reached if r in UNBOUNDED],
        long_bounded_reachable=[r for r in reached if r in LONG_BOUNDED],
        peer_reachable=[r for r in reached if r.startswith(PEER_PREFIXES)],
        which_funcs=whicher,
    )


def main():
    order, names, want, meta = v6_order()
    print(f'modules.dep/dep+softdep names: {len(names)}; v6 emit() order: {len(order)}')
    print('first 25 of the v6 order:', ' '.join(order[:25]))
    idx = {m: i for i, m in enumerate(order)}
    for key in ('dwc3_msm', 'ufs_qcom', 'qcom_glink', 'pmic_glink', 'qmi_helpers',
                'pdr_interface', 'ucsi_glink', 'eud', 'msm_kgsl', 'adsp_loader_dlkm',
                'qcom_hwspinlock', 'smem', 'phy_msm_ssusb_qmp', 'altmode_glink'):
        print(f'  position of {key}: {idx.get(key)}')
    res = {}
    only = sys.argv[1:] or None
    todo = [m for m in order if (not only or m in only)]
    for i, m in enumerate(todo):
        try:
            r = analyse(m)
        except Exception as e:                      # never abort the sweep
            r = dict(error=f'{type(e).__name__}: {e}')
        if r:
            r['order_index'] = idx.get(m)
            res[m] = r
        if i % 25 == 0:
            print(f'  .. {i}/{len(todo)}', flush=True)
    risky = {m: r for m, r in res.items()
             if r.get('unbounded_reachable')}
    peer = {m: r for m, r in res.items()
            if r.get('peer_reachable') and r.get('probe_funcs')}
    (OUT / 'module-risk.json').write_text(json.dumps(
        dict(order=order, results=res), indent=2) + '\n')

    print(f'\n=== modules in the v6 order: {len(res)}')
    print(f'=== with an UNBOUNDED wait reachable from init/probe: {len(risky)}')
    for m, r in sorted(risky.items(), key=lambda kv: kv[1]['order_index']):
        print(f'  [pos {r["order_index"]:3d}] {m:32s} platform_driver={int(r["registers_platform_driver"])} '
              f'unbounded={r["unbounded_reachable"]}')
    print(f'\n=== with a peer-rendezvous call reachable from a probe: {len(peer)}')
    for m, r in sorted(peer.items(), key=lambda kv: kv[1]['order_index']):
        print(f'  [pos {r["order_index"]:3d}] {m:32s} peer={r["peer_reachable"][:5]}')
    print(f'\nwritten {OUT / "module-risk.json"}')


if __name__ == '__main__':
    main()
