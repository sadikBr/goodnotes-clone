#!/usr/bin/env python3
"""Prints a version's notes from CHANGELOG.md (its "## x.y.z" section).
Usage: python3 scripts/release-notes.py 1.2.3 [> notes.md]
Exits with an error when the version has no notes."""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def notes(version):
    text = open(os.path.join(ROOT, "CHANGELOG.md"), encoding="utf8").read()
    m = re.search(r"^## +v?" + re.escape(version) + r"\b[^\n]*\n(.*?)(?=^## |\Z)", text, re.M | re.S)
    return m.group(1).strip() if m else ""


if __name__ == "__main__":
    v = (sys.argv[1] if len(sys.argv) > 1 else "").lstrip("v")
    n = notes(v)
    if not n:
        sys.exit(f"CHANGELOG.md has no notes for {v}: add a '## {v}' section")
    print(n)
