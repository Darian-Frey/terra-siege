"""Regenerate ship, structure and scenery meshes for assets/meshes/ from their Python definitions.

Run through Blender (4.x) from the repo root:

    blender -b --factory-startup -P scripts/meshgen/build.py -- [options] [names...]

Options:
    --out DIR      where to write the .obj files (default: assets/meshes)
    --render DIR   also render preview PNGs (six views per model) into DIR
names: any of the model names below; default is all of them.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import enemies
import structures
import player
import scenery
from shiplib import render, setup_scene, write_obj

SHIPS = {**player.SHIPS, **enemies.SHIPS, **structures.SHIPS, **scenery.SHIPS}
HEADERS = {**player.HEADERS, **enemies.HEADERS, **structures.HEADERS,
           **scenery.HEADERS}


def main(argv):
    out = os.path.join(HERE, "..", "..", "assets", "meshes")
    render_dir = None
    names = []
    it = iter(argv)
    for a in it:
        if a == "--out":
            out = next(it)
        elif a == "--render":
            render_dir = next(it)
        elif a in SHIPS:
            names.append(a)
        else:
            sys.exit("unknown argument %r; models: %s" % (a, ", ".join(SHIPS)))
    out = os.path.normpath(out)
    os.makedirs(out, exist_ok=True)
    if render_dir:
        os.makedirs(render_dir, exist_ok=True)
        setup_scene()
    for name in names or SHIPS:
        ship, size = SHIPS[name]()
        path = os.path.join(out, name + ".obj")
        v, t = write_obj(ship, path, HEADERS[name])
        print("meshgen: %s  %d verts  %d tris  -> %s" % (name, v, t, path))
        if render_dir:
            render(ship, render_dir, size)


main(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
