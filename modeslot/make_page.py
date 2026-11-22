"""Draw the slot's fallback page: a 1280 x 800 PNG (the whole screen on a Live III), shown while no app draws the page.

usage: python3 make_page.py OUT.png [TITLE] [LINE ...]
       python3 make_page.py --icon OUT.png [LOGO.png]   (the 70 x 70 MENU icon, like MPC's: the Open MPC logo)

MPC shows it while the slot is the current mode (modeslot hook 4), scaled to fit the page. Any 1280 x 800 PNG
works; this one is only a plain test card.
"""
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 1280, 800
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def icon(out, logo=None):
    """The MENU icon: the Open MPC logo (a 512 px square PNG on white) at MPC's icon size, its white corners made
    transparent; without a logo, a plain "OPEN MPC" tile."""
    img = Image.new("RGBA", (70, 70), (0, 0, 0, 0))
    if logo:
        src = Image.open(logo).convert("RGB")
        for corner in ((0, 0), (src.width - 1, 0), (0, src.height - 1), (src.width - 1, src.height - 1)):
            ImageDraw.floodfill(src, corner, (255, 0, 255), thresh=60)   # the white around the rounded square
        rgba = src.convert("RGBA")
        rgba.putdata([(0, 0, 0, 0) if p[:3] == (255, 0, 255) else p for p in list(rgba.getdata())])
        mark = rgba.convert("RGBa").resize((64, 64), Image.LANCZOS).convert("RGBA")   # premultiplied: no white fringe
        img.alpha_composite(mark, (3, 3))
    else:
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([4, 4, 65, 65], radius=10, fill=(232, 72, 52, 255))
        f = ImageFont.truetype(BOLD, 17)
        for text, y in (("OPEN", 15), ("MPC", 35)):
            d.text(((70 - d.textlength(text, font=f)) / 2, y), text, font=f, fill=(255, 255, 255, 255))
    img.save(out, optimize=True)
    print(out, 70, 70)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    if sys.argv[1] == "--icon":
        return icon(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
    out = sys.argv[1]
    title = sys.argv[2] if len(sys.argv) > 2 else "PLUGIN MANAGER"
    lines = sys.argv[3:] or ["Waiting for the Plugin Manager's background server.",
                             "Turn it on in the plugin's SETTINGS tab: Background server.",
                             "Press Main or Menu to leave."]

    img = Image.new("RGB", (W, H), (18, 20, 24))
    d = ImageDraw.Draw(img)
    big = ImageFont.truetype(BOLD, 96)
    small = ImageFont.truetype(FONT, 36)
    accent = (232, 72, 52)

    d.rectangle([0, 0, W, 10], fill=accent)
    tw = d.textlength(title, font=big)
    d.text(((W - tw) / 2, 250), title, font=big, fill=(240, 240, 240))
    d.line([(W / 2 - 280, 390), (W / 2 + 280, 390)], fill=accent, width=4)
    y = 430
    for line in lines:
        lw = d.textlength(line, font=small)
        d.text(((W - lw) / 2, y), line, font=small, fill=(170, 176, 186))
        y += 56
    img.save(out, optimize=True)
    print(out, W, H)


if __name__ == "__main__":
    main()
