#!/usr/bin/env python3
"""Summarise and compare the GPU profiler output of Switch stderr.log files.

The renderer's `[Switch] SwitchGpuPassProfiler` prints a `[gpu passes]` table every 300 frames and
`SwitchGpuDrawProfiler` adds `[gpu draws]` tables; the driver's `NVK_SHADER_STATS=1` prints one
`estadisticas de shader:` line per compiled shader. This script takes the steady part of each log
(tables whose GPU frame time is within a few percent of the slowest table, i.e. the benchmark spot
after loading) and prints:

- per log: the GPU frame time and the time of every pass (median over the steady tables);
- with two or more logs: the difference of every pass against the first log;
- the most expensive draw groups, with the registers, occupancy, instruction count and spills of their
  shaders when a log with shader statistics is given;
- round 13, `SwitchGpuSlowFrameMs`: the `[gpu slow frames]` parts of every report of a log merged (the
  steady-table filter does not apply: dips are what they are about): the passes and draw groups by the GPU
  time they add in slow frames against the average frame, weighted by each report's slow frames, and the
  slowest frames of the whole log.

Usage:
    tools/switch-gpu-profile.py baseline.log other.log [more.log ...]
    tools/switch-gpu-profile.py --stats first-launch.log draws.log
"""

from __future__ import annotations

import argparse
import re
import statistics
import sys
from collections import defaultdict
from pathlib import Path

PASS_HEADER = re.compile(r"^\[gpu passes\] average over (\d+) frames, GPU frame ([\d.]+) ms:")
PASS_LINE = re.compile(r"^\s+([\d.]+) ms\s+(.*?)\s+draws\s+([\d.]+)\s*$")
DRAW_HEADER = re.compile(r"^\[gpu draws\] (.*?): ([\d.]+) ms, ([\d.]+) draws per frame")
DRAW_LINE = re.compile(r"^\s+([\d.]+) ms\s+([\d.]+) draws\s+([\d.]+) verts\s+ps (\S+)\s+vs (\S+)\s+(.*)$")
STATS_LINE = re.compile(r"estadisticas de shader: (\w+) spirv ([0-9a-f]{8}), (\d+) registros, (\d+) B de memoria local, "
                        r"(\d+) warps por SM, (\d+) B de codigo, (\d+) instrucciones, (\d+) ciclos estaticos, "
                        r"(\d+)/(\d+) spills/fills")
# Round 13 (SwitchGpuSlowFrameMs).
SLOW_HEADER = re.compile(r"^\[gpu slow frames\] (\d+) of (\d+) frames with a GPU frame of (\d+) ms or more "
                         r"\(longest ([\d.]+) ms, average ([\d.]+) ms; all frames ([\d.]+) ms\)")
SLOW_NONE = re.compile(r"^\[gpu slow frames\] none of (\d+) frames with a GPU frame of (\d+) ms or more")
SLOW_PASS = re.compile(r"^\s+([+-][\d.]+) ms\s+([\d.]+) vs\s+([\d.]+)\s+(.*?)\s+draws\s+([\d.]+) vs\s+([\d.]+)\s*$")
SLOWEST = re.compile(r"^\s{4}([\d.]+) ms \(frame (\d+), ([\d.]+) s\): (.*)$")
SLOW_DRAW = re.compile(r"^\s+([+-][\d.]+) ms\s+([\d.]+) vs\s+([\d.]+)\s+([\d.]+) vs\s+([\d.]+) draws\s+(.*?)\s+ps (\S+) vs (\S+) (.*)$")


def new_slow_report(slow, frames, threshold, longest=0.0, average=0.0, all_frames=0.0):
    return {"slow": slow, "frames": frames, "threshold": threshold, "longest": longest, "average": average,
            "all": all_frames, "counters": "", "passes": [], "slowest": [], "draws": []}


