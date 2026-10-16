"""Matrix provenance for one frame (JFG_DEBUG_MTXSRC). Usage: mtx-provenance.py RUN.LOG SYMS.TXT
Joins '[mtxsrc] write' lines (who converted each matrix while the frame was built) with the '[mtxsrc] task' loads
of the graphics task that best matches them, then prints, per writer, how many loads of each kind (camera, relative,
absolute) it explains, and every load nothing explains. SYMS.TXT is build/diag/syms.txt; main code is section 0 at
0x80000450. Return addresses outside main code (relocatable modules) are printed raw."""
import re
import sys
from collections import Counter, defaultdict

log = open(sys.argv[1], errors="replace").read().splitlines()
syms = sorted((0x80000450 + int(off, 16), name) for sec, off, name in (l.split() for l in open(sys.argv[2])) if sec == "0")


def name_of(ra):
    if not (0x80000450 <= ra < 0x800B0000):
        return f"{ra:08X} (module)"
    best = max((s for s in syms if s[0] <= ra), default=None)
    return f"{best[1]}+{ra - best[0]:#x}" if best else f"{ra:08X}"


writes = {}
for line in log:
    m = re.match(r"\[mtxsrc\] write \d+ ([0-9A-F]{8}) ra ([0-9A-F]{8}) via (\w+)", line)
    if m:
        writes[int(m.group(1), 16)] = (name_of(int(m.group(2), 16)), m.group(3))
tasks = defaultdict(list)
for line in log:
    m = re.match(r"\[mtxsrc\] task (-?\d+) load slot (\d+) mul (\d) addr ([0-9A-F]{8})", line)
    if m:
        tasks[int(m.group(1))].append((int(m.group(2)), int(m.group(3)), int(m.group(4), 16)))
if not tasks:
    sys.exit("no '[mtxsrc] task' loads in the log")
task = max(tasks, key=lambda t: sum(1 for _, _, a in tasks[t] if a in writes))
print(f"task {task}: {len(tasks[task])} loads, {len(writes)} conversions recorded")
kinds = defaultdict(Counter)
unexplained = []
for slot, mul, addr in tasks[task]:
    kind = "camera" if slot == 0 and mul == 0 else ("relative" if mul else "absolute")
    if addr in writes:
        kinds[writes[addr]][kind] += 1
    else:
        unexplained.append((addr, slot, mul, kind))
for (writer, via), c in sorted(kinds.items(), key=lambda kv: -sum(kv[1].values())):
    print(f"{writer:48s} {via:5s} " + " ".join(f"{k}={n}" for k, n in sorted(c.items())))
print(f"unexplained loads: {len(unexplained)}")
for addr, slot, mul, kind in unexplained[:60]:
    print(f"  {addr:08X} slot {slot} mul {mul} {kind}")
