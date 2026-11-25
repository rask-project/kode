#!/usr/bin/env python3
# Draws the layers of Tekst's app icon (assets/tekst.icon, an Icon Composer
# document) and the legacy icon files, assets/tekst.svg (the whole icon on
# the macOS grid), tekst.png and tekst.icns (tools/render-icon.swift and
# iconutil), in the style of Konsoll's (../konsoll/tools/make-app-icons.py):
# a silver ring (the system's rounded square), a border and a sheet with
# chamfered top-left and bottom-right corners. Where Konsoll shows ">_",
# Tekst shows indented, syntax-coloured lines of code ending at the
# insertion point (the I-beam caret): text and code editing in one mark.
#
#   python3 tools/make-app-icons.py                  # writes assets/tekst.icon, .svg, .png, .icns
#   python3 tools/make-app-icons.py --preview DIR    # composite previews of every palette
#   ICON_PALETTE=polar python3 tools/make-app-icons.py
import os, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Same measurements as Konsoll's icon (its original is 1254 px; the border
# spans x 85-1170, y 197-1013 with 160 px chamfers and 35 px corners; the
# inner area is inset 42 px with 120 px chamfers), scaled to WIDTH on the
# 1024 canvas, so both icons sit side by side in the Dock.
WIDTH = float(os.environ.get("ICON_WIDTH", 840))
SCALE = WIDTH / 1085
ORIGIN_X = (1024 - WIDTH) / 2
ORIGIN_Y = (1024 - 816 * SCALE) / 2

# Token colours: keyword, name (plain identifiers and punctuation), string,
# number; then the caret.
PALETTES = {
    # Natt (night): a blue-black sheet, cooler and bluer than Konsoll's
    # graphite, with bright tokens.
    "natt": dict(border="#0A0F1A", area=("#1C2538", "#141B2A"),
                 keyword="#6CB6FF", name="#E6EDF3", string="#7EE2A8", number="#FFB86B", caret="#6CB6FF"),
    # Polar: a cool white sheet with a navy border, light-theme tokens.
    "polar": dict(border="#1D2740", area=("#FFFFFF", "#E8EDF4"),
                  keyword="#0B57D0", name="#1D2740", string="#1A7F37", number="#C4432B", caret="#0B57D0"),
    # Fjord: a deep blue sheet.
    "fjord": dict(border="#0E2238", area=("#1F4E79", "#173C5E"),
                  keyword="#9FD8FF", name="#FFFFFF", string="#9BF0C0", number="#FFD08A", caret="#7FD1FF"),
    # Aurora: a dark teal sheet, the northern lights' greens and violet.
    "aurora": dict(border="#06201F", area=("#0F3A3D", "#0B2B2E"),
                   keyword="#B49BFF", name="#E8FFF8", string="#5EF2B5", number="#7FE3FF", caret="#5EF2B5"),
}


def point(x, y):
    """A point in Konsoll's original icon coordinates, on the 1024 canvas."""
    return ORIGIN_X + (x - 85) * SCALE, ORIGIN_Y + (y - 197) * SCALE


def sheet(left, top, right, bottom, chamfer, radius):
    """Rounded top-right and bottom-left corners, chamfered top-left and
    bottom-right ones (in original pixels)."""
    x0, y0 = point(left, top)
    x1, y1 = point(right, bottom)
    c, r = chamfer * SCALE, radius * SCALE
    return (f"M{x0 + c:.1f} {y0:.1f} L{x1 - r:.1f} {y0:.1f} A{r:.1f} {r:.1f} 0 0 1 {x1:.1f} {y0 + r:.1f} "
            f"L{x1:.1f} {y1 - c:.1f} L{x1 - c:.1f} {y1:.1f} L{x0 + r:.1f} {y1:.1f} "
            f"A{r:.1f} {r:.1f} 0 0 1 {x0:.1f} {y1 - r:.1f} L{x0:.1f} {y0 + c:.1f} Z")


def bar(x0, y0, x1, y1):
    """A rounded bar (a line of text) from original-pixel corners."""
    ax, ay = point(x0, y0)
    bx, by = point(x1, y1)
    h = by - ay
    return f'<rect x="{ax:.1f}" y="{ay:.1f}" width="{bx - ax:.1f}" height="{h:.1f}" rx="{h / 2:.1f}"/>'


def layer(body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" '
            f'viewBox="0 0 1024 1024">\n{body}\n</svg>\n')


def body(svg):
    return svg.split("\n", 1)[1].rsplit("</svg>", 1)[0]


