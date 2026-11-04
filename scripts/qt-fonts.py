#!/usr/bin/env python3
"""Fonts for the app, in qt/assets/fonts (committed; run this only to change
them): each face below, from the Fontsource packages on jsDelivr. The latin
subset keeps the family name; the latin-ext subset is renamed "<Family> Ext"
(the app lists it right after the family), since Qt mixes up two files with
the same family and style, and merged files lose their kerning.

Needs fontTools (pip install fonttools). Run: python3 scripts/qt-fonts.py
"""
import glob, io, os, sys, urllib.request
from fontTools.ttLib import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "qt", "assets", "fonts")

# (Fontsource package, version, weight, style, subsets): the faces the text
# presets use (qt/src/draw/Fonts.cpp) and the UI's Inter.
FACES = [
    ("andika", "5.3.0", 400, "normal", "latin latin-ext"),
    ("andika", "5.3.0", 700, "normal", "latin latin-ext"),
    ("gochi-hand", "5.3.0", 400, "normal", "latin"),
    ("handlee", "5.3.0", 400, "normal", "latin"),
    ("inter", "5.3.0", 400, "italic", "latin latin-ext"),
    ("inter", "5.3.0", 400, "normal", "latin latin-ext"),
    ("inter", "5.3.0", 600, "normal", "latin latin-ext"),
    ("inter", "5.3.0", 700, "italic", "latin latin-ext"),
    ("inter", "5.3.0", 700, "normal", "latin latin-ext"),
    ("lora", "5.3.0", 400, "italic", "latin latin-ext"),
    ("lora", "5.3.0", 400, "normal", "latin latin-ext"),
    ("lora", "5.3.0", 700, "italic", "latin latin-ext"),
    ("lora", "5.3.0", 700, "normal", "latin latin-ext"),
    ("mynerve", "5.3.0", 400, "normal", "latin latin-ext"),
    ("noto-sans-arabic", "5.3.0", 400, "normal", "arabic"),
    # Each handwriting preset's Arabic face (qt/src/draw/Fonts.cpp).
    ("noto-naskh-arabic", "5.3.0", 400, "normal", "arabic"),
    ("noto-naskh-arabic", "5.3.0", 700, "normal", "arabic"),
    ("lemonada", "5.3.0", 400, "normal", "arabic"),
    ("lemonada", "5.3.0", 700, "normal", "arabic"),
    ("katibeh", "5.3.0", 400, "normal", "arabic"),
    ("baloo-bhaijaan-2", "5.3.0", 400, "normal", "arabic"),
    ("baloo-bhaijaan-2", "5.3.0", 700, "normal", "arabic"),
    ("aref-ruqaa", "5.3.0", 400, "normal", "arabic"),
    ("amiri", "5.3.0", 400, "normal", "arabic"),
    ("rakkas", "5.3.0", 400, "normal", "arabic"),
    ("changa", "5.3.0", 400, "normal", "arabic"),
    ("marhey", "5.3.0", 400, "normal", "arabic"),
    ("el-messiri", "5.3.0", 400, "normal", "arabic"),
    ("noto-sans-arabic", "5.3.0", 700, "normal", "arabic"),
    ("playpen-sans", "5.3.0", 400, "normal", "latin latin-ext"),
    ("playpen-sans", "5.3.0", 700, "normal", "latin latin-ext"),
    ("playwrite-fr-moderne", "5.3.0", 400, "normal", "latin"),
    ("playwrite-fr-trad", "5.3.0", 400, "normal", "latin"),
    ("schoolbell", "5.3.0", 400, "normal", "latin"),
    ("shantell-sans", "5.3.0", 400, "normal", "latin latin-ext"),
    ("shantell-sans", "5.3.0", 700, "normal", "latin latin-ext"),
]

for f in glob.glob(os.path.join(OUT, "*.ttf")):
    os.remove(f)
for pkg, version, weight, style, subsets in FACES:
    for sub in subsets.split():
        url = f"https://cdn.jsdelivr.net/npm/@fontsource/{pkg}@{version}/files/{pkg}-{sub}-{weight}-{style}.woff"
        try:
            data = urllib.request.urlopen(url).read()
        except Exception as e:
            print("missing", url, e, file=sys.stderr)
            continue
        font = TTFont(io.BytesIO(data))
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
