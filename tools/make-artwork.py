#!/usr/bin/env python3
"""Generates the app artwork (menu logo, channel icon, TV and GamePad splashes).

The look follows dborth's GX apps: slanted, white-outlined lettering that is grey
with a gold accent, on near-black. The text is rendered from a system font, so
the output is plain raster art with no font dependency at run time.

    python3 tools/make-artwork.py

Needs Pillow. Looks for Arial Black / Arial Bold Italic (macOS), then DejaVu.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_META = os.path.join(ROOT, "wiiu", "meta")
OUT_IMG = os.path.join(ROOT, "wiiu", "data", "images_hd")

HEAVY = [
    "/System/Library/Fonts/Supplemental/Arial Black.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
]
BOLD_ITALIC = [
    "/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf",
]

GREY_TOP, GREY_BOTTOM = (232, 232, 232), (150, 150, 150)
GOLD_TOP, GOLD_BOTTOM = (255, 226, 92), (222, 160, 0)
OUTLINE = (255, 255, 255)
BACKGROUND = (10, 12, 12)
DOTS = [(36, 112, 220), (34, 160, 70), (240, 200, 30), (215, 50, 40)]  # blue, green, yellow, red
SHEAR = 0.22
SS = 4  # supersampling factor


def font(paths, size):
    for p in paths:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    sys.exit("no usable font found, edit HEAVY / BOLD_ITALIC in this script")


def gradient(size, top, bottom):
    w, h = size
    img = Image.new("RGB", size)
    px = img.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return img


def slanted_word(text, fnt, top, bottom, stroke):
    """One word, slanted, with a gradient fill and a white outline, on transparency."""
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    box = probe.textbbox((0, 0), text, font=fnt, stroke_width=stroke)
    w, h = box[2] - box[0] + 2 * stroke, box[3] - box[1] + 2 * stroke
    pad = int(h * SHEAR) + stroke

    mask = Image.new("L", (w + pad, h), 0)
    ImageDraw.Draw(mask).text((stroke - box[0], stroke - box[1]), text, font=fnt, fill=255)
    outline = Image.new("L", mask.size, 0)
    ImageDraw.Draw(outline).text((stroke - box[0], stroke - box[1]), text, font=fnt, fill=255,
                                 stroke_width=stroke, stroke_fill=255)

    def shear(m):
        # lean the top to the right: x' = x + SHEAR * (h - y)
        return m.transform(m.size, Image.AFFINE, (1, SHEAR, -SHEAR * h, 0, 1, 0), Image.BICUBIC)

    mask, outline = shear(mask), shear(outline)
    layer = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    layer.paste(Image.new("RGBA", mask.size, OUTLINE + (255,)), (0, 0), outline)
    layer.paste(gradient(mask.size, top, bottom).convert("RGBA"), (0, 0), mask)
    return layer


def logo(width, height, brighten=False):
    """not64 in grey with a gold U, four colour dots above, on transparency."""
    big = (width * SS, height * SS)
    canvas = Image.new("RGBA", big, (0, 0, 0, 0))

    size = int(big[1] * 0.50)
    fnt = font(HEAVY, size)
    stroke = max(2, size // 22)
    left = slanted_word("not64", fnt, GREY_TOP, GREY_BOTTOM, stroke)
    right = slanted_word("U", fnt, GOLD_TOP, GOLD_BOTTOM, stroke)

    # the slant leaves empty room to the right of the first word: tuck the U into it
    gap = -(stroke + int(size * 0.22))
    total = left.width + gap + right.width
    x = (big[0] - total) // 2
    y = int(big[1] * 0.38)
    canvas.alpha_composite(left, (x, y))
    canvas.alpha_composite(right, (x + left.width + gap, y))

    d = ImageDraw.Draw(canvas)
    r = int(big[1] * 0.055)
    cx = big[0] // 2 - int(r * 5.4)
    for i, c in enumerate(DOTS):
        cxi = cx + int(i * r * 3.6)
        cyi = int(big[1] * 0.20) + (r if i in (1, 2) else 0)
        d.ellipse((cxi - r - 3, cyi - r - 3, cxi + r + 3, cyi + r + 3), fill=OUTLINE + (255,))
        d.ellipse((cxi - r, cyi - r, cxi + r, cyi + r), fill=c + (255,))

    out = canvas.resize((width, height), Image.LANCZOS)
    if brighten:
        px = out.load()
        for yy in range(height):
            for xx in range(width):
                r_, g_, b_, a_ = px[xx, yy]
                px[xx, yy] = (min(255, r_ + 28), min(255, g_ + 28), min(255, b_ + 28), a_)
    return out


def splash(width, height, subtitle_size, caption=True):
    img = Image.new("RGBA", (width, height), BACKGROUND + (255,))
    # a soft lighter vignette in the middle, like the dborth splashes
    glow = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gd.ellipse((width * 0.15, height * 0.15, width * 0.85, height * 0.85), fill=(48, 52, 52, 150))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(width * 0.07)))

    lw = int(width * 0.82)
    lh = int(lw * 117 / 352)
    img.alpha_composite(logo(lw, lh), ((width - lw) // 2, int(height * 0.14)))

    d = ImageDraw.Draw(img)
    sub = font(BOLD_ITALIC, subtitle_size)
    text = "NINTENDO 64 EMULATOR FOR WII U"
    tw = d.textlength(text, font=sub)
    d.text(((width - tw) / 2, int(height * 0.66)), text, font=sub, fill=(240, 240, 240, 255))
    if caption:
        cap = font(BOLD_ITALIC, int(subtitle_size * 0.9))
        d.text(((width - d.textlength("Loading...", font=cap)) / 2, int(height * 0.84)),
               "Loading...", font=cap, fill=(200, 200, 200, 255))
    return img


def icon():
    img = Image.new("RGBA", (128, 128), BACKGROUND + (255,))
    big = logo(112, 112)
    img.alpha_composite(big, (8, 8))
    return img.convert("RGB")


def console():
    """The Nintendo 64 seen from the front, for the game list when no game is picked."""
    W, H = 300, 240
    k = SS
    img = Image.new("RGBA", (W * k, H * k), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def box(x0, y0, x1, y1):
        return [x0 * k, y0 * k, x1 * k, y1 * k]

    def vgrad(rect, top, bottom, radius):
        x0, y0, x1, y1 = [int(v * k) for v in rect]
        g = gradient((x1 - x0, y1 - y0), top, bottom).convert("RGBA")
        m = Image.new("L", g.size, 0)
        ImageDraw.Draw(m).rounded_rectangle([0, 0, g.size[0] - 1, g.size[1] - 1], radius=radius * k, fill=255)
        img.paste(g, (x0, y0), m)

    # soft shadow under the console
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse(box(26, 188, 274, 226), fill=(0, 0, 0, 90))
    img.alpha_composite(sh.filter(ImageFilter.GaussianBlur(7 * k)))
    d = ImageDraw.Draw(img)

    # cartridge sticking out of the slot (green label)
    vgrad((112, 18, 188, 74), (150, 150, 156), (104, 104, 110), 6)
    d.rounded_rectangle(box(120, 28, 180, 56), radius=3 * k, fill=(16, 118, 56))
    d.rectangle(box(128, 36, 172, 40), fill=(255, 255, 255, 220))
    d.rectangle(box(128, 45, 160, 48), fill=(255, 226, 92))
    # top deck with the cartridge slot
    vgrad((26, 62, 274, 106), (112, 112, 118), (78, 78, 84), 10)
    d.rounded_rectangle(box(104, 62, 196, 74), radius=3 * k, fill=(22, 22, 24))
    # front face
    vgrad((22, 100, 278, 200), (76, 76, 82), (40, 40, 44), 12)
    # power switch and reset button
    d.rounded_rectangle(box(40, 112, 70, 128), radius=4 * k, fill=(198, 198, 204))
    d.rectangle(box(48, 117, 62, 123), fill=(110, 110, 116))
    d.ellipse(box(238, 111, 258, 131), fill=(198, 198, 204))
    d.ellipse(box(243, 116, 253, 126), fill=(128, 128, 134))
    # four controller ports with the player colors above them
    for cx, col in zip((78, 126, 174, 222), DOTS):
        d.ellipse(box(cx - 4, 142, cx + 4, 150), fill=col)
        d.rounded_rectangle(box(cx - 17, 156, cx + 17, 184), radius=5 * k, fill=(20, 20, 22))
        d.rounded_rectangle(box(cx - 12, 161, cx + 12, 178), radius=3 * k, fill=(46, 46, 50))
    return img.resize((W, H), Image.LANCZOS)


def main():
    os.makedirs(OUT_META, exist_ok=True)
    os.makedirs(OUT_IMG, exist_ok=True)
    logo(352, 117).save(os.path.join(OUT_IMG, "logo.png"))
    logo(352, 117, brighten=True).save(os.path.join(OUT_IMG, "logo_over.png"))
    console().save(os.path.join(OUT_IMG, "default_preview.png"))
    icon().save(os.path.join(OUT_META, "icon.png"))
    splash(1280, 720, 30).save(os.path.join(OUT_META, "tv_splash.png"))
    splash(854, 480, 20).save(os.path.join(OUT_META, "drc_splash.png"))
    print("artwork written to", OUT_META, "and", OUT_IMG)


if __name__ == "__main__":
    main()
