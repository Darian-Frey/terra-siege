"""Scenery: trees, boulders, antennas and crash sites. The game draws these instanced (one draw
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


# ---------------------------------------------------------------- Antenna
def antenna():
    """Guyed radio mast, about 9 m to the beacon. Footing, square mast,
    equipment box, red beacon and three guy wires to ground anchors at
    1.8 m."""
    s = Ship("antenna")
    s.block((-0.45, 0.45, -0.45, 0.45, -0.3), (-0.4, 0.4, -0.4, 0.4, 0.25), 30)
    s.stack([(0.25, 0.16, 0.16), (8.2, 0.1, 0.1)], [_alt(15, 8)], n=4, rot=math.pi / 4)
    s.block((-0.45, 0.45, -0.45, 0.45, 8.2), (-0.45, 0.45, -0.45, 0.45, 8.6),
            {"top": 15, "bottom": 8, "front": 8, "back": 15, "left": 15, "right": 8})
    s.block((-0.12, 0.12, -0.12, 0.12, 8.6), (-0.1, 0.1, -0.1, 0.1, 8.9), 2)
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 6
        gx, gy = 1.8 * math.cos(a), 1.8 * math.sin(a)
        s.strut((0, 0, 7.2), (gx, gy, 0.0), 0.025, 8)
        s.block((gx - 0.15, gx + 0.15, gy - 0.15, gy + 0.15, -0.2),
                (gx - 0.12, gx + 0.12, gy - 0.12, gy + 0.12, 0.15), 30)
    return s, 11.0


# ---------------------------------------------------------------- Crash site
def crash_site():
    """Burnt-out wreck of a small craft, nose dug into the ground (nose
    +Z), about 5 m long. Mottled scorched hull, one crumpled wing, a torn
    stub, a bent tail fin, a wing panel lying beside it, and a glowing
    ember in a hole in the spine."""
    s = Ship("crash_site")
    tilt = lambda y, z: z - 0.25 * y  # nose down
    def sec(y, prof):
        return (y, [(x, tilt(y, z)) for x, z in prof])
    s.loft([
        sec(2.6, [(0, 0.15)]),
        sec(1.6, [(0, 0.75), (0.7, 0.45), (0.75, -0.1), (0, -0.25)]),
        sec(-1.2, [(0, 0.8), (0.75, 0.5), (0.8, -0.05), (0, -0.25)]),
        sec(-2.4, [(0, 0.6), (0.5, 0.4), (0.55, 0.05), (0, -0.05)]),
    ], [[22, 30, 0], [0, 22, 0], [22, 0, 0]], back=0)
    # crumpled right wing, torn left stub
    w = [s.vert(0.7, 0.6, 0.1), s.vert(2.3, 0.2, 0.5), s.vert(2.3, -0.4, 0.45), s.vert(0.7, -0.6, 0.15)]
    s.twoside(w, 30)
    w = [s.vert(-0.7, 0.5, 0.1), s.vert(-1.3, 0.3, -0.1), s.vert(-1.3, -0.3, -0.15), s.vert(-0.7, -0.5, 0.12)]
    s.twoside(w, 0)
    # bent tail fin
    f = [s.vert(0, -1.4, tilt(-1.4, 0.75)), s.vert(0, -2.4, tilt(-2.4, 0.55)), s.vert(0.35, -2.5, 2.0)]
    s.twoside(f, 22)
    # torn-off wing panel lying on the ground to port
    s.block((-2.7, -1.6, -1.6, 0.2, 0.0), (-2.7, -1.6, -1.6, 0.2, 0.12),
            {"top": 30, "bottom": 0, "front": 0, "back": 0, "left": 0, "right": 0})
    # ember in a hole in the spine
    s.block((-0.3, 0.3, -0.35, 0.35, tilt(0, 0.7)), (-0.25, 0.25, -0.3, 0.3, tilt(0, 0.92)), 11)
    s.block((-0.12, 0.12, -0.15, 0.15, tilt(0, 0.9)), (-0.1, 0.1, -0.12, 0.12, tilt(0, 0.98)), 17)
    return s, 7.0


def crash_debris():
    """Scorched wreckage chunk scattered around a crash site. Same span
    as the boulders; the game flattens it per copy."""
    return _boulder("crash_debris", 51, 30, _alt(0, 22), 0)


SHIPS = {"tree_conifer": tree_conifer, "tree_broadleaf": tree_broadleaf,
         "rock_a": rock_a, "rock_b": rock_b, "rock_c": rock_c,
         "antenna": antenna, "crash_site": crash_site, "crash_debris": crash_debris}

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
"antenna": """terra-siege antenna — guyed radio mast decoration.
Concrete footing, two-tone square mast, equipment box at 8.2-8.6, red
beacon to 8.9, three guy wires to ground anchors 1.8 m out.
{verts} verts, {tris} tris. Drawn instanced, scaled uniformly per copy.
Origin at ground level. c30 footing/anchors, c15/c08 mast and box,
c02 beacon, c08 wires.""",
"crash_site": """terra-siege crash site — burnt-out wreck decoration.
Scorched hull with its nose dug in, crumpled wing, torn stub, bent tail
fin, a wing panel on the ground and a glowing ember in the spine.
{verts} verts, {tris} tris. Drawn instanced with crash_debris scattered
around it. Origin at ground level, +Z = nose. c22/c30/c00 hull,
c11/c17 ember.""",
"crash_debris": """terra-siege crash debris — scorched wreckage chunk. Crash sites
scatter these around the wreck, scaled per axis (flattened) and yawed
per chunk, drawn instanced. Spans about x, z in [-0.5, 0.5] and y in
[-0.1, 0.9] before scaling (origin at ground level).
{verts} verts, {tris} tris. c30 top, c00/c22 sides, c00 base.""",
}

