#!/usr/bin/env python3
"""Regenerate the app's LVGL Chinese font subsets (app_font_14/20/40.c).

The originals were produced with lv_font_conv (node), which is not available on
this machine; this script reproduces the same lv_font_conv output layout with
Pillow's FreeType rasterizer:

- 4 bpp antialiased bitmaps, tightly bit-packed (rows are NOT padded to bytes,
  matching the bitmaps lv_font_conv emitted for these files),
- glyph_dsc with bitmap_index/adv_w(1/16 px)/box/ofs fields,
- cmaps: FORMAT0_TINY for ASCII 0x20-0x7E + SPARSE_TINY for CJK,
- no kerning (kern_dsc = NULL).

The charset is scanned from the C string literals under main/ so the subsets
track the actual UI copy: glyphs of retired characters drop out and new copy
automatically gets its glyphs.
Run from anywhere:  python tools/gen_app_fonts.py
"""
from __future__ import annotations

import re
from pathlib import Path

from PIL import Image, ImageFont

HERE = Path(__file__).resolve().parent
FONT_DIR = HERE.parent / "main" / "assets" / "fonts"
TTF = HERE.parent / "managed_components" / "lvgl__lvgl" / "scripts" / \
    "generators" / "built_in_font" / "SourceHanSansSC-Normal.otf"

SRC_DIR = HERE.parent / "main"
# CJK cmap covers 0x4E00..0x958F (range_start 19968, length 18412).
CMAP_START = 19968
CMAP_LEN = 18412

# Runtime-only text (e.g. approved commands echoed from the host) can contain
# characters that never appear in static UI copy, so the source scan can't
# discover them. Append such characters here as they show up as tofu boxes.
EXTRA_DYNAMIC = [
    0x95EB,  # 闫 (commit author name in git commands)
    0x4E0D,  # 不
    0x4E0A,  # 上
]

SIZES = [14, 20, 40]
FONT_NAMES = {14: "app_font_14", 20: "app_font_20", 40: "app_font_40"}


