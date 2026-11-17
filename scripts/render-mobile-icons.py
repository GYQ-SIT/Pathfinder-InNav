"""Renders the phone app's icons (PNG) from the SVG files in InNavi/Content/ImageAssets/Mobile/Source.

The SVGs are the design team's line icons (Lucide, ISC licence). Each is drawn white on a transparent background, 128 x 128 px, so the app can
tint it to any colour at run time (the light and the night theme, a blue open tab). The PNG of each icon is written next to the sources as
Mobile_<Name>.png in InNavi/Content/ImageAssets/Mobile; it is the source of the texture /Game/ImageAssets/Mobile/Mobile_<Name>, which the
app loads by that name (see MobileIcons.h). After running this, import or reimport the changed textures in the Unreal editor, and set each to
Compression "Uncompressed (RGBA8)" and Mip Gen "NoMipmaps" so the transparent edges stay clean.

How it renders: Microsoft Edge (headless) draws each SVG on black, and the transparency is worked out from how bright each pixel is. The one
icon that keeps its own colours (CompassColour) is drawn on black and on white, which gives both its colour and its transparency.
Needs Python with Pillow, and Edge (it is on every Windows 11 PC). Run it from PowerShell (a bash started by an editor can stop Edge from
writing its picture).

    py scripts/render-mobile-icons.py            # all icons
    py scripts/render-mobile-icons.py Search     # only these (names without Mobile_ and .png)
"""

import base64
import os
import re
import subprocess
import sys
import tempfile
import time

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOBILE = os.path.join(ROOT, "InNavi", "Content", "ImageAssets", "Mobile")
SOURCES = os.path.join(MOBILE, "Source")
SIZE = 128
COLUMNS = 6

# Icon name -> source SVG (in Source). CompassColour keeps its own colours; every other icon is white.
KEEP_COLOURS = {"CompassColour"}


def find_edge():
    for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles")):
        if base:
            path = os.path.join(base, "Microsoft", "Edge", "Application", "msedge.exe")
            if os.path.exists(path):
                return path
    sys.exit("Microsoft Edge was not found.")


def recolour_white(svg):
    """Every stroke and fill that is a colour (not 'none') becomes white."""
    return re.sub(r'(stroke|fill)="(?!none)[^"]*"', lambda m: '%s="#ffffff"' % m.group(1), svg)


def render_sheet(svgs, background):
    """Draws each SVG in a cell on a page of one background colour and returns the picture as a PIL image."""
    rows = (len(svgs) + COLUMNS - 1) // COLUMNS
    cells = []
    for svg in svgs:
        data = base64.b64encode(svg.encode("utf-8")).decode("ascii")
        cells.append('<img src="data:image/svg+xml;base64,%s" width="%d" height="%d">' % (data, SIZE, SIZE))
    html = (
        "<!doctype html><meta charset=utf-8><style>html,body{margin:0;background:%s}"
        "#g{display:grid;grid-template-columns:repeat(%d,%dpx)}img{display:block}</style><div id=g>%s</div>"
        % (background, COLUMNS, SIZE, "".join(cells))
    )
    with tempfile.TemporaryDirectory() as tmp:
        page = os.path.join(tmp, "icons.html")
        shot = os.path.join(tmp, "icons.png")
        with open(page, "w", encoding="utf-8") as f:
            f.write(html)
        subprocess.run(
            [find_edge(), "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
             "--user-data-dir=" + os.path.join(tmp, "profile"),  # its own profile, so an Edge that is open does not take the job
             "--window-size=%d,%d" % (COLUMNS * SIZE, rows * SIZE + 200), "--screenshot=" + shot,
             "file:///" + page.replace("\\", "/")],
            check=True, capture_output=True, timeout=120)
        # Edge's first process can return before the picture is written.
        for _ in range(120):
            if os.path.exists(shot) and os.path.getsize(shot) > 0:
                time.sleep(0.3)
                break
            time.sleep(0.5)
        else:
            sys.exit("Edge did not write the picture.")
        return Image.open(shot).convert("RGB").copy()


def cell(sheet, index):
    x, y = (index % COLUMNS) * SIZE, (index // COLUMNS) * SIZE
    return sheet.crop((x, y, x + SIZE, y + SIZE))


def main():
    available = sorted(f[:-4] for f in os.listdir(SOURCES) if f.endswith(".svg"))
    names = sys.argv[1:] or available
    unknown = [n for n in names if n not in available]
    if unknown:
        sys.exit("No source SVG for: %s" % ", ".join(unknown))

    svgs = []
    for name in names:
        with open(os.path.join(SOURCES, name + ".svg"), "r", encoding="utf-8") as f:
            svg = f.read()
        if "<script" in svg.lower():
            sys.exit("%s.svg contains a script." % name)
        svgs.append(svg if name in KEEP_COLOURS else recolour_white(svg))

    on_black = render_sheet(svgs, "#000")
    keep_index = [i for i, n in enumerate(names) if n in KEEP_COLOURS]
    on_white = render_sheet(svgs, "#fff") if keep_index else None

    for i, name in enumerate(names):
        black = cell(on_black, i)
        if name in KEEP_COLOURS:
            white = cell(on_white, i)
            # On black a pixel is alpha * colour; on white it is alpha * colour + (1 - alpha) * 255. So alpha = 1 - (white - black) / 255.
            out = Image.new("RGBA", (SIZE, SIZE))
            bp, wp, op = black.load(), white.load(), out.load()
            for y in range(SIZE):
                for x in range(SIZE):
                    alpha = 1.0 - (sum(wp[x, y]) - sum(bp[x, y])) / (3 * 255.0)
                    alpha = min(1.0, max(0.0, alpha))
                    if alpha <= 0.003:
                        op[x, y] = (0, 0, 0, 0)
                    else:
                        op[x, y] = tuple(min(255, round(c / alpha)) for c in bp[x, y]) + (round(alpha * 255),)
        else:
            # White on black: the alpha is how bright the pixel is.
            alpha = black.getchannel("G")
            out = Image.new("RGBA", (SIZE, SIZE), (255, 255, 255, 0))
            out.putalpha(alpha)
        out.save(os.path.join(MOBILE, "Mobile_%s.png" % name))
        print("wrote Mobile_%s.png" % name)


if __name__ == "__main__":
    main()
