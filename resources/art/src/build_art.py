#!/usr/bin/env python3
"""Generate JamesGames' illustrations as SVG and rasterize them to PNG.

Original art in a soft painted style: hand-cut edges, pooled pigment and fine
paper grain, in the Meadow palette (sage, olive, dusty rose, apricot, cream).
Everything is procedural and seeded, so a re-run gives identical output.

    python3 resources/art/src/build_art.py        # writes resources/art/*.png

Needs rsvg-convert (librsvg): Qt's SVG renderer has no filter support, so the
grain and rough edges have to be baked into PNGs here.
"""
import math
import os
import random
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.dirname(HERE)

PALETTES = {
    "light": {
        "sky_rose": "#E9C9BF", "rose": "#C99A90", "olive": "#8C9462", "sage": "#6F8E74",
        "forest": "#4B6655", "apricot": "#F6B26B", "sun": "#FCC58C", "cream": "#F4EEE3", "sky": "#F7E2CF",
        "petal": "#D99A94", "petal_hi": "#F2C2B8", "bird": "#B98F88", "bird_belly": "#EAC9A0",
    },
    # Dusk: the same meadow after sunset - muted, deeper, never black.
    "dark": {
        "sky_rose": "#4A3A39", "rose": "#5C4643", "olive": "#3F4633", "sage": "#35473B",
        "forest": "#26342B", "apricot": "#C9895A", "sun": "#8A6048", "cream": "#2A2622", "sky": "#3B302C",
        "petal": "#A87873", "petal_hi": "#C29790", "bird": "#8C6D68", "bird_belly": "#B89A7A",
    },
}

FILTERS = """
  <filter id="paint" x="-5%" y="-5%" width="110%" height="110%" color-interpolation-filters="sRGB">
    <feTurbulence type="fractalNoise" baseFrequency="0.018" numOctaves="3" seed="3" result="mottle"/>
    <feColorMatrix in="mottle" type="matrix" values="0 0 0 0 0.5  0 0 0 0 0.47  0 0 0 0 0.44  0 0 0 -0.9 0.62" result="mottleA"/>
    <feComposite in="mottleA" in2="SourceAlpha" operator="in" result="mottleIn"/>
    <feBlend in="SourceGraphic" in2="mottleIn" mode="soft-light" result="pooled"/>
    <feTurbulence type="fractalNoise" baseFrequency="0.75" numOctaves="2" seed="7" result="noise"/>
    <feColorMatrix in="noise" type="matrix" values="0 0 0 0 0.55  0 0 0 0 0.52  0 0 0 0 0.48  0 0 0 -0.7 0.42" result="speck"/>
    <feComposite in="speck" in2="SourceAlpha" operator="in" result="grain"/>
    <feBlend in="pooled" in2="grain" mode="multiply"/>
  </filter>
  <filter id="rough" x="-5%" y="-5%" width="110%" height="110%">
    <feTurbulence type="fractalNoise" baseFrequency="0.03" numOctaves="2" seed="4" result="w"/>
    <feDisplacementMap in="SourceGraphic" in2="w" scale="6" xChannelSelector="R" yChannelSelector="G"/>
  </filter>
"""


def ridge(rng, width, base, amp, bumps, height):
    """A closed rolling-hill shape: smooth random ridge line, filled to the bottom."""
    xs = [width * i / bumps for i in range(bumps + 1)]
    # alternate crests and dips so it rolls instead of wobbling
    ys = [base - amp * ((0.55 + 0.45 * rng.random()) if i % 2 else (0.05 + 0.3 * rng.random()))
          for i, _ in enumerate(xs)]
    d = [f"M -20 {ys[0]:.1f}"]
    for i in range(1, len(xs)):
        x0, x1 = xs[i - 1], xs[i]
        cx = (x0 + x1) / 2
        d.append(f"C {cx:.1f} {ys[i - 1]:.1f}, {cx:.1f} {ys[i]:.1f}, {x1:.1f} {ys[i]:.1f}")
    d.append(f"L {width + 20} {height + 20} L -20 {height + 20} Z")
    return " ".join(d)


