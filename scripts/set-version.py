#!/usr/bin/env python3
"""Sets the app's version: VERSION (which the build reads) and the Rust
crates' Cargo.toml files. Usage: python3 scripts/set-version.py 1.2.3"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
v = (sys.argv[1] if len(sys.argv) > 1 else "").lstrip("v")
if not re.fullmatch(r"\d+\.\d+\.\d+", v):
    sys.exit("usage: python3 scripts/set-version.py 1.2.3")
with open(os.path.join(ROOT, "VERSION"), "w") as f:
    f.write(v + "\n")
for p in ["core/Cargo.toml", "qt/native/Cargo.toml"]:
    path = os.path.join(ROOT, p)
    s = open(path, encoding="utf8").read()
    open(path, "w", encoding="utf8").write(re.sub(r'^version\s*=\s*"[^"]+"', f'version = "{v}"', s, count=1, flags=re.M))
print(f"version set to {v}: commit it, then release it (docs/RELEASING.md)")
