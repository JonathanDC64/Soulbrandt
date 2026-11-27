# TEMPORARY (branch tmp/rpcs3-mac-check, never merged): setup's unlock start with RPCS3's own macOS build.
#   python tools/tmp_rpcs3_mac_check.py <folder that holds RPCS3.app> <work folder>
# Setup's engine finds the app, gives RPCS3 its own folder through HOME (rpcs3.redirect) and starts it as the unlock
# does (rpcs3._Child: stdin from the null device, its output to a file, a session of its own), with one input that is
# not a SELF, named from the work folder. It passes when RPCS3 ends by itself, its lock is in setup's folder, and the
# runner's own ~/Library/Application Support/rpcs3 and ~/Library/Caches/rpcs3 were not made.
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "setup"))
from des_setup import rpcs3  # noqa: E402

apps, work = Path(sys.argv[1]), Path(sys.argv[2])
exe = rpcs3.find(apps)
print("find:", exe)
if not exe:
    sys.exit("no RPCS3 found")
print("where:", rpcs3.where(exe))
print("player_dirs:", rpcs3.player_dirs(exe))
print("is_shared:", rpcs3.is_shared(work / "data", exe))
user = Path.home() / "Library"
real = (user / "Application Support" / "rpcs3", user / "Caches" / "rpcs3")
before = [p.exists() for p in real]

home = work / "rpcs3"
home.mkdir(parents=True)
(work / "in").mkdir()
(work / "in" / "EBOOT.BIN").write_bytes(b"not a SELF, " * 40)
add, lock = rpcs3.redirect(home)
print("redirect:", add, "lock:", lock)
env = dict(os.environ, **rpcs3.NO_MESSAGE_BOXES, **add)
ended = True
for run, args in (("decrypt", ["--decrypt", "in/EBOOT.BIN"]),):
    out = work / f"{run}.txt"
    t = time.monotonic()
    with open(out, "wb") as f:
        child = rpcs3._Child([str(exe)] + args, env, work, f)
        code = child.wait(180)
        if code is None:
            ended = False
            print(f"{run}: still running after 180 s; ended")
            child.end()
            code = child.wait(None)
        child.close()
    print(f"== {run}: exit status {code} after {time.monotonic() - t:.1f} s; its output:")
    print(out.read_text(encoding="utf-8", errors="replace")[-4000:])
print("lock there:", lock.is_file())
print("setup's folder now holds:")
for p in sorted(home.rglob("*"))[:80]:
    print("   ", p.relative_to(home))
after = [p.exists() for p in real]
print("the runner's own RPCS3 folders before / after:", before, after)
ok = ended and lock.is_file() and after == before
print("RESULT:", "ok" if ok else "FAILED")
sys.exit(0 if ok else 1)