def flower(x, y, r, p, rot=0):
    petals = "".join(
        f'<ellipse cx="0" cy="{-r * 0.62:.1f}" rx="{r * 0.42:.1f}" ry="{r * 0.6:.1f}" transform="rotate({a})"/>'
        for a in range(0, 360, 72))
    return (f'<g transform="translate({x:.1f} {y:.1f}) rotate({rot})">'
            f'<g fill="{p["petal"]}">{petals}</g>'
            f'<circle r="{r * 0.28:.1f}" fill="{p["apricot"]}"/></g>')


def grass_tuft(x, y, h, p, rng):
    blades = []
    for _ in range(5):
        dx = rng.uniform(-h * 0.35, h * 0.35)
        bend = rng.uniform(-h * 0.25, h * 0.25)
        blades.append(f'<path d="M {x:.1f} {y:.1f} Q {x + bend:.1f} {y - h * 0.6:.1f} {x + dx:.1f} {y - h:.1f}" '
                      f'stroke="{p["forest"]}" stroke-width="{h * 0.07:.1f}" stroke-linecap="round" fill="none"/>')
    return "".join(blades)


def bird(x, y, s, p, flip=False):
    """A small soaring bird: two swept wings and a plump body, like the reference birds."""
    sx = -1 if flip else 1
    return (f'<g transform="translate({x:.1f} {y:.1f}) scale({sx * s:.3f} {s:.3f})" filter="url(#rough)">'
            f'<path d="M 0 0 C -18 -10, -38 -26, -62 -30 C -44 -14, -26 -2, -6 6 Z" fill="{p["bird"]}"/>'
            f'<path d="M 4 -2 C 20 -18, 40 -40, 70 -48 C 52 -24, 32 -4, 10 8 Z" fill="{p["bird"]}"/>'
            f'<ellipse cx="2" cy="4" rx="16" ry="8" fill="{p["bird_belly"]}" transform="rotate(-12)"/>'
            f'<circle cx="16" cy="0" r="5.5" fill="{p["bird_belly"]}"/>'
            f'<path d="M 21 -1 L 28 1 L 21 3 Z" fill="{p["forest"]}"/></g>')


def svg(width, height, body):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}"><defs>{FILTERS}</defs>{body}</svg>')


def hills(p, seed=12):
    """Wide band for the bottom of the main panel. Top edge fades into the surface."""
    w, h = 2400, 360
    rng = random.Random(seed)
    # (colour, baseline, amplitude, bumps, opacity): far ridges tall and broad,
    # near ridges low and busier, so the layers roll out of step like real hills.
    layers = [
        (p["sky_rose"], 205, 150, 3, 0.6),
        (p["rose"], 255, 125, 4, 0.9),
        (p["olive"], 300, 95, 5, 1.0),
        (p["sage"], 336, 62, 7, 1.0),
    ]
    body = ['<g filter="url(#paint)">']
    for color, base, amp, bumps, op in layers:
        body.append(f'<path d="{ridge(rng, w, base, amp, bumps, h)}" fill="{color}" opacity="{op}" filter="url(#rough)"/>')
    # meadow details on the front ridge
    for _ in range(26):
        x = rng.uniform(20, w - 20)
        body.append(grass_tuft(x, h - rng.uniform(4, 22), rng.uniform(14, 26), p, rng))
    for _ in range(14):
        x = rng.uniform(40, w - 40)
        body.append(flower(x, h - rng.uniform(18, 40), rng.uniform(6, 10), p, rng.uniform(0, 72)))
    body.append("</g>")
    body.append(bird(w * 0.18, 150, 0.55, p))
    body.append(bird(w * 0.215, 176, 0.38, p))
    return svg(w, h, "".join(body))


