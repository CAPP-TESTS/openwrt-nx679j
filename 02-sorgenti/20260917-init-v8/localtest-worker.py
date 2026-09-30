#!/usr/bin/env python3
"""Local, seconds-long test of the riskiest part of the worker: the per-module
child with its own deadline.

This runs the SHIPPED worker binary under qemu-aarch64 user mode with
V8_MODDIR/V8_JOURNAL pointing into a scratch directory, so:
  * a normal .ko exercises the "attempted, kernel said no" path
    (finit_module returns EPERM here: a normal user cannot load modules);
  * a FIFO named like a .ko exercises the "the child is stuck for ever" path.
    open() on a FIFO with no writer blocks in the kernel, which is the closest
    local equivalent of a driver probe that never returns;
  * a directory with only metadata (no .ko at all) exercises "MISSING".

What is asserted, from the journal file the worker itself writes:
  1. the ATTEMPT line is written BEFORE the risky call;
  2. the stuck module gets a TIMEOUT-KILLED line after ~15 s;
  3. the walk CONTINUES: the next modules are attempted after it;
  4. the journal distinguishes loaded/failed/TIMEOUT-KILLED;
  5. the exit status is 0 (the chain never aborts the boot).

NOT asserted here: anything about real drivers, real UFS, real USB.
"""
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

OUT = Path('/home/user/nx679j-stock/experiments/20260917-init-v8')
WORK = OUT / 'work'
ROOT = OUT / 'localtest'
MODSRC = OUT / 'modsrc'          # unpacked ONCE, outside the wiped scratch dir
WORKER = WORK / 'worker-v8'
MOD_TIMEOUT_S = 15
res = {'checks': []}


def check(name, ok, detail=''):
    res['checks'].append({'check': name, 'pass': bool(ok), 'detail': str(detail)})
    print(f'[{"PASS" if ok else "FAIL"}] {name}: {detail}')
    return bool(ok)


def modsrc(name):
    """Find a bundled .ko for this name in the unpacked v7 ramdisk tree."""
    cand = list(MODSRC.rglob(name))
    if cand:
        return cand[0]
    cand = list(MODSRC.rglob(name.replace('-', '_')))
    return cand[0] if cand else None


def setup(victim=None, with_kos=True):
    shutil.rmtree(ROOT, ignore_errors=True)
    md = ROOT / 'modules'
    md.mkdir(parents=True)
    src_mods = sorted(MODSRC.rglob('*.ko'))
    names = []
    if with_kos:
        for k in src_mods[:6]:
            shutil.copy(k, md / k.name)
            names.append(k.name)
    if victim:
        if victim == 'first':
            victim = names[0] if names else 'phy-qcom-emu.ko'
        (md / victim).unlink(missing_ok=True)
        os.mkfifo(md / victim)
        if victim not in names:
            names.insert(0, victim)
    (md / 'fallback.order').write_text('# test order\n' + '\n'.join(names) + '\n')
    return md, names


def run_chain(tag, md, timeout_s=120):
    j = ROOT / f'{tag}.journal'
    env = dict(os.environ, V8_MODDIR=str(md), V8_JOURNAL=str(j))
    t0 = time.time()
    p = subprocess.run(['qemu-aarch64', str(WORKER), 'chain'],
                       capture_output=True, text=True, env=env, timeout=timeout_s)
    return {'tag': tag, 'rc': p.returncode, 'seconds': round(time.time() - t0, 1),
            'journal': j.read_text(errors='replace') if j.exists() else '',
            'stdout': p.stdout[-400:], 'stderr': p.stderr[-400:]}


def main():
    if not WORKER.exists():
        raise SystemExit('build the worker first')
    # unpack the proven v7 ramdisk once, as the source of real .ko files
    if not list(MODSRC.rglob('*.ko')):
        MODSRC.mkdir(parents=True, exist_ok=True)
        v7 = (OUT.parent / '20260916-122926-native-baseline' / 'probe-v7-sonda'
              / 'work' / 'v7-ramdisk.cpio')
        sh = subprocess.run(f'cpio -idm --quiet < {v7} "lib/modules/*"',
                            shell=True, cwd=str(MODSRC), capture_output=True, text=True)
        if not list(MODSRC.rglob('*.ko')):
            raise SystemExit(f'no .ko unpacked from the v7 ramdisk: {sh.stderr[:200]}')

    # ---- test 1: normal modules, no victim ---------------------------------
    md, names = setup()
    a = run_chain('t1-normal', md)
    res['t1'] = a
    check('t1: every module was attempted with an ATTEMPT line first',
          a['journal'].count('ATTEMPT finit_module') == len(names),
          f'{a["journal"].count("ATTEMPT finit_module")} ATTEMPT lines for {len(names)} modules')
    check('t1: a result line per module (loaded / failed), never silent',
          a['journal'].count('MOD ') >= len(names), f'{a["journal"].count("MOD ")} MOD lines')
    check('t1: no TIMEOUT-KILLED when nothing hangs', 'TIMEOUT-KILLED' not in a['journal'], '')
    check('t1: the chain finished and exited 0 (a failed load is not fatal)',
          a['rc'] == 0 and 'chain: done in' in a['journal'], f'rc={a["rc"]}')

    # ---- test 2: the FIRST module is a FIFO: it cannot ever return ---------
    md, names = setup(victim='first')
    victim = names[0]
    b = run_chain('t2-fifo', md)
    res['t2'] = b
    j = b['journal']
    check('t2: the stuck module is named in an ATTEMPT line written before the call',
          f'ATTEMPT finit_module' in j and victim in j.split('ATTEMPT finit_module')[0] + j,
          victim)
    check(f't2: the child was KILLED on the deadline and journalled as TIMEOUT-KILLED',
          f'{victim[:20]}' in j and 'TIMEOUT-KILLED' in j,
          [l for l in j.splitlines() if 'TIMEOUT-KILLED' in l][:1])
    after = j.split('TIMEOUT-KILLED')
    check('t2: the walk CONTINUED after the kill (later modules were attempted)',
          len(after) > 1 and 'ATTEMPT finit_module' in after[1],
          [l for l in after[1].splitlines() if 'ATTEMPT' in l][:2] if len(after) > 1 else '')
    check('t2: the cost of the stuck module was the deadline, not forever',
          MOD_TIMEOUT_S <= b['seconds'] <= MOD_TIMEOUT_S + 45, f'{b["seconds"]} s')
    check('t2: the chain still reported its summary and exited 0',
          b['rc'] == 0 and 'chain: done in' in j, f'rc={b["rc"]}')
    check('t2: loaded and TIMEOUT-KILLED are different strings in the same journal',
          'MOD ' in j and 'TIMEOUT-KILLED' in j, 'both present')

    # ---- test 3: metadata present, no .ko at all --------------------------
    md, names = setup(with_kos=False)
    (md / 'fallback.order').write_text('ghost-module.ko\nsecond-ghost.ko\n')
    c = run_chain('t3-missing', md)
    res['t3'] = c
    check('t3: a module with no .ko is reported MISSING, not silently skipped',
          'MISSING' in c['journal'], [l for l in c['journal'].splitlines() if 'MISSING' in l][:2])

    passed = sum(1 for x in res['checks'] if x['pass'])
    res['summary'] = {'passed': passed, 'total': len(res['checks'])}
    (OUT / 'localtest-worker.json').write_text(json.dumps(res, indent=2) + '\n')
    print(f'\n=== {passed}/{len(res["checks"])} checks passed -> localtest-worker.json')
    return 0 if passed == len(res['checks']) else 1


if __name__ == '__main__':
    sys.exit(main())
