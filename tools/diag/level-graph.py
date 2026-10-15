"""Build the level graph from exit crawls (scripts/survey-batch.sh with LEVEL/EXIT jobs) and report reachability.
Usage: level-graph.py LEVEL_NAMES START CRAWL_ROOT...   (names from JFG_DEBUG_LEVELS=names)
Prints every level's exits and their destinations, then which levels a player can walk to from START through exits.
Levels entered only by story events (cutscenes, bosses, ship travel) show up as unreachable from exits alone."""
import os, re, sys
from collections import defaultdict, deque

names = {}
for line in open(sys.argv[1]):
    m = re.match(r"(\d+) \(0x[0-9A-F]+\): (.*)", line)
    if m:
        names[int(m[1])] = m[2].strip()
start = int(sys.argv[2])

edges = defaultdict(dict)   # level -> {exit: destination or None}
exit_counts = {}
for root in sys.argv[3:]:
    for level in os.listdir(root):
        if not level.isdigit():
            continue
        for exit_no in os.listdir(os.path.join(root, level)):
            log_path = os.path.join(root, level, exit_no, "run.log")
            if not exit_no.isdigit() or not os.path.exists(log_path):
                continue
            log = open(log_path, errors="replace").read()
            m = re.search(r"\[levels\] (\d+) exits; taking", log)
            if m:
                exit_counts[int(level)] = int(m[1])
            dest = None
            if "triggered in level" in log:
                d = re.search(r"mainChangeLevel\(level=(\d+)", log.split("triggered in level", 1)[1])
                dest = int(d[1]) if d else None
            edges[int(level)][int(exit_no)] = dest

def label(n):
    return f"{n} {names.get(n, '?')}"

print("Exits:")
for level in sorted(edges):
    outs = ", ".join(f"{x}->{d if d is not None else '-'}" for x, d in sorted(edges[level].items()))
    missing = exit_counts.get(level, 0) - len(edges[level])
    print(f"  {label(level)[:40]:40s} {outs}{f'  ({missing} untested)' if missing > 0 else ''}")

seen = {start}
queue = deque([start])
while queue:
    level = queue.popleft()
    for dest in edges.get(level, {}).values():
        if dest is not None and dest not in seen:
            seen.add(dest)
            queue.append(dest)
print(f"\nReachable from {label(start)} through exits: {len(seen)} levels")
print("  " + ", ".join(str(n) for n in sorted(seen)))
crawled = set(edges)
unreached = sorted(crawled - seen)
print(f"\nCrawled levels not reachable through exits from {start} ({len(unreached)}): entered by story events or ship travel")
for n in unreached:
    print(f"  {label(n)}")
dead = sorted(n for n in seen if n in edges and all(d is None for d in edges[n].values()) and edges[n])
print(f"\nReachable levels whose exits all failed to change level ({len(dead)}):", ", ".join(label(n) for n in dead))

# Clusters: levels joined by exits in either direction. JFG's planets should each form one, linked by ship travel.
adjacent = defaultdict(set)
for level, outs in edges.items():
    for dest in outs.values():
        if dest is not None:
            adjacent[level].add(dest)
            adjacent[dest].add(level)
clusters, assigned = [], set()
for level in sorted(set(edges) | set(adjacent)):
    if level in assigned:
        continue
    group, todo = set(), [level]
    while todo:
        n = todo.pop()
        if n not in group:
            group.add(n)
            todo.extend(adjacent[n] - group)
    assigned |= group
    clusters.append(sorted(group))
clusters.sort(key=len, reverse=True)
print(f"\nClusters joined by exits ({len(clusters)}; singletons are cutscenes, tests and levels whose exits never fired):")
for group in clusters:
    if len(group) > 1:
        print(f"  {len(group):3d}: " + "; ".join(f"{n} {names.get(n, '?')[:22]}" for n in group))
print("  singletons: " + ", ".join(str(g[0]) for g in clusters if len(g) == 1))