def scan_cjk_from_sources() -> list[int]:
    """Collect every CJK char used inside C string literals under main/.

    Line comments are stripped first so commented-out copy and prose in
    comments don't leak glyphs into the subsets. Chars outside the CJK cmap
    range are rejected loudly instead of silently breaking the lookup.
    """
    str_pat = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
    cps: set[int] = set()
    for f in sorted(SRC_DIR.glob("*.c")):
        text = f.read_text(encoding="utf-8", errors="ignore")
        text = re.sub(r"//[^\n]*", "", text)
        for m in str_pat.finditer(text):
            for ch in m.group(1):
                cp = ord(ch)
                if 0x4E00 <= cp <= 0x9FFF:
                    cps.add(cp)
    if not cps:
        raise SystemExit("no CJK characters found in main/*.c string literals")
    bad = [hex(c) for c in sorted(cps) if not (CMAP_START <= c < CMAP_START + CMAP_LEN)]
    if bad:
        raise SystemExit(f"codepoints outside the supported CJK cmap range: {bad}")
    return sorted(cps)


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, ascent: int):
    """Return (adv_w16, box_w, box_h, ofs_x, ofs_y, packed_4bpp bytes).

    LVGL fmt_txt `ofs_y` is the bitmap BOTTOM relative to the baseline,
    positive upwards (lv_draw_label places the bitmap top at
    baseline_y - box_h - ofs_y). Pillow reports `my` as the bitmap top
    below the ascender line, so the bottom-above-baseline value is
    ascent - my - box_h.
    """
    adv = int(round(font.getlength(ch) * 16))
    if adv > 0xFFF:
        adv = 0xFFF
    mask, (mx, my) = font.getmask2(ch, mode="L")
    w, h = mask.size
    if w <= 0 or h <= 0:
        return adv, 0, 0, 0, 0, b""
    img = Image.frombuffer("L", mask.size, bytes(mask))
    # Pillow pads the mask of baseline-hugging glyphs (quotes, dashes, ...)
    # down to the baseline; lv_font_conv emits the bare ink box. Trim to the
    # ink bounding box and shift the offsets accordingly.
    bbox = img.getbbox()
    if bbox is None:
        return adv, 0, 0, 0, 0, b""
    if bbox != (0, 0, w, h):
        mx += bbox[0]
        my += bbox[1]
        img = img.crop(bbox)
        w, h = img.size
    px = img.load()
    # Continuous 4bpp bit stream, MSB first, row-major.
    bits: list[int] = []
    for y in range(h):
        for x in range(w):
            bits.append((px[x, y] * 15 + 127) // 255)
    out = bytearray()
    for i in range(0, len(bits), 2):
        hi = bits[i]
        lo = bits[i + 1] if i + 1 < len(bits) else 0
        out.append((hi << 4) | lo)
    return adv, w, h, mx, ascent - my - h, bytes(out)


def parse_old_metrics(path: Path) -> tuple[int, int]:
    text = path.read_text(encoding="utf-8")
    lh = re.search(r"\.line_height = (\d+)", text)
    bl = re.search(r"\.base_line = (\d+)", text)
    if not lh or not bl:
        raise SystemExit(f"no line_height/base_line in {path}")
    return int(lh.group(1)), int(bl.group(1))


def gen_font(size: int, name: str, codepoints: list[int],
             line_height: int, base_line: int) -> None:
    font = ImageFont.truetype(str(TTF), size)
    ascent_pil, descent_pil = font.getmetrics()
    # Keep the old files' line metrics so existing layouts don't shift; glyphs
    # stay anchored to the baseline via ofs_y (see render_glyph).

    ascii_glyphs = list(range(0x20, 0x7F))
    cjk = sorted(set(codepoints) | set(EXTRA_DYNAMIC))
    ordered = ascii_glyphs + cjk  # glyph ids: 1..95 ascii, 96.. cjk

    bitmaps = bytearray()
    dsc_lines = []
    cjk_offsets = []
    for idx, cp in enumerate(ordered, start=1):
        ch = chr(cp)
        adv, bw, bh, ox, oy, data = render_glyph(font, ch, ascent_pil)
        dsc_lines.append(
            "    {{.bitmap_index = {bi}, .adv_w = {adv}, .box_w = {bw}, "
            ".box_h = {bh}, .ofs_x = {ox}, .ofs_y = {oy}}},".format(
                bi=len(bitmaps), adv=adv, bw=bw, bh=bh, ox=ox, oy=oy))
        bitmaps += data
        if cp > 0x7E:
            cjk_offsets.append(cp - 19968)

    cmaps = [
        '    {\n'
        '        .range_start = 32, .range_length = 95, .glyph_id_start = 1,\n'
        '        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, '
        '.type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n'
        '    },\n'
        '    {\n'
        '        .range_start = 19968, .range_length = 18412, '
        '.glyph_id_start = 96,\n'
        '        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, '
        f'.list_length = {len(cjk_offsets)}, '
        '.type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY\n'
        '    },\n'
    ]

    def fmt_bytes(data: bytes, per_line: int = 16) -> str:
        # Every row keeps a trailing comma (like lv_font_conv output), so rows
        # can be joined by plain newlines.
        rows = [", ".join(f"0x{b:02x}" for b in data[i:i + per_line]) + ","
                for i in range(0, len(data), per_line)]
        return "\n".join("    " + r for r in rows)

    uni_rows = []
    row = []
    for off in cjk_offsets:
        row.append(f"0x{off:x}")
        if len(row) == 8:
            uni_rows.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        uni_rows.append("    " + ", ".join(row) + ",")

    guard = name.upper()
    # Embed the complete charset as an lv_font_conv-style --range so the file
    # is self-describing and can be re-parsed on the next run.
    range_str = "0x20-0x7E," + ",".join(f"0x{c:04X}" for c in cjk)
    src = f"""/*******************************************************************************
 * Size: {size} px
 * Bpp: 4
 * Opts: generated by tools/gen_app_fonts.py (Pillow rasterizer, lv_font_conv
 * layout, no kerning). Charset: --range {range_str}
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef {guard}
#define {guard}

/*-----------------
 *  BITMAPS
 *----------------*/

static const uint8_t glyph_bitmap[] = {{
{fmt_bytes(bytes(bitmaps))}
}};

/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {{
    {{.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}} /* id = 0 reserved */,
{chr(10).join(dsc_lines)}
}};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_1[] = {{
{chr(10).join(uni_rows)}
}};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{{
{"".join(cmaps)}}};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {{
#else
static lv_font_fmt_txt_dsc_t font_dsc = {{
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 16,
    .cmap_num = 2,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = NULL
#endif
}};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t {name} = {{
#else
lv_font_t {name} = {{
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = {line_height},          /*The maximum line height required by the font*/
    .base_line = {base_line},             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = {-max(1, size // 14)},
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
}};



#endif /*#if {guard}*/
"""
    out = FONT_DIR / f"{name}.c"
    out.write_text(src, encoding="utf-8", newline="\n")
    print(f"{name}: {len(ordered) + 1} glyphs, line_height={line_height}, "
          f"base_line={base_line}, bitmap={len(bitmaps)} B -> {out}")


def main() -> None:
    if not TTF.exists():
        raise SystemExit(f"source font missing: {TTF}")
    cjk = scan_cjk_from_sources()
    print(f"charset: {len(cjk)} CJK codepoints from main/*.c")
    for size in SIZES:
        name = FONT_NAMES[size]
        path = FONT_DIR / f"{name}.c"
        line_height, base_line = parse_old_metrics(path)
        gen_font(size, name, cjk, line_height, base_line)
    print("done")


if __name__ == "__main__":
    main()
