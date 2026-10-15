"""Summarise an exit crawl (scripts/survey-batch.sh with LEVEL/EXIT jobs): for each job, the level, the exit taken,
how many exits the level has, the destination the game asked for, and whether the run crashed or logged errors.
Usage: crawl-report.py CRAWL_ROOT [LEVEL_NAMES]  (names from JFG_DEBUG_LEVELS=names, e.g. build/diag/level-names.txt)"""
import os, re, sys

root = sys.argv[1]
names = {}
if len(sys.argv) > 2:
    for line in open(sys.argv[2]):
        m = re.match(r"(\d+) \(0x[0-9A-F]+\): (.*)", line)
        if m:
            names[int(m[1])] = m[2].strip()

rows = []
for level in sorted(os.listdir(root), key=lambda s: int(s) if s.isdigit() else -1):
    if not level.isdigit():
        continue
    for exit_no in sorted(os.listdir(os.path.join(root, level))):
        log_path = os.path.join(root, level, exit_no, "run.log")
        if not os.path.exists(log_path):
            continue
        log = open(log_path, errors="replace").read()
        exits = re.search(r"\[levels\] (\d+) exits; taking", log)
        triggered = "triggered in level" in log
        dest = None
        after = log.split("triggered in level", 1)[1] if triggered else ""
        m = re.search(r"mainChangeLevel\(level=(\d+)", after)
        if m:
            dest = int(m[1])
        crashed = "[crash]" in log or "Cannot partially unload" in log
        shots = os.path.exists(os.path.join(root, level, exit_no, "t86.png"))
        status = "crash" if crashed or not shots else ("ok" if dest is not None else ("no-exit" if exits and exits[1] == "0" else "no-change"))
        rows.append((int(level), int(exit_no), int(exits[1]) if exits else -1, dest, status))

for level, exit_no, exits, dest, status in rows:
    print(f"{level:3d} {names.get(level, '')[:28]:28s} exit {exit_no}/{exits if exits >= 0 else '?'} -> "
          f"{'' if dest is None else dest:>4} {names.get(dest, '')[:28] if dest is not None else '':28s} {status}")
counts = {}
for r in rows:
    counts[r[4]] = counts.get(r[4], 0) + 1
print("summary:", counts)
