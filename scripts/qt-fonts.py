#!/usr/bin/env python3
"""Fonts for the native Qt app, in qt/assets/fonts: each face the web app
uses, from @fontsource. The latin subset keeps the family name; the
latin-ext subset is renamed "<Family> Ext" (the app lists it right after the
family), since Qt mixes up two files with the same family and style, and
merged files lose their kerning.

Needs fontTools (pip install fonttools). Run after changing fonts in
src/fonts.ts: python3 scripts/qt-fonts.py
"""
import glob, os, re, sys
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "qt", "assets", "fonts")
FONTS_TS = open(os.path.join(ROOT, "src", "fonts.ts"), encoding="utf8").read()

# Faces the web app imports: (package, subset, weight, style)
faces = {}
for pkg, spec in re.findall(r'"@fontsource/([a-z0-9-]+)/([a-z0-9-]+)\.css"', FONTS_TS):
    m = re.match(r"^(?:(latin-ext|latin|arabic)-)?(\d+)(-italic)?$", spec)
    if not m:
        continue
    subsets = [m.group(1)] if m.group(1) else ["latin", "latin-ext"]
    key = (pkg, m.group(2), "italic" if m.group(3) else "normal")
    faces.setdefault(key, set()).update(subsets)
# The UI's semibold Inter.
faces.setdefault(("inter", "600", "normal"), set()).update(["latin", "latin-ext"])

for f in glob.glob(os.path.join(OUT, "*")):
    os.remove(f)
for (pkg, weight, style), subsets in sorted(faces.items()):
    files = []
    for sub in ["latin", "latin-ext", "arabic"]:
        if sub not in subsets:
            continue
        p = os.path.join(ROOT, "node_modules", "@fontsource", pkg, "files", f"{pkg}-{sub}-{weight}-{style}.woff")
        if os.path.exists(p):
            files.append(p)
    if not files:
        print("missing", pkg, weight, style, file=sys.stderr)
        continue
    for p in files:
        sub = "latin-ext" if "-latin-ext-" in p else "arabic" if "-arabic-" in p else "latin"
        font = TTFont(p)
        font.flavor = None
        if sub == "latin-ext":
            for n in font["name"].names:
                if n.nameID in (1, 16):
                    n.string = str(n) + " Ext"
                elif n.nameID in (4, 6):
                    n.string = str(n).replace(" ", " Ext ", 1) if " " in str(n) else str(n) + " Ext"
        out = os.path.join(OUT, f"{pkg}-{sub}-{weight}-{style}.ttf")
        font.save(out)
        print(os.path.basename(out))
