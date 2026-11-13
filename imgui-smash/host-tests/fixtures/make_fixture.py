#!/usr/bin/env python3
"""Builds host-tests/fixtures/label-fixture.ttf, the stand-in for a system shared font in the host tests.

Source: Noto Sans CJK JP Regular (SIL Open Font License 1.1, Debian package fonts-noto-cjk,
/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc). The subset keeps the test strings (Latin, kana, kanji,
Hangul, Cyrillic, the ideographic space), every Latin-1 Supplement and Latin Extended-A character the font maps (the
overlay's static set), and the first 160 CJK ideographs from U+4E00 it maps; the CFF outlines are converted to
TrueType glyf (the shared fonts' outline format) and the family is renamed "Label Fixture".

Needs fontTools (tested with 4.60.1):
  python3 -m venv /tmp/venv && /tmp/venv/bin/pip install fonttools==4.60.1
  /tmp/venv/bin/python host-tests/fixtures/make_fixture.py [source.ttc]
"""
import hashlib
import sys
from pathlib import Path

from fontTools import subset
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTCollection, newTable

SOURCE = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
FAMILY = "Noto Sans CJK JP"
OUT = Path(__file__).with_name("label-fixture.ttf")
TEXTS = ["Æsir テスト日本", "æØéÅøÆ", "日本語のなまえ", "한글", "강철무사", "Иван", "\u3000"]
STATIC = range(0xA0, 0x180)
KANJI = 160


def pick_font(path):
    for font in TTCollection(path).fonts:
        if font["name"].getDebugName(1) == FAMILY:
            return font
    raise SystemExit(f"{FAMILY} not in {path}")


def to_truetype(font):
    order = font.getGlyphOrder()
    glyphs = font.getGlyphSet()
    quadratic = {}
    for name in order:
        pen = TTGlyphPen(glyphs)
        glyphs[name].draw(Cu2QuPen(pen, 1.0, reverse_direction=True))
        quadratic[name] = pen.glyph()
    font["loca"] = newTable("loca")
    font["glyf"] = glyf = newTable("glyf")
    glyf.glyphOrder = order
    glyf.glyphs = quadratic
    del font["CFF "]
    if "VORG" in font:
        del font["VORG"]
    glyf.compile(font)
    hmtx = font["hmtx"]
    for name, glyph in glyf.glyphs.items():
        if hasattr(glyph, "xMin"):
            hmtx[name] = (hmtx[name][0], glyph.xMin)
    font["maxp"] = maxp = newTable("maxp")
    maxp.tableVersion = 0x00010000
    for field in ("maxZones",):
        setattr(maxp, field, 1)
    for field in ("maxTwilightPoints", "maxStorage", "maxFunctionDefs", "maxInstructionDefs", "maxStackElements",
                  "maxSizeOfInstructions", "maxComponentElements"):
        setattr(maxp, field, 0)
    post = font["post"]
    post.formatType = 3.0
    font["head"].glyphDataFormat = 0
    font.sfntVersion = "\x00\x01\x00\x00"


def rename(font):
    names = font["name"]
    for record in list(names.names):
        if record.nameID in (1, 3, 4, 6, 16, 17, 21, 22):
            names.removeNames(nameID=record.nameID)
    names.setName("Label Fixture", 1, 3, 1, 0x409)
    names.setName("Regular", 2, 3, 1, 0x409)
    names.setName("Label Fixture Regular", 4, 3, 1, 0x409)
    names.setName("LabelFixture-Regular", 6, 3, 1, 0x409)
    names.setName("Subset of Noto Sans CJK JP Regular, SIL Open Font License 1.1", 3, 3, 1, 0x409)


def main(argv):
    font = pick_font(argv[1] if len(argv) > 1 else SOURCE)
    cmap = font.getBestCmap()
    wanted = {ord(c) for text in TEXTS for c in text if c != " "}
    kanji = [cp for cp in range(0x4E00, 0xA000) if cp in cmap][:KANJI]
    wanted.update(kanji)
    missing = sorted(cp for cp in wanted if cp not in cmap)
    if missing:
        raise SystemExit("source lacks " + " ".join(f"U+{cp:04X}" for cp in missing))
    static = [cp for cp in STATIC if cp in cmap]
    wanted.update(static)
    options = subset.Options()
    options.layout_features = []
    options.hinting = False
    options.notdef_outline = True
    options.name_IDs = ["*"]
    options.drop_tables += ["VORG", "vhea", "vmtx", "BASE", "GPOS", "GSUB", "GDEF"]
    subsetter = subset.Subsetter(options)
    subsetter.populate(unicodes=sorted(wanted))
    subsetter.subset(font)
    to_truetype(font)
    rename(font)
    font.save(OUT)
    data = OUT.read_bytes()
    print(f"{OUT.name}: {len(data)} bytes sha256 {hashlib.sha256(data).hexdigest()} "
          f"codepoints {len(wanted)} kanji U+{kanji[0]:04X}..U+{kanji[-1]:04X} "
          f"static U+00A0..U+017F {len(static)} of {len(STATIC)}")


if __name__ == "__main__":
    main(sys.argv)