def parse(path: Path):
    tables = []      # (gpu frame ms, {pass: ms}, {pass: draws}, ["per frame: ..." lines])
    draws = []       # (pass, pass ms, [(ms, draws, verts, ps, vs, state)]) in log order
    stats = defaultdict(list)
    slow_reports = []  # round 13: one new_slow_report() per "[gpu slow frames]" part
    current = None
    current_draws = None
    current_slow = None
    slow_draws = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = SLOW_HEADER.match(line)
        if match:
            current_slow = new_slow_report(int(match.group(1)), int(match.group(2)), int(match.group(3)),
                                           float(match.group(4)), float(match.group(5)), float(match.group(6)))
            slow_reports.append(current_slow)
            slow_draws = False
            current = current_draws = None
            continue
        match = SLOW_NONE.match(line)
        if match:
            slow_reports.append(new_slow_report(0, int(match.group(1)), int(match.group(2))))
            current = current_draws = current_slow = None
            continue
        if current_slow is not None:
            if line.startswith("[gpu slow draws]"):
                slow_draws = True
                continue
            if line.startswith("  per frame, slow vs all:"):
                current_slow["counters"] = line.strip()
                continue
            match = SLOWEST.match(line)
            if match:
                current_slow["slowest"].append((float(match.group(1)), int(match.group(2)), float(match.group(3)),
                                                match.group(4)))
                continue
            match = SLOW_DRAW.match(line) if slow_draws else SLOW_PASS.match(line)
            if match and slow_draws:
                current_slow["draws"].append((float(match.group(1)), float(match.group(2)), float(match.group(3)),
                                              float(match.group(4)), float(match.group(5)), match.group(6),
                                              match.group(7), match.group(8), match.group(9).strip()))
                continue
            if match:
                current_slow["passes"].append((float(match.group(1)), float(match.group(2)), float(match.group(3)),
                                               " ".join(match.group(4).split()), float(match.group(5)),
                                               float(match.group(6))))
                continue
            if not line.startswith("  "):
                current_slow = None
        match = STATS_LINE.search(line)
        if match:
            stage, spirv = match.group(1), match.group(2)
            # stage, registers, local memory, warps, instructions, spills, fills (group 8 is static cycles)
            stats[spirv].append((stage, int(match.group(3)), int(match.group(4)), int(match.group(5)),
                                 int(match.group(7)), int(match.group(9)), int(match.group(10))))
            continue
        match = PASS_HEADER.match(line)
        if match:
            current = (float(match.group(2)), {}, {}, [])
            tables.append(current)
            current_draws = None
            continue
        match = DRAW_HEADER.match(line)
        if match:
            current_draws = (match.group(1), float(match.group(2)), [])
            draws.append((len(tables) - 1, current_draws))
            current = None
            continue
        if current is not None:
            match = PASS_LINE.match(line)
            if match:
                name = " ".join(match.group(2).split())
                current[1][name] = float(match.group(1))
                current[2][name] = float(match.group(3))
                continue
            # "  (x ms in the passes listed)" and "  per frame: ..." belong to the table.
            if line.startswith("  per frame:"):
                current[3].append(line.strip())
            elif not line.startswith("  ("):
                current = None
        if current_draws is not None:
            match = DRAW_LINE.match(line)
            if match:
                current_draws[2].append((float(match.group(1)), float(match.group(2)), float(match.group(3)),
                                         match.group(4), match.group(5), match.group(6).strip()))
            elif not line.startswith("  "):
                current_draws = None
    return tables, draws, stats, slow_reports


