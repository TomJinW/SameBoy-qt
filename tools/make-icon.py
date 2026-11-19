#!/usr/bin/env python3
"""Draws the SameBoy Qt application icon (original artwork) and writes every size the three platforms need.

Usage: python3 tools/make-icon.py [PREVIEW_DIR]   (needs Pillow; macOS also needs iconutil for the .icns)
Outputs in resources/icons/: sameboy-qt.png (256, embedded in the app), sameboy-qt.ico (Windows),
sameboy-qt.icns (macOS, only when iconutil is available), linux/<size>.png (Linux hicolor sizes).
"""
import math, os, random, shutil, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageFilter

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "icons")
S = 4  # supersampling factor
N = 1024

def pixel_scene():
    """The 160x144 scene shown on the icon's screen (a dusk landscape with a small hero)."""
    random.seed(7)
    W, H = 160, 144
    im = Image.new("RGB", (W, H)); d = ImageDraw.Draw(im)
    sky = [(24,16,64),(40,24,88),(72,32,112),(120,48,128),(184,72,120),(232,112,104),(248,168,104),(252,216,128)]
    band = 88 // len(sky) + 1
    for y in range(0, 92):
        i = min(y // band, len(sky) - 1)
        for x in range(W):
            c = sky[i]
            if i + 1 < len(sky) and (y % band) >= band - 4 and (x + y) % 2 == 0: c = sky[i + 1]
            im.putpixel((x, y), c)
    d.ellipse((98,56,126,84), fill=(255,236,150)); d.ellipse((102,60,122,80), fill=(255,248,200))
    def mountain(cx, base, w, h, col):
        for dx in range(-w, w + 1):
            d.line((cx + dx, base - int(h * (1 - abs(dx) / w)), cx + dx, base), fill=col)
    mountain(30,92,40,34,(88,56,128)); mountain(88,92,46,28,(104,64,136)); mountain(140,92,34,30,(88,56,128))
    d.rectangle((0,92,W,112), fill=(72,52,112))
    d.rectangle((60,70,86,92), fill=(56,40,96))
    for tx in (60,70,80):
        d.rectangle((tx,62,tx+6,70), fill=(56,40,96)); d.polygon([(tx-1,62),(tx+3,55),(tx+7,62)], fill=(184,56,72))
    d.rectangle((70,80,76,92), fill=(24,16,48))
    for x in range(W):
        d.line((x, int(100+6*math.sin(x/17.0)+3*math.sin(x/7.0)), x, H), fill=(48,128,72))
        d.line((x, int(112+5*math.sin(x/13.0+2)+2*math.sin(x/5.0)), x, H), fill=(32,104,64))
    d.rectangle((0,120,W,H), fill=(24,80,56))
    for y in range(100,125): d.line((74-(y-100)//2, y, 92+(y-100), y), fill=(216,176,112))
    hx, hy = 76, 106
    d.ellipse((hx-6,hy+14,hx+8,hy+17), fill=(16,56,40)); d.rectangle((hx-3,hy+8,hx+5,hy+15), fill=(72,96,200))
    d.ellipse((hx-6,hy-2,hx+8,hy+10), fill=(255,208,160)); d.rectangle((hx-6,hy-3,hx+8,hy+2), fill=(200,72,56))
    d.rectangle((hx-1,hy-6,hx+3,hy-3), fill=(255,224,96)); d.point((hx-2,hy+5), fill=(24,16,40)); d.point((hx+4,hy+5), fill=(24,16,40))
    d.rectangle((hx+8,hy+2,hx+9,hy+14), fill=(232,240,255)); d.rectangle((hx+7,hy+12,hx+10,hy+13), fill=(255,224,96))
    return im

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def artwork(size, pad):
    """Square icon artwork; `pad` is the transparent margin (fraction of the canvas) around the rounded body."""
    s = size * S
    canvas = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    body = int(s * (1 - 2 * pad)); off = (s - body) // 2
    k = body / 1024.0
    def P(v): return int(v * k)
    # rounded body with a dark-green radial gradient
    grad = Image.new("RGB", (body, body)); g = ImageDraw.Draw(grad)
    cx, cy, rad = body * 0.5, body * 0.42, body * 0.75
    for r in range(int(rad), 0, -4):
        t = r / rad; g.ellipse((cx - r, cy - r, cx + r, cy + r), fill=lerp((60, 110, 40), (15, 56, 15), t))
    mask = Image.new("L", (body, body), 0); ImageDraw.Draw(mask).rounded_rectangle((0, 0, body - 1, body - 1), P(228), fill=255)
    canvas.paste(grad.convert("RGBA"), (off, off), mask)
    # console drawn in 1024-space on its own layer, then composited
    layer = Image.new("RGBA", (body, body), (0, 0, 0, 0)); d = ImageDraw.Draw(layer)
    cream, edge, dark = (242, 247, 221), (190, 205, 150), (31, 79, 31)
    L, T, R, B = 170, 96, 854, 930
    sh = Image.new("RGBA", (body, body), (0, 0, 0, 0)); ImageDraw.Draw(sh).rounded_rectangle((P(L+10), P(T+24), P(R+10), P(B+24)), P(48), fill=(0, 0, 0, 120))
    layer = Image.alpha_composite(layer, sh.filter(ImageFilter.GaussianBlur(P(22)))); d = ImageDraw.Draw(layer)
    d.rounded_rectangle((P(L), P(T), P(R), P(B)), P(48), fill=edge)
    d.rounded_rectangle((P(L+8), P(T+8), P(R-8), P(B-8)), P(42), fill=cream)
    # screen bezel and screen
    d.rounded_rectangle((P(212), P(148), P(812), P(676)), P(34), fill=dark)
    sx, sy, sw, sh_ = 272, 196, 480, 432
    scene = pixel_scene().resize((P(sw), P(sh_)), Image.NEAREST)
    layer.paste(scene, (P(sx), P(sy)))
    d.rectangle((P(sx), P(sy), P(sx+sw), P(sy+sh_)), outline=(15, 56, 15), width=max(1, P(5)))
    # d-pad
    dp = (330, 800); t, a = 52, 112
    d.rounded_rectangle((P(dp[0]-t//2), P(dp[1]-a//2), P(dp[0]+t//2), P(dp[1]+a//2)), P(14), fill=dark)
    d.rounded_rectangle((P(dp[0]-a//2), P(dp[1]-t//2), P(dp[0]+a//2), P(dp[1]+t//2)), P(14), fill=dark)
    # A / B buttons
    for bx, by in ((700, 770), (610, 830)):
        d.ellipse((P(bx-42), P(by-42), P(bx+42), P(by+42)), fill=(196, 60, 96))
        d.ellipse((P(bx-30), P(by-34), P(bx+26), P(by+14)), fill=(230, 110, 140))
    # speaker slits
    for i in range(4):
        x0 = 712 + i * 28
        d.rounded_rectangle((P(x0), P(870), P(x0 + 12), P(918)), P(6), fill=edge)
    canvas.alpha_composite(layer, (off, off))
    return canvas.resize((size, size), Image.LANCZOS)

def main():
    os.makedirs(os.path.join(OUT, "linux"), exist_ok=True)
    full = artwork(N, 0.03)    # Windows / Linux: nearly full-bleed
    mac = artwork(N, 0.098)    # macOS: Apple's 824/1024 body with room for the shadow
    # soft drop shadow for the macOS rendition
    sh = Image.new("RGBA", (N, N), (0, 0, 0, 0)); m = mac.split()[3].filter(ImageFilter.GaussianBlur(14))
    sh.paste((0, 0, 0, 90), (0, 12), m); mac = Image.alpha_composite(sh, mac)
    full.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "sameboy-qt.png"))
    for n in (16, 32, 48, 64, 128, 256, 512):
        full.resize((n, n), Image.LANCZOS).save(os.path.join(OUT, "linux", f"{n}.png"))
    full.save(os.path.join(OUT, "sameboy-qt.ico"), sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])
    if shutil.which("iconutil"):
        with tempfile.TemporaryDirectory() as tmp:
            iconset = os.path.join(tmp, "sameboy-qt.iconset"); os.makedirs(iconset)
            for n in (16, 32, 128, 256, 512):
                mac.resize((n, n), Image.LANCZOS).save(os.path.join(iconset, f"icon_{n}x{n}.png"))
                mac.resize((n * 2, n * 2), Image.LANCZOS).save(os.path.join(iconset, f"icon_{n}x{n}@2x.png"))
            subprocess.run(["iconutil", "-c", "icns", iconset, "-o", os.path.join(OUT, "sameboy-qt.icns")], check=True)
    else:
        print("iconutil not found: sameboy-qt.icns was not regenerated", file=sys.stderr)
    if len(sys.argv) > 1:  # optional: also write previews of both renditions into the given folder
        full.save(os.path.join(sys.argv[1], "preview-full.png")); mac.save(os.path.join(sys.argv[1], "preview-mac.png"))

main()
