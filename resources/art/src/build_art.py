#!/usr/bin/env python3
"""Build Sprout's meadow artwork from the SVG templates beside this file.

Run python resources/art/src/build_art.py with Pillow and either rsvg-convert or CairoSVG.
The theme-specific SVGs and PNGs are checked in so app builds need no renderer.
"""
from pathlib import Path
import shutil
from string import Template
import subprocess

from PIL import Image

HERE = Path(__file__).resolve().parent
OUT = HERE.parent

# Inks are ordered from paper through foreground, with a separate set for dusk.
PALETTES = {
    "light": dict(
        sky="#F5E8D3", mist="#E6CABB", ridge="#C6A39A", field="#A8AD80",
        meadow="#829A78", leaf="#5C816C", pine="#355D50", ink="#23483F",
        etch="#CAD3A2", glass="#C3D9C4", pane="#9CBFB0", frame="#4C7060",
        sun="#F3BC78", gold="#EDB45E", pollen="#FFF1BC", silk="#F3AE95",
        petal="#DD806F", fold="#B85C57", wing="#CB8757",
    ),
    "dark": dict(
        sky="#303B44", mist="#504859", ridge="#665665", field="#566450",
        meadow="#425C51", leaf="#6E9382", pine="#3E675C", ink="#294C46",
        etch="#A0B598", glass="#77958B", pane="#5B7D76", frame="#AEC0A5",
        sun="#D4C7A2", gold="#CEAB72", pollen="#EEDCBA", silk="#DBAA9D",
        petal="#C58B83", fold="#9C686C", wing="#B09878",
    ),
}

SKY_DISC = {
    "light": '<circle r="40" fill="$sun"/>',
    "dark": '<path d="M21-34A40 40 0 1 0 36 18A34 34 0 0 1 21-34Z" fill="$sun"/>',
}


def template(name):
    return Template((HERE / f"{name}.svg.in").read_text(encoding="utf-8"))


def illustration(name, mode):
    colors = PALETTES[mode]
    symbols = {}
    if name in ("hero-spray", "vignette"):
        symbols["sky_disc"] = Template(SKY_DISC[mode]).substitute(colors)
    if name in ("vignette", "hills"):
        symbols["glasshouse"] = template("glasshouse").substitute(colors)
    return template(name).substitute(colors, **symbols)


def rasterize(name, width, output=None):
    source = HERE / f"{name}.svg"
    output = output or OUT / f"{name}.png"
    if shutil.which("rsvg-convert"):
        subprocess.run(["rsvg-convert", "-w", str(width), source, "-o", output], check=True)
    else:
        from cairosvg import svg2png
        svg2png(url=str(source), output_width=width, write_to=str(output))


def build_emblem():
    icons = OUT.parent / "icons"
    for name in ("arachnel.svg", "arachnel-github.svg"):
        shutil.copyfile(HERE / "emblem.svg", icons / name)
    for size in (16, 22, 24, 32, 48, 64, 128, 256, 512):
        rasterize("emblem", size, icons / "png" / f"{size}.png")
    shutil.copyfile(icons / "png/128.png", OUT / "emblem-small.png")
    shutil.copyfile(icons / "png/512.png", OUT / "emblem.png")
    with Image.open(OUT / "emblem.png") as logo:
        logo.save(icons / "arachnel.ico", sizes=[(n, n) for n in (16, 24, 32, 48, 64, 128, 256)])


if __name__ == "__main__":
    for mode in PALETTES:
        for scene, width in (("hills", 2400), ("hero-spray", 900), ("vignette", 400)):
            name = f"{scene}-{mode}"
            (HERE / f"{name}.svg").write_bytes(illustration(scene, mode).encode("utf-8"))
            rasterize(name, width)
    build_emblem()