def leaf(x, y, length, angle, color, width_ratio=0.38):
    """Pointed leaf from (x, y) along `angle` (degrees)."""
    w = length * width_ratio
    return (f'<g transform="translate({x:.1f} {y:.1f}) rotate({angle:.1f})">'
            f'<path d="M 0 0 C {length * 0.3:.1f} {-w:.1f}, {length * 0.75:.1f} {-w * 0.8:.1f}, {length:.1f} 0 '
            f'C {length * 0.75:.1f} {w * 0.8:.1f}, {length * 0.3:.1f} {w:.1f}, 0 0 Z" fill="{color}"/>'
            f'<path d="M 2 0 L {length * 0.85:.1f} 0" stroke="#FFFFFF" stroke-opacity="0.22" stroke-width="{max(1.2, length * 0.03):.1f}" stroke-linecap="round"/>'
            f'</g>')


def stem(rng, x0, y0, x1, y1, p, leaves, flower_r, leaf_len, colors):
    """A curved stem with alternating leaves and a flower (or bud) at the tip."""
    cx, cy = (x0 + x1) / 2 + rng.uniform(-60, 60), (y0 + y1) / 2
    out = [f'<path d="M {x0:.1f} {y0:.1f} Q {cx:.1f} {cy:.1f} {x1:.1f} {y1:.1f}" '
           f'stroke="{p["forest"]}" stroke-width="5" stroke-linecap="round" fill="none"/>']
    for i in range(leaves):
        t = 0.25 + 0.6 * i / max(1, leaves - 1)
        # point on the quadratic curve and its direction
        bx = (1 - t) ** 2 * x0 + 2 * (1 - t) * t * cx + t * t * x1
        by = (1 - t) ** 2 * y0 + 2 * (1 - t) * t * cy + t * t * y1
        dx = 2 * (1 - t) * (cx - x0) + 2 * t * (x1 - cx)
        dy = 2 * (1 - t) * (cy - y0) + 2 * t * (y1 - cy)
        ang = math.degrees(math.atan2(dy, dx))
        side = 1 if i % 2 else -1
        out.append(leaf(bx, by, leaf_len * rng.uniform(0.8, 1.15), ang + side * rng.uniform(38, 58),
                        colors[i % len(colors)]))
    if flower_r:
        out.append(flower(x1, y1, flower_r, p, rng.uniform(0, 72)))
    else:
        out.append(f'<ellipse cx="{x1:.1f}" cy="{y1:.1f}" rx="7" ry="11" fill="{p["petal"]}" '
                   f'transform="rotate({rng.uniform(-30, 30):.0f} {x1:.1f} {y1:.1f})"/>')
    return "".join(out)


def hero_spray(p, seed=5):
    """Right side of the library hero card: a low sun, flowering stems, one bird."""
    w, h = 900, 420
    rng = random.Random(seed)
    leaf_colors = [p["sage"], p["olive"], p["forest"]]
    body = ['<g filter="url(#paint)">',
            f'<circle cx="{w * 0.62:.0f}" cy="{h * 0.62:.0f}" r="150" fill="{p["sun"]}" opacity="0.4" filter="url(#rough)"/>']
    stems = [  # base x, tip x, tip y, leaves, flower radius, leaf length
        (820, 700, 120, 5, 34, 58), (860, 790, 60, 6, 28, 50), (760, 600, 190, 4, 0, 52),
        (880, 880, 150, 4, 24, 46), (700, 520, 260, 3, 22, 44), (900, 640, 80, 5, 0, 48),
    ]
    for bx, tx, ty, nl, fr, ll in stems:
        body.append(stem(rng, bx, h + 10, tx, ty, p, nl, fr, ll, leaf_colors))
    for _ in range(9):  # tiny floating petals / seeds
        body.append(f'<circle cx="{rng.uniform(420, 860):.0f}" cy="{rng.uniform(40, 300):.0f}" '
                    f'r="{rng.uniform(2.5, 4.5):.1f}" fill="{p["petal_hi"]}" opacity="0.8"/>')
    body.append("</g>")
    body.append(bird(w * 0.52, h * 0.22, 0.7, p, flip=True))
    return svg(w, h, "".join(body))