def caret(x, top_y, bot_y, stem=40, serif=130, serif_h=34):
    """The I-beam insertion point: a stem with a serif at each end."""
    return "\n    ".join([
        bar(x - stem / 2, top_y, x + stem / 2, bot_y),
        bar(x - serif / 2, top_y, x + serif / 2, top_y + serif_h),
        bar(x - serif / 2, bot_y - serif_h, x + serif / 2, bot_y),
    ])


def glyph_code(p):
    """Four lines of code from where Konsoll's ">_" starts (x 250, y 340),
    each a run of coloured tokens, the middle ones indented one level; the
    caret stands at the end of the third. The bars are a bit thicker than
    Konsoll's "_" (37 px) so they hold up at small sizes."""
    h, step, gap, indent = 50, 112, 28, 110
    rows = [
        (0, [("keyword", 150), ("name", 260)]),                    # fn name
        (1, [("keyword", 120), ("name", 150), ("string", 280)]),   #   let x = "…"
        (1, [("name", 220), ("number", 130)]),                     #   call(42)|
        (0, [("name", 110)]),                                      # end
    ]
    caret_row = 2
    groups = {}
    for i, (level, tokens) in enumerate(rows):
        y = 340 + i * step
        x = 250 + level * indent
        for kind, width in tokens:
            groups.setdefault(kind, []).append(bar(x, y, x + width, y + h))
            x += width + gap
        if i == caret_row:
            end_x, caret_y = x - gap, y
    out = [f'  <g fill="{p[kind]}">\n    ' + "\n    ".join(bars) + "\n  </g>" for kind, bars in groups.items()]
    out.append(f'  <g fill="{p["caret"]}">\n    {caret(end_x + 74, caret_y - 34, caret_y + h + 34, stem=30, serif=96, serif_h=26)}\n  </g>')
    return "\n".join(out)



def layers(p):
    silver = layer('''  <defs><linearGradient id="s" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#DCE8EF"/><stop offset="1" stop-color="#9BA1A8"/>
  </linearGradient></defs>
  <rect width="1024" height="1024" fill="url(#s)"/>''')
    border = layer(f'  <path d="{sheet(85, 197, 1170, 1013, 160, 35)}" fill="{p["border"]}"/>')
    top, bottom = p["area"]
    area = layer(f'''  <defs><linearGradient id="a" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="{top}"/><stop offset="1" stop-color="{bottom}"/>
  </linearGradient></defs>
  <path d="{sheet(127, 240, 1130, 975, 120, 30)}" fill="url(#a)"/>''')
    text = layer(glyph_code(p))
    return dict(silver=silver, border=border, area=area, text=text)


def composite(p, grid=False):
    """The layers stacked and masked to the system's rounded square. With
    `grid`, laid out on the macOS icon grid (an 824 px body at 100 px, 185 px
    corners), as an .icns expects; otherwise full bleed, as a preview."""
    l = layers(p)
    stack = f'''  <defs><clipPath id="mask"><rect x="0" y="0" width="1024" height="1024" rx="230"/></clipPath></defs>
  <g clip-path="url(#mask)">
  {body(l["silver"])}
  {body(l["border"])}
  {body(l["area"])}
  {body(l["text"])}
  </g>'''
    if grid:
        stack = f'<g transform="translate(100 100) scale({824 / 1024})">\n{stack}\n</g>'
    return layer(stack)


if len(sys.argv) == 3 and sys.argv[1] == "--preview":
    out = sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for name, p in PALETTES.items():
        open(f"{out}/tekst-{name}.svg", "w").write(composite(p))
    sys.exit(0)

palette = PALETTES[os.environ.get("ICON_PALETTE", "natt")]
assets = f"{root}/assets"
d = f"{assets}/tekst.icon/Assets"
os.makedirs(d, exist_ok=True)
for name, svg in layers(palette).items():
    open(f"{d}/{name}.svg", "w").write(svg)
open(f"{assets}/tekst.svg", "w").write(composite(palette, grid=True))

# The PNG and the .icns, rendered from tekst.svg (macOS: swift, iconutil).
import shutil, subprocess, tempfile
if shutil.which("swift") and shutil.which("iconutil"):
    with tempfile.TemporaryDirectory() as work:
        iconset = f"{work}/tekst.iconset"
        subprocess.run(["swift", f"{root}/tools/render-icon.swift", f"{assets}/tekst.svg",
                        f"{assets}/tekst.png", iconset], check=True)
        subprocess.run(["iconutil", "-c", "icns", iconset, "-o", f"{assets}/tekst.icns"], check=True)
else:
    print("swift/iconutil not found: tekst.png and tekst.icns not updated", file=sys.stderr)
