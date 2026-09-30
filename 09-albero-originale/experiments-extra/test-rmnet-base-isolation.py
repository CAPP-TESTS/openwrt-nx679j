#!/usr/bin/env python3
"""Run the isolation script against a temporary filesystem, never real modules."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

SOURCE = Path(__file__).with_name("rmnet-base-isolation.sh").read_text()
ADDONS = ["rmnet_aps", "rmnet_perf_tether", "rmnet_perf",
          "rmnet_offload", "rmnet_shs"]
CORE = ["rmnet_core", "rmnet_ctl", "ipam", "gsim"]


def write(path, text, executable=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    if executable:
        path.chmod(0o755)


def run_case(mutation=None, failed_module=""):
    with tempfile.TemporaryDirectory(prefix="rmnet-isolation-test-") as name:
        root = Path(name)
        write(root / "proc/sys/kernel/random/boot_id", "fixture-boot\n")
        write(root / "proc/uptime", "1.00 1.00\n")
        write(root / "proc/modules", "fixture-only\n")
        write(root / "sys/class/remoteproc/remoteproc3/state", "offline\n")
        (root / "sys/class/net").mkdir(parents=True)
        for module in ADDONS + CORE:
            write(root / f"sys/module/{module}/refcnt",
                  "1\n" if module == "rmnet_shs" else "0\n")
        holder = root / "sys/module/rmnet_shs/holders/rmnet_perf"
        holder.parent.mkdir()
        holder.symlink_to(root / "sys/module/rmnet_perf")
        write(root / "tmp/rawdump-checkpoint.sh",
              '#!/bin/sh\nprintf "%s %s\\n" "$1" "$2" >> '
              '"$RMNET_FIXTURE_ROOT/checkpoints"\n')
        write(root / "bin/dmesg", "#!/bin/sh\nprintf 'fixture-dmesg\\n'\n", True)
        write(root / "bin/rmmod", """#!/usr/bin/env python3
import os
from pathlib import Path
import shutil
import sys
r = Path(os.environ["RMNET_FIXTURE_ROOT"])
assert r.name.startswith("rmnet-isolation-test-")
assert len(sys.argv) == 2
m = sys.argv[1]
assert m in ("rmnet_aps", "rmnet_perf_tether", "rmnet_perf",
             "rmnet_offload", "rmnet_shs")
assert Path(os.environ["LD_LIBRARY_PATH"]).is_dir()
with (r / "calls").open("a") as f:
    f.write(m + "\\n")
if m == os.environ["RMNET_FAIL_MODULE"]:
    sys.exit(23)
shutil.rmtree(r / "sys/module" / m)
if m == "rmnet_perf":
    (r / "sys/module/rmnet_shs/refcnt").write_text("0\\n")
""", True)
        script = re.sub(r"/(?:sys|proc|tmp)/",
                        lambda match: str(root) + match.group(), SOURCE)
        script = script.replace("/sbin/rmmod", str(root / "bin/rmmod"))
        write(root / "run.sh", script)
        if mutation:
            mutation(root)
        essential_before = {m: (root / "sys/module" / m).is_dir() for m in CORE}
        env = dict(os.environ, RMNET_FIXTURE_ROOT=str(root),
                   RMNET_FAIL_MODULE=failed_module,
                   PATH=str(root / "bin") + os.pathsep + os.environ["PATH"])
        args = ["sh", str(root / "run.sh"), "fixture-boot"]
        result = subprocess.run(args, env=env, capture_output=True, timeout=5)
        calls = ((root / "calls").read_text().splitlines()
                 if (root / "calls").exists() else [])
        done = (root / "tmp/rmnet-base-isolation.done").exists()
        assert {m: (root / "sys/module" / m).is_dir() for m in CORE} == essential_before
        if mutation:
            assert result.returncode != 0 and not calls and not done
        elif failed_module:
            assert result.returncode == 23 and not done
            assert calls == ADDONS[:ADDONS.index(failed_module) + 1]
            assert f"failed_remove_{failed_module}" in (
                root / "checkpoints").read_text()
        else:
            assert result.returncode == 0, result.stderr.decode()
            assert calls == ADDONS and done
            checkpoints = (root / "checkpoints").read_text().splitlines()
            expected = ["16 before_base_isolation"]
            for i, module in enumerate(ADDONS):
                expected += [f"{2*i} before_remove_{module}",
                             f"{2*i+1} after_remove_{module}"]
            expected += ["17 after_base_isolation"]
            assert checkpoints == expected
            again = subprocess.run(args, env=env, capture_output=True, timeout=5)
            assert again.returncode != 0
            assert (root / "calls").read_text().splitlines() == calls


run_case()
for failed in ADDONS:
    run_case(failed_module=failed)
for mutation in [
    lambda r: write(r / "proc/sys/kernel/random/boot_id", "wrong\n"),
    lambda r: write(r / "sys/class/remoteproc/remoteproc3/state", "running\n"),
    lambda r: (r / "tmp/observed-bootstrap.once").mkdir(),
    lambda r: (r / "sys/class/net/rmnet_data0").mkdir(),
    lambda r: write(r / "sys/module/rmnet_aps/refcnt", "1\n"),
    lambda r: write(r / "sys/module/rmnet_shs/refcnt", "0\n"),
    lambda r: (r / "sys/module/rmnet_shs/holders/rmnet_perf").unlink(),
    lambda r: (r / "sys/module/rmnet_core").rename(r / "core-preserved"),
]:
    run_case(mutation=mutation)
print("PASS: guards, dependency order, core preservation, checkpoints, "
      "duplicate rejection and stop on each unload error")
