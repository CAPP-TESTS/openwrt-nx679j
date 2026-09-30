#!/usr/bin/env python3
"""v6 module plan: closure of the v5 seed over the device's own modules.dep and
modules.softdep, resolved against the device vendor ramdisk .ko files, ordered
topologically. Helper for build-candidate-v6.py; prints a summary."""
import json, sys
from pathlib import Path

VR = Path('/home/user/nx679j-stock/port-work/stock_vendor_ramdisk/lib/modules')
EXP = Path('/home/user/nx679j-stock/experiments/20260916-122926-native-baseline')
def canon(s):
    """mirror of canon() in candidate-init-v6.c"""
    s = s.rsplit('/', 1)[-1]
    s = ''.join('_' if c in '-.' else c.lower() for c in s)
    return s[:-3] if len(s) > 3 and s.endswith('_ko') else s


FILES = {canon(p.name): p for p in sorted(VR.glob('*.ko'))}


def load_map(name, sep=':'):
    out = {}
    for line in (VR / name).read_text().splitlines():
        if sep not in line:
            continue
        left, right = line.split(sep, 1)
        out.setdefault(canon(left), [])
        for t in right.split():
            out[canon(left)].append(canon(t))
    return out


def soft_map():
    pre, post = {}, {}
    for line in (VR / 'modules.softdep').read_text().splitlines():
        t = line.split()
        if len(t) < 3 or t[0] != 'softdep':
            continue
        m, mode = canon(t[1]), None
        for x in t[2:]:
            if x in ('pre:', 'post:'):
                mode = x
            elif mode == 'pre:':
                pre.setdefault(m, []).append(canon(x))
            elif mode == 'post:':
                post.setdefault(m, []).append(canon(x))
    return pre, post
def plan():
    deps = load_map('modules.dep')
    pre, post = soft_map()
    v5 = json.loads((EXP / 'candidate-minimal-gadget-v5/manifest.json').read_text())
    seed = [canon(r['name']) for r in v5['module_table']]
    need, missing, frontier = set(), [], []
    for m in seed:
        if m in FILES:
            need.add(m)
            frontier.append(m)
        else:
            missing.append((m, 'v5 seed'))
    while frontier:
        m = frontier.pop()
        for n in deps.get(m, []) + pre.get(m, []) + post.get(m, []):
            if n in need:
                continue
            if n not in FILES:
                missing.append((n, f'{m} requires it (dep/softdep)'))
                continue
            need.add(n)
            frontier.append(n)
    order, state = [], {}

    def visit(m):
        if state.get(m) == 2:
            return
        if state.get(m) == 1:
            missing.append((m, 'dependency cycle'))
            return
        state[m] = 1
        for n in deps.get(m, []) + pre.get(m, []):
            if n in need:
                visit(n)
        if m not in order:
            order.append(m)
        for n in post.get(m, []):
            if n in need:
                visit(n)
        state[m] = 2

    for m in seed:
        visit(m)
    for m in sorted(need):
        visit(m)
    return dict(seed=seed, need=sorted(need), order=order, missing=missing,
                deps=deps, pre=pre, post=post)
def goals_from_init():
    """the GOALS[] array in candidate-init-v6.c, which are goals not orders"""
    src = (EXP / 'candidate-init-v6.c').read_text()
    blk = src.split('static const char *GOALS[] = {', 1)[1].split('};', 1)[0]
    return [g.strip().strip('"').strip(',') for g in blk.replace('\n', ' ').split(',')
            if g.strip().strip('"')]


def report():
    p = plan()
    goals = [canon(g) for g in goals_from_init()]
    print(f"v5 seed                     : {len(p['seed'])}")
    print(f"closure (bundled set)       : {len(p['need'])}")
    print(f"topological order           : {len(p['order'])} "
          f"({'sorted' if len(p['order']) == len(p['need']) else 'MISMATCH'})")
    print(f"missing from vendor set     : {p['missing'] or 'none'}")
    print(f"softdep-required additions  : "
          f"{sorted(set(p['need']) - set(p['seed']) - set(p['deps'].get('', [])))}")
    print(f"goal names (init GOALS[])   : {len(goals)}, "
          f"unresolved: {[g for g in goals if g not in p['need']] or 'none'}")
    for m in ('qcom_hwspinlock', 'smem', 'phy_generic', 'eud', 'dwc3_msm',
              'ufs_qcom', 'phy_qcom_ufs_qmp_v4_waipio'):
        print(f"  order[{p['order'].index(m) if m in p['order'] else -1:>3}] {m}")
    return p


if __name__ == '__main__':
    p = report()
    sys.exit(1 if p['missing'] else 0)
#@@APPEND@@
