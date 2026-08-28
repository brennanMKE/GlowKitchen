#!/usr/bin/env python3
"""List the issues in issues/ as a table or JSON.

Reads the NNNN.md files that are the source of truth (see issues/Issues.md)
and prints their metadata. Standard library only.

Examples:
  scripts/list_issues.py                    # all issues, newest first
  scripts/list_issues.py --status open      # only open ones
  scripts/list_issues.py --open             # open + in-progress
  scripts/list_issues.py --json             # machine readable
  scripts/list_issues.py --color always      # keep the colors when piping
"""

from __future__ import annotations

import argparse
import html
import json
import os
import re
import sys
from pathlib import Path

ISSUES_DIR = Path(__file__).resolve().parent.parent / "issues"

# Ordered as in issues/Issues.md
STATUS_DISPLAY = {
    "open": "Open",
    "in-progress": "In Progress",
    "resolved": "Resolved",
    "closed": "Closed",
    "wontfix": "Won't Fix",
}
UNRESOLVED = ("open", "in-progress")

# ANSI SGR codes, one per status.
STATUS_COLOR = {
    "open": "1;33",       # bold yellow — needs attention
    "in-progress": "1;36",  # bold cyan — being worked
    "resolved": "32",     # green — done, awaiting confirmation
    "closed": "2;32",     # dim green — confirmed
    "wontfix": "2;31",    # dim red — acknowledged, not doing it
}
HEADER_COLOR = "1"
RULE_COLOR = "2"

FILENAME_RE = re.compile(r"^\d{4}\.md$")
TITLE_RE = re.compile(r"^#\s+(\d{4})\s*[—-]\s*(.*)$")
META_RE = re.compile(r"^\|\s*\*\*(?P<key>[^*]+)\*\*\s*\|\s*(?P<value>.*?)\s*\|\s*$")


def parse_issue(path: Path) -> dict:
    number = path.stem
    title = ""
    meta = {}

    with path.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not title:
                match = TITLE_RE.match(line)
                if match:
                    number, title = match.group(1), html.unescape(match.group(2).strip())
                continue
            if line.startswith("## "):
                break  # metadata table sits between the title and the first section
            match = META_RE.match(line)
            if match:
                meta[match.group("key").strip().lower()] = html.unescape(match.group("value"))

    status = meta.get("status", "").strip().lower()
    return {
        "number": number,
        "title": title or "(untitled)",
        "status": status,
        "status_display": STATUS_DISPLAY.get(status, status or "?"),
        "module": meta.get("module", ""),
        "platform": meta.get("platform", ""),
        "first_seen": meta.get("first seen", ""),
        "closed": meta.get("closed", ""),
        "branch": meta.get("branch", ""),
        "commit": meta.get("commit", ""),
        "path": str(path),
    }


def load_issues(directory: Path) -> list[dict]:
    if not directory.is_dir():
        sys.exit(f"no issues directory at {directory}")
    files = sorted(p for p in directory.iterdir() if FILENAME_RE.match(p.name))
    return [parse_issue(p) for p in files]


def want_color(mode: str) -> bool:
    """Honor --color, then the NO_COLOR/FORCE_COLOR conventions, then isatty()."""
    if mode == "always":
        return True
    if mode == "never":
        return False
    if os.environ.get("NO_COLOR"):
        return False
    if os.environ.get("FORCE_COLOR"):
        return True
    return sys.stdout.isatty()


def strip_markdown(text: str) -> str:
    text = re.sub(r"`([^`]*)`", r"\1", text)
    text = re.sub(r"\*\*([^*]*)\*\*", r"\1", text)
    return text


def print_table(issues: list[dict], width: int, color: bool) -> None:
    rows = [
        (i["number"], i["status_display"], i["first_seen"], strip_markdown(i["title"]))
        for i in issues
    ]
    headers = ("#", "Status", "First seen", "Title")
    widths = [max(len(h), *(len(r[c]) for r in rows)) for c, h in enumerate(headers)]

    # Let the title column absorb whatever terminal width is left.
    title_room = width - (sum(widths[:-1]) + 3 * len(widths))
    if title_room > 10:
        widths[-1] = min(widths[-1], title_room)

    def paint(text: str, sgr: str | None) -> str:
        # Applied after padding so the escape codes never affect column widths.
        return f"\033[{sgr}m{text}\033[0m" if color and sgr else text

    def fmt(cells, sgrs=None):
        parts = []
        for index, (cell, w) in enumerate(zip(cells, widths)):
            cell = cell if len(cell) <= w else cell[: w - 1] + "…"
            parts.append(paint(cell.ljust(w), sgrs[index] if sgrs else None))
        line = "  ".join(parts)
        return line if color else line.rstrip()

    print(fmt(headers, [HEADER_COLOR] * len(headers)))
    print(paint("  ".join("-" * w for w in widths), RULE_COLOR))
    for row, issue in zip(rows, issues):
        print(fmt(row, [None, STATUS_COLOR.get(issue["status"]), None, None]))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dir", type=Path, default=ISSUES_DIR, help="issues directory (default: issues/)")
    parser.add_argument("--status", action="append", metavar="VALUE",
                        help="only this status; repeatable (open, in-progress, resolved, closed, wontfix)")
    parser.add_argument("--open", dest="only_open", action="store_true",
                        help="shorthand for --status open --status in-progress")
    parser.add_argument("--json", dest="as_json", action="store_true", help="emit JSON")
    parser.add_argument("--reverse", action="store_true", help="oldest issue first")
    parser.add_argument("--width", type=int, default=120, help="table width (default: 120)")
    parser.add_argument("--color", choices=("auto", "always", "never"), default="auto",
                        help="colorize the status column (default: auto — only when stdout is a terminal)")
    args = parser.parse_args()

    wanted = {s.strip().lower() for s in (args.status or [])}
    if args.only_open:
        wanted.update(UNRESOLVED)

    unknown = wanted - set(STATUS_DISPLAY)
    if unknown:
        sys.exit(f"unknown status: {', '.join(sorted(unknown))}")

    issues = load_issues(args.dir)
    if wanted:
        issues = [i for i in issues if i["status"] in wanted]
    if not args.reverse:
        issues.reverse()

    if args.as_json:
        print(json.dumps(issues, indent=2))
        return
    if not issues:
        print("no matching issues")
        return
    color = want_color(args.color)
    print_table(issues, args.width, color)

    counts = {}
    for issue in issues:
        counts[issue["status"]] = counts.get(issue["status"], 0) + 1
    parts = []
    for status in STATUS_DISPLAY:  # report in lifecycle order
        if status in counts:
            label = f"{counts[status]} {STATUS_DISPLAY[status].lower()}"
            parts.append(f"\033[{STATUS_COLOR[status]}m{label}\033[0m" if color else label)
    print(f"\n{len(issues)} issue{'s' if len(issues) != 1 else ''} ({', '.join(parts)})")


if __name__ == "__main__":
    main()
