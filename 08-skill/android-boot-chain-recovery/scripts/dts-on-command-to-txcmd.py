#!/usr/bin/env python3
"""Convert a vendor panel node's `qcom,mdss-dsi-on-command` hex array from a
DECOMPILED DTS into a device-side shell script that feeds every packet to the
msm DSI debugfs `tx_cmd` node in the format that driver actually parses:
DECIMAL byte values separated by single spaces, one DSI packet per write.

Why decimal: the vendor sde_connector's _sde_debugfs_conn_cmd_tx_write parses
numbers, not hex tokens -- `echo "39 01 00 00 ..."` fails with
`[sde error] input buffer conversion failed` on the first letter it cannot
parse (`b0`, `cf` ...), while pure decimal lines are accepted. Verified on
device: a full 34-packet init sequence delivered 34/34 (`INIT-OK=34 FAIL=0`).

Packet layout emitted (same as in the DT array):
  [type] [last=1] [vc=0] [ack=0] [wait_hi] [wait_lo] [len] [payload...]
Example: DT hex `05 01 00 00 78 00 01 11` (sleep-out, 120 ms wait) becomes
`5 1 0 0 120 0 1 17`.

Typical use (panel bring-up over the running vendor kernel):
  # on host (the live FDT is already decompiled on this device's port tree):
  dts-on-command-to-txcmd.py running_fdt.dts --out /tmp/ddic_init.sh
  scp /tmp/ddic_init.sh root@<dev>:/tmp/
  # on device:
  sh /tmp/ddic_init.sh          # rewrites the DDIC registers + sleep-out + display-on

Notes:
  * busybox sleep on these devices accepts INTEGERS only (`sleep 0.15` ->
    `invalid number`), so waits are rounded to `sleep 1`.
  * only the `[...]` array form is handled; the `<0x... 0x...>` word form is
    ambiguous per byte and must not be guessed -- use the bracket-form node.
  * a node can carry several on-command arrays (one per refresh-rate timing);
    --which selects one, default 0.
  * sending the sequence does NOT by itself guarantee pixels: pair it with the
    disable-all-CRTCs-then-set mode-set and keep the ESD recovery loop off
    (esd_sw_sim_success) while debugging.
"""
import argparse
import re
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dts", help="decompiled .dts text containing the panel node")
    ap.add_argument("--which", type=int, default=0,
                    help="index of the on-command array (per-timing variants)")
    ap.add_argument("--out", default="ddic_init.sh", help="output shell script")
    ap.add_argument("--txcmd",
                    default="/sys/kernel/debug/dri/0/DSI-1/tx_cmd",
                    help="device path of the tx_cmd debugfs node")
    args = ap.parse_args()

    with open(args.dts, "r", errors="replace") as fh:
        txt = fh.read()

    arrays = re.findall(r"qcom,mdss-dsi-on-command\s*=\s*\[([^\]]+)\];", txt)
    if not arrays:
        sys.exit("no 'qcom,mdss-dsi-on-command = [ ... ];' array found "
                 "(word form <0x...> is not supported)")
    if args.which >= len(arrays):
        sys.exit(f"--which {args.which}: only {len(arrays)} array(s) found")
    seq = arrays[args.which].split()

    cmds = []
    i = 0
    while i < len(seq):
        if i + 7 > len(seq):
            sys.exit(f"truncated packet header at byte {i}")
        typ, last, vc, ack = seq[i], seq[i + 1], seq[i + 2], seq[i + 3]
        w1, w2 = seq[i + 4], seq[i + 5]
        ln = int(seq[i + 6], 16)
        payload = seq[i + 7:i + 7 + ln]
        if len(payload) != ln:
            sys.exit(f"packet at byte {i}: declared len {ln}, found {len(payload)}")
        cmds.append((typ, w1, w2, payload))
        i += 7 + ln

    lines = ["#!/bin/sh", f"D={args.txcmd}", "ok=0; fail=0"]
    for typ, w1, w2, payload in cmds:
        full = [typ, "01", "00", "00", w1, w2, f"{len(payload):02x}"] + payload
        dec = " ".join(str(int(b, 16)) for b in full)
        lines.append(f'echo "{dec}" > $D/tx_cmd && ok=$((ok+1)) || fail=$((fail+1))')
        if w1 in ("78", "14") or w2 in ("78", "14"):
            lines.append("sleep 1")   # busybox sleep: integers only
        else:
            lines.append("true")
    lines.append('echo "INIT-OK=$ok FAIL=$fail"')

    with open(args.out, "w") as fh:
        fh.write("\n".join(lines) + "\n")
    print(f"{len(cmds)} packets -> {args.out}")


if __name__ == "__main__":
    main()