def print_slow(path: Path, slow_reports, all_stats, rows: int) -> None:
    """Round 13: the [gpu slow frames] parts of one log, merged (weighted by each report's slow frames)."""
    if not slow_reports:
        return
    slow = sum(r["slow"] for r in slow_reports)
    frames = sum(r["frames"] for r in slow_reports)
    threshold = slow_reports[-1]["threshold"]
    print(f"\n{path.name}: slow GPU frames (SwitchGpuSlowFrameMs = {threshold}): {slow} of {frames} frames in "
          f"{len(slow_reports)} reports", end="")
    if not slow:
        print()
        return
    longest = max(r["longest"] for r in slow_reports)
    average = sum(r["average"] * r["slow"] for r in slow_reports) / slow
    print(f", longest {longest:.1f} ms, average {average:.1f} ms")

    added = defaultdict(float)
    slow_ms = defaultdict(float)
    all_ms = defaultdict(float)
    slow_draws = defaultdict(float)
    all_draws = defaultdict(float)
    for report in slow_reports:
        weight = report["slow"]
        for diff, slow_value, all_value, name, s_draws, a_draws in report["passes"]:
            added[name] += diff * weight
            slow_ms[name] += slow_value * weight
            all_ms[name] += all_value * weight
            slow_draws[name] += s_draws * weight
            all_draws[name] += a_draws * weight
    print("  passes by the GPU time they add in slow frames (ms per slow frame; the reports' 'all frames' averages, weighted "
          "the same way):")
    for name in sorted(added, key=lambda n: -added[n])[:rows]:
        print(f"    {added[name] / slow:+7.2f} ms  {slow_ms[name] / slow:6.2f} vs {all_ms[name] / slow:6.2f}  {name:<40} "
              f"draws {slow_draws[name] / slow:5.0f} vs {all_draws[name] / slow:5.0f}")

    print("  slowest frames of the log:")
    for ms, frame, seconds, text in sorted((f for r in slow_reports for f in r["slowest"]), reverse=True)[:10]:
        print(f"    {ms:6.1f} ms  frame {frame} at {seconds:.1f} s: {text}")

    # The counters of the report with the most slow frames.
    busiest = max(slow_reports, key=lambda r: r["slow"])
    if busiest["counters"]:
        print(f"  {busiest['counters']} (report with the most slow frames: {busiest['slow']})")

    groups = defaultdict(float)
    for report in slow_reports:
        for diff, _, _, _, _, name, ps, vs, state in report["draws"]:
            groups[(name, ps, vs, state)] += diff * report["slow"]
    if groups:
        print("  draw groups by the GPU time they add in slow frames (ms per slow frame):")
        for (name, ps, vs, state), value in sorted(groups.items(), key=lambda item: -item[1])[:rows]:
            print(f"    {value / slow:+7.3f} ms  {name:<34} ps {ps:<26} {shader_stats(all_stats, ps):<22} "
                  f"vs {vs:<26} {state}")


def steady(tables, tolerance):
    """The tables of the benchmark spot: GPU frame within `tolerance` of the slowest steady value."""
    if not tables:
        return []
    frames = sorted(t[0] for t in tables)
    # The slowest tables are the scene; ignore a single outlier at the very top.
    reference = frames[-2] if len(frames) >= 4 else frames[-1]
    return [t for t in tables if abs(t[0] - reference) <= reference * tolerance]


def summarise(tables):
    frame = statistics.median(t[0] for t in tables)
    passes = defaultdict(list)
    draws = defaultdict(list)
    for _, table, table_draws, _ in tables:
        for name, ms in table.items():
            passes[name].append(ms)
            draws[name].append(table_draws[name])
    return frame, {name: statistics.median(values) for name, values in passes.items()}, \
        {name: statistics.median(values) for name, values in draws.items()}


