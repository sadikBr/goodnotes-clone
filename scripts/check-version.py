#!/usr/bin/env python3
"""The release's tag (TAG=v1.2.3) must match the app's version everywhere,
or the app would compare the wrong numbers when it looks for updates."""
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
print(f"version {found['tag']} ok")
