#!/usr/bin/env python3
"""The release's tag (TAG=v1.2.3) must match the app's version everywhere,
or the app would compare the wrong numbers when it looks for updates; and
CHANGELOG.md must say what's new in it."""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
cargo = lambda p: re.search(r'^version\s*=\s*"([^"]+)"', open(os.path.join(ROOT, p), encoding="utf8").read(), re.M).group(1)
found = {
    "tag": os.environ.get("TAG", "").lstrip("v"),
    "VERSION": open(os.path.join(ROOT, "VERSION")).read().strip(),
    "core/Cargo.toml": cargo("core/Cargo.toml"),
    "qt/native/Cargo.toml": cargo("qt/native/Cargo.toml"),
}
if len(set(found.values())) != 1:
    sys.exit(f"Version mismatch: {found}\nRun: python3 scripts/set-version.py <version>")
# What's new, for the release page and the app's update card.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
release_notes = __import__("release-notes")
if not release_notes.notes(found["tag"]):
    sys.exit(f"CHANGELOG.md has no notes for {found['tag']}: add a '## {found['tag']}' section (shown in the update card)")
print(f"version {found['tag']} ok")
