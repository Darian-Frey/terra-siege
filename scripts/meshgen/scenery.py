"""Scenery: trees and boulders. The game draws these instanced (one draw
call per model for every visible copy), so they stay small. Each builder
returns (Ship, preview ortho size).

Origins sit at ground level. Trunks and boulders reach a little below
it so they stay planted on slopes."""
import math
import random
from shiplib import Ship


def _ring(s, z, r, n, rot=0.0):
    return [s.vert(r * math.cos(rot + 2 * math.pi * k / n),
                   r * math.sin(rot + 2 * math.pi * k / n), z) for k in range(n)]


def _cone(s, ring, tip, cols, bottom):
    """Faceted cone from a ring up to a tip; cols(k) colours facet k."""
    t = s.vert(*tip)
    n = len(ring)
    for k in range(n):
        ref = (0, 0, s.v[ring[k]][2])
        s.face([ring[k], ring[(k + 1) % n], t], cols(k), ref)
    s.face(ring, bottom, (0, 0, tip[2]))


def _alt(a, b):
    return lambda k: a if k % 2 == 0 else b


# ---------------------------------------------------------------- Trees
def tree_conifer():
    """Two stacked six-sided cones on a square trunk, about 4 m tall."""
    s = Ship("tree_conifer")
    s.stack([(-0.3, 0.16, 0.16), (1.1, 0.13, 0.13)], [14], n=4, rot=math.pi / 4)
    _cone(s, _ring(s, 0.9, 1.15, 6), (0, 0, 2.9), _alt(28, 25), 25)
    _cone(s, _ring(s, 2.1, 0.85, 6, math.pi / 6), (0, 0, 4.1), _alt(28, 25), 25)
    return s, 6.0


def tree_broadleaf():
    """Faceted six-sided crown on a square trunk, about 3.3 m tall."""
    s = Ship("tree_broadleaf")
    s.stack([(-0.3, 0.17, 0.17), (1.5, 0.14, 0.14)], [14], n=4, rot=math.pi / 4)
    s.stack([(1.2, 0.55, 0.55), (1.7, 1.1, 1.1), (2.6, 1.15, 1.15), (3.3, 0.7, 0.7)],
            [_alt(28, 25), _alt(24, 28), _alt(24, 28)], n=6, rot=math.pi / 6,
            bottom=25, top=24)
    return s, 5.0


# ---------------------------------------------------------------- Boulders
def _boulder(name, seed, top, sides, bottom):
    """Irregular 20-triangle stone, about 1 m across, y from -0.1 to 0.9
    (the game scales each copy per axis). Two staggered five-vertex
    rings with jittered radii between a bottom point and an off-centre
    top point."""
    rnd = random.Random(seed)
    s = Ship(name)
    s.ref = (0, 0, 0.35)
    n = 5
    lo = [s.vert(rnd.uniform(0.42, 0.52) * math.cos(a), rnd.uniform(0.42, 0.52) * math.sin(a),
                 rnd.uniform(0.0, 0.12))
          for a in (2 * math.pi * k / n for k in range(n))]
    hi = [s.vert(rnd.uniform(0.28, 0.4) * math.cos(a), rnd.uniform(0.28, 0.4) * math.sin(a),
                 rnd.uniform(0.5, 0.65))
          for a in (2 * math.pi * (k + 0.5) / n for k in range(n))]
    b = s.vert(0, 0, -0.1)
    t = s.vert(rnd.uniform(-0.1, 0.1), rnd.uniform(-0.1, 0.1), 0.9)
    for k in range(n):
        j = (k + 1) % n
        s.face([lo[j], lo[k], b], bottom)
        s.face([lo[k], lo[j], hi[k]], sides(k))
        s.face([hi[k], lo[j], hi[j]], sides(k + 1))
        s.face([hi[k], hi[j], t], top)
    return s, 1.6


def rock_a():
    return _boulder("rock_a", 11, 8, _alt(26, 30), 30)


def rock_b():
    return _boulder("rock_b", 23, 26, _alt(30, 8), 30)


def rock_c():
    return _boulder("rock_c", 37, 24, _alt(26, 30), 30)


SHIPS = {"tree_conifer": tree_conifer, "tree_broadleaf": tree_broadleaf,
         "rock_a": rock_a, "rock_b": rock_b, "rock_c": rock_c}

_TREE = """Drawn instanced by Decorations (one draw call per model), scaled
uniformly per tree. Origin at ground level; the trunk reaches y = -0.3
so it stays planted on slopes. Local frame: +Z forward."""
_ROCK = """terra-siege boulder, variant {v}. Rock decorations are clusters of
these, scaled per axis and yawed per boulder, drawn instanced. Spans
about x, z in [-0.5, 0.5] and y in [-0.1, 0.9] (origin at ground level).
{verts} verts, {tris} tris. {cols}"""

HEADERS = {
"tree_conifer": "terra-siege tree, conifer: two six-sided cones on a square trunk.\n"
                "{verts} verts, {tris} tris, about 4 m tall at scale 1.\n" + _TREE +
                "\nc14 trunk, c28/c25 needles.",
"tree_broadleaf": "terra-siege tree, broadleaf: faceted six-sided crown on a square trunk.\n"
                  "{verts} verts, {tris} tris, about 3.3 m tall at scale 1.\n" + _TREE +
                  "\nc14 trunk, c24/c28/c25 leaves.",
"rock_a": _ROCK.replace("{v}", "A").replace("{cols}", "c08 top, c26/c30 sides, c30 base."),
"rock_b": _ROCK.replace("{v}", "B").replace("{cols}", "c26 top, c30/c08 sides, c30 base."),
"rock_c": _ROCK.replace("{v}", "C (mossy)").replace("{cols}", "c24 top, c26/c30 sides, c30 base."),
}