def shader_stats(stats, name):
    """'registers/warps/instructions[/spills]' for a 'HASH/spirv' shader name, if known."""
    if "/" not in name:
        return ""
    spirv = name.split("/")[1]
    entries = stats.get(spirv)
    if not entries:
        return ""
    stage, registers, local, warps, instructions, spills, fills = max(entries, key=lambda e: e[4])
    text = f"{registers}r {warps}w {instructions}i"
    if spills or fills or local:
        text += f" SPILLS {spills}/{fills} {local}B"
    if len(entries) > 1:
        text += f" (x{len(entries)})"
    return text


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--stats", type=Path, action="append", default=[],
                        help="extra log(s) holding NVK_SHADER_STATS lines (e.g. the first launch)")
    parser.add_argument("--tolerance", type=float, default=0.04, help="steady-table window (default 4%%)")
    parser.add_argument("--passes", type=int, default=25, help="passes to list (default 25)")
    args = parser.parse_args()

    all_stats = defaultdict(list)
    parsed = []
    slow_by_log = []
    for path in args.logs + args.stats:
        tables, draws, stats, slow_reports = parse(path)
        for key, value in stats.items():
            all_stats[key].extend(value)
        if path in args.logs:
            parsed.append((path, tables, draws))
            slow_by_log.append((path, slow_reports))

    summaries = []
    for path, tables, _ in parsed:
        chosen = steady(tables, args.tolerance)
        if not chosen:
            print(f"{path}: no [gpu passes] tables")
            continue
        frame, passes, draw_counts = summarise(chosen)
        summaries.append((path, frame, passes, draw_counts, len(chosen), len(tables), chosen[-1][3]))

    if not summaries:
        sys.exit("No profiler tables found.")

    for path, slow_reports in slow_by_log:
        print_slow(path, slow_reports, all_stats, args.passes)
    if any(reports for _, reports in slow_by_log):
        print()

    base_path, base_frame, base_passes, base_draws, _, _, _ = summaries[0]
    for path, frame, _, _, used, total, per_frame in summaries:
        delta = "" if path == base_path else f"  ({frame - base_frame:+.2f} ms, {100 * (frame - base_frame) / base_frame:+.1f} %)"
        print(f"{path.name}: GPU frame {frame:.2f} ms over {used} of {total} tables{delta}")
        # The counters of the last steady table (resolve copies, barriers, buffer unlocks...).
        for line in per_frame:
            print(f"    {line}")

    print()
    header = f"{'pass':<42} {'draws':>6} " + " ".join(f"{p.name[:14]:>14}" for p, *_ in summaries)
    print(header)
    names = sorted(base_passes, key=lambda n: -base_passes[n])[:args.passes]
    for name in names:
        cells = [f"{base_passes[name]:14.2f}"]
        for _, _, passes, _, _, _, _ in summaries[1:]:
            value = passes.get(name)
            cells.append(f"{'-':>14}" if value is None else f"{value - base_passes[name]:+14.2f}")
        print(f"{name[:42]:<42} {base_draws[name]:6.0f} " + " ".join(cells))

    for path, tables, draws in parsed:
        if not draws:
            continue
        chosen = {id(t) for t in steady(tables, args.tolerance)}
        groups = defaultdict(lambda: defaultdict(list))
        pass_ms = defaultdict(list)
        for table_index, (name, ms, lines) in draws:
            if table_index < 0 or id(tables[table_index]) not in chosen:
                continue
            pass_ms[name].append(ms)
            for line in lines:
                groups[name][(line[3], line[4], line[5])].append(line)
        if not groups:
            continue
        print(f"\n{path.name}: most expensive draw groups (median ms per frame; r/w/i = registers, warps per SM, instructions)")
        for name in sorted(pass_ms, key=lambda n: -statistics.median(pass_ms[n])):
            print(f"  {name}: {statistics.median(pass_ms[name]):.2f} ms")
            rows = []
            for (ps, vs, state), lines in groups[name].items():
                rows.append((statistics.median(l[0] for l in lines), statistics.median(l[1] for l in lines),
                             statistics.median(l[2] for l in lines), ps, vs, state))
            for ms, count, verts, ps, vs, state in sorted(rows, reverse=True)[:12]:
                print(f"    {ms:6.3f} ms {count:5.1f} draws {verts:8.0f} verts  ps {ps:<26} {shader_stats(all_stats, ps):<22} "
                      f"vs {vs:<26} {shader_stats(all_stats, vs):<22} {state}")


if __name__ == "__main__":
    main()