def blob(cx, cy, r, rng, points=9, wobble=0.09):
    """An organic, hand-cut round shape: a circle with gentle irregular bulges."""
    pts = []
    for i in range(points):
        a = 2 * math.pi * i / points
        rr = r * (1 + rng.uniform(-wobble, wobble))
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    d = []
    for i in range(points):
        p0, p1, p2 = pts[i - 1], pts[i], pts[(i + 1) % points]
        m0 = ((p0[0] + p1[0]) / 2, (p0[1] + p1[1]) / 2)
        m1 = ((p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2)
        if i == 0:
            d.append(f"M {m0[0]:.1f} {m0[1]:.1f}")
        d.append(f"Q {p1[0]:.1f} {p1[1]:.1f} {m1[0]:.1f} {m1[1]:.1f}")
    return " ".join(d) + " Z"


def vignette(p, seed=21):
    """Empty-state picture: a little round meadow - sun, hills, a sprout, two birds."""
    w, h = 400, 340
    rng = random.Random(seed)
    shape = blob(200, 176, 150, rng)
    body = [f'<defs><clipPath id="v"><path d="{shape}"/></clipPath></defs>',
            f'<path d="{shape}" fill="{p["sky"]}" filter="url(#paint)"/>',
            '<g clip-path="url(#v)" filter="url(#paint)">',
            f'<circle cx="236" cy="160" r="70" fill="{p["sun"]}" filter="url(#rough)"/>',
            f'<path d="{ridge(rng, 400, 250, 60, 3, 340)}" fill="{p["rose"]}" opacity="0.9" filter="url(#rough)"/>',
            f'<path d="{ridge(rng, 400, 292, 40, 4, 340)}" fill="{p["sage"]}" filter="url(#rough)"/>']
    for _ in range(6):
        body.append(grass_tuft(rng.uniform(60, 340), 330 - rng.uniform(0, 14), rng.uniform(12, 18), p, rng))
    body.append(stem(rng, 176, 332, 170, 208, p, 4, 24, 36, [p["sage"], p["olive"], p["forest"]]))
    body.append(stem(rng, 196, 332, 214, 246, p, 2, 0, 30, [p["olive"], p["sage"]]))
    body.append("</g>")
    body.append(bird(118, 116, 0.5, p))
    body.append(bird(150, 92, 0.36, p))
    return svg(w, h, "".join(body))


def render(name, svg_text, width):
    src = os.path.join(HERE, f"{name}.svg")
    with open(src, "w") as f:
        f.write(svg_text)
    dst = os.path.join(OUT, f"{name}.png")
    subprocess.run(["rsvg-convert", "-w", str(width), src, "-o", dst], check=True)
    return dst


def rasterize(src_name, dst_name, width):
    """Render one of the hand-authored SVGs in this folder (emblem.svg, ...)."""
    dst = os.path.join(OUT, f"{dst_name}.png")
    subprocess.run(["rsvg-convert", "-w", str(width), os.path.join(HERE, src_name), "-o", dst], check=True)
    return dst


if __name__ == "__main__":
    for mode, p in PALETTES.items():
        print(render(f"hills-{mode}", hills(p), 2400))
        print(render(f"hero-spray-{mode}", hero_spray(p), 900))
        print(render(f"vignette-{mode}", vignette(p), 400))
    print(rasterize("emblem.svg", "emblem", 512))
    # the small-size drawing reads at rail/tab sizes where the full one turns to mush
    print(rasterize("emblem-small.svg", "emblem-small", 128))
