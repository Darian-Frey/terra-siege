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


def tree_poplar():
    """Tall narrow column crown on a short trunk, about 7 m tall: the
    field-edge and roadside line tree."""
    s = Ship("tree_poplar")
    s.stack([(-0.3, 0.15, 0.15), (1.0, 0.12, 0.12)], [14], n=4, rot=math.pi / 4)
    rings = s.stack([(0.8, 0.3, 0.3), (1.7, 0.62, 0.62), (4.2, 0.72, 0.72), (6.0, 0.48, 0.48)],
                    [_alt(28, 25), _alt(24, 28), _alt(28, 25)], n=6, bottom=25)
    _cone(s, rings[-1], (0, 0, 7.2), _alt(24, 28), 25)
    return s, 9.0


def tree_birch():
    """Slender white trunk with a black band and a light green crown,
    about 4.3 m tall."""
    s = Ship("tree_birch")
    s.stack([(-0.3, 0.12, 0.12), (0.8, 0.11, 0.11), (1.0, 0.11, 0.11), (2.2, 0.09, 0.09)],
            [15, 0, 15], n=4, rot=math.pi / 4)
    s.stack([(1.8, 0.4, 0.4), (2.4, 0.95, 0.95), (3.5, 0.85, 0.85), (4.3, 0.35, 0.35)],
            [_alt(29, 24), _alt(24, 29), _alt(29, 24)], n=5, bottom=28, top=24)
    return s, 6.0


def bush():
    """Low rounded shrub, about 1.1 m tall and 1.5 m across: hedgerow
    filler and forest-edge undergrowth."""
    s = Ship("bush")
    s.stack([(-0.15, 0.5, 0.5), (0.35, 0.75, 0.75), (0.8, 0.6, 0.6), (1.1, 0.25, 0.25)],
            [_alt(28, 25), _alt(24, 28), _alt(28, 24)], n=6, rot=math.pi / 6, top=24)
    return s, 2.5


def tree_dead():
    """Bare grey trunk with four crooked branches, about 4.5 m tall."""
    s = Ship("tree_dead")
    s.stack([(-0.3, 0.2, 0.2), (1.5, 0.15, 0.15), (3.0, 0.1, 0.1), (3.8, 0.05, 0.05)],
            [26, 30, 26], n=4, rot=math.pi / 4, top=30)
    for (bx, by, bz), (tx, ty, tz) in (((0, 0, 1.6), (0.9, 0.3, 2.6)),
                                       ((0, 0, 2.2), (-0.8, 0.4, 3.1)),
                                       ((0, 0, 2.7), (0.2, -0.9, 3.7)),
                                       ((0.5, 0.17, 2.2), (0.7, 0.9, 2.9))):
        s.strut((bx, by, bz), (tx, ty, tz), 0.05, 26)
    s.strut((0, 0, 3.6), (0.3, 0.2, 4.5), 0.035, 30)
    return s, 6.0


# ---------------------------------------------------------------- Groves
def _merge(dst, src, dx, dy, scale, yaw, sink=-2.5, skirt=None):
    """Copy src's faces into dst, scaled, turned about the vertical and
    moved to (dx, dy). Trunk feet (thin, below ground) drop to `sink` so
    the stand stays planted when the game shears it onto a slope. With
    `skirt`, other below-ground vertices (a bush's bottom ring) drop to
    it too, so low shrubs don't float on uneven ground."""
    c, sn = math.cos(yaw), math.sin(yaw)
    remap = {}
    for i, (x, my, z) in enumerate(src.v):
        y = -my
        if z < 0 and abs(x) < 0.3 and abs(y) < 0.3:
            z = sink / scale
        elif z < 0 and skirt is not None:
            z = skirt / scale
        remap[i] = dst.vert(dx + scale * (x * c - y * sn), dy + scale * (x * sn + y * c), scale * z)
    for idx, col in src.f:
        dst.face([remap[i] for i in idx], col, ref=None)


def _grove(name, seed, mix, radius=8.0, spacing=2.5, skirt=None):
    """A stand of trees as one model: Poisson-disc spots inside a wobbly
    disc of about `radius` metres, each a tree picked from `mix`
    [(builder, weight)] with its own size and turn."""
    rnd = random.Random(seed)
    s = Ship(name)
    wob = [rnd.uniform(0, 2 * math.pi) for _ in range(2)]
    def edge(a):
        return radius * (0.82 + 0.1 * math.sin(3 * a + wob[0]) + 0.08 * math.sin(5 * a + wob[1]))
    spots = []
    for _ in range(4000):
        a = rnd.uniform(0, 2 * math.pi)
        r = edge(a) * math.sqrt(rnd.random())
        x, y = r * math.cos(a), r * math.sin(a)
        if all((x - u) ** 2 + (y - v) ** 2 >= spacing ** 2 for u, v in spots):
            spots.append((x, y))
    total = sum(w for _, w in mix)
    cache = {}
    for x, y in spots:
        pick = rnd.uniform(0, total)
        for build, w in mix:
            pick -= w
            if pick <= 0:
                break
        if build not in cache:
            cache[build] = build()[0]
        _merge(s, cache[build], x, y, rnd.uniform(0.85, 1.3), rnd.uniform(0, 2 * math.pi),
               skirt=skirt)
    return s, 2 * radius + 4


def grove_conifer_a():
    return _grove("grove_conifer_a", 11, [(tree_conifer, 1)], spacing=2.3)


def grove_conifer_b():
    return _grove("grove_conifer_b", 23, [(tree_conifer, 6), (tree_dead, 0.4), (tree_birch, 0.6)], spacing=2.4)


def grove_broadleaf_a():
    return _grove("grove_broadleaf_a", 37, [(tree_broadleaf, 1)], spacing=2.6)


def grove_broadleaf_b():
    return _grove("grove_broadleaf_b", 41, [(tree_broadleaf, 6), (tree_poplar, 0.6), (bush, 1)], spacing=2.5)


def grove_birch():
    return _grove("grove_birch", 53, [(tree_birch, 6), (bush, 1), (tree_broadleaf, 0.5)], spacing=2.4)


def grove_mixed():
    return _grove("grove_mixed", 67, [(tree_conifer, 1), (tree_broadleaf, 1), (tree_birch, 0.4)], spacing=2.5)


# Copses: small clumps for the thinner forest edges.
def copse_conifer():
    return _grove("copse_conifer", 101, [(tree_conifer, 1)], radius=4.5, spacing=2.3)


def copse_broadleaf():
    return _grove("copse_broadleaf", 113, [(tree_broadleaf, 4), (bush, 1)], radius=4.5,
                  spacing=2.4, skirt=-0.6)


def copse_birch():
    return _grove("copse_birch", 127, [(tree_birch, 3), (bush, 2)], radius=4.5, spacing=2.2,
                  skirt=-0.6)


def copse_mixed():
    return _grove("copse_mixed", 131, [(tree_conifer, 1), (tree_broadleaf, 1), (bush, 0.5)],
                  radius=4.5, spacing=2.4, skirt=-0.6)


# ---------------------------------------------------------------- Hedges
def _hedge(name, seed, tree=None):
    """A 4 m stretch of hedge along +Y (y -2..2): a jittered row of bushes,
    optionally with one tree standing in it. The game lays these end to
    end along field edges, stretched along Y to fit."""
    rnd = random.Random(seed)
    s = Ship(name)
    b = bush()[0]
    y = -2.0 + rnd.uniform(0.3, 0.6)
    while y < 1.8:
        _merge(s, b, rnd.uniform(-0.25, 0.25), y, rnd.uniform(0.85, 1.25),
               rnd.uniform(0, 2 * math.pi), skirt=-0.6)
        y += rnd.uniform(0.95, 1.25)
    if tree:
        _merge(s, tree()[0], rnd.uniform(-0.2, 0.2), rnd.uniform(-0.6, 0.6),
               rnd.uniform(1.0, 1.3), rnd.uniform(0, 2 * math.pi))
    return s, 7.0


def hedge_a():
    return _hedge("hedge_a", 201)


def hedge_b():
    return _hedge("hedge_b", 211)


def hedge_tree():
    return _hedge("hedge_tree", 223, tree_broadleaf)


def hedge_birch():
    return _hedge("hedge_birch", 227, tree_birch)


# ---------------------------------------------------------------- Walls & fences
def _wall(name, seed, top_cols):
    """A 4 m stretch of dry-stone wall along +Y (y -2..2): about 1.1 m
    high, battered from 0.7 m wide at the foot to 0.45 m at the top, in
    stone courses, with a row of upright coping stones. Its foot reaches
    y = -0.4 so it stays planted when sheared onto a slope."""
    rnd = random.Random(seed)
    s = Ship(name)
    cols = [8, 8, 15, 26, 30]
    # Body: four 1 m sections in two courses, stones alternating in tone.
    y = -2.0
    while y < 2.0 - 1e-6:
        y1 = min(2.0, y + rnd.uniform(0.7, 1.1))
        for z0, z1, w0, w1 in ((-0.4, 0.45, 0.35, 0.29), (0.45, 0.92, 0.29, 0.22)):
            c = rnd.choice(cols)
            s.block((-w0, w0, y, y1, z0), (-w1, w1, y, y1, z1),
                    {"top": c, "bottom": 30, "front": c, "back": c,
                     "left": rnd.choice(cols), "right": rnd.choice(cols)})
        y = y1
    # Coping: upright stones on edge along the top.
    y = -2.0
    while y < 1.95:
        t = rnd.uniform(0.16, 0.24)
        h = rnd.uniform(0.14, 0.24)
        c = rnd.choice(top_cols)
        s.block((-0.24, 0.24, y, y + t, 0.9), (-0.21, 0.21, y + 0.01, y + t - 0.01, 0.92 + h),
                {"top": c, "bottom": 30, "front": c, "back": c, "left": c, "right": c})
        y += t + rnd.uniform(0.0, 0.03)
    return s, 6.0


def wall_a():
    return _wall("wall_a", 301, [8, 15, 26])


def wall_b():
    return _wall("wall_b", 307, [24, 24, 8, 15])


def _fence(name, posts, rail_col, post_col, rails):
    """A 4 m stretch of post-and-rail fence along +Y (y -2..2), about
    1.25 m high. Posts sit at the given y (the next stretch supplies the
    post at y = 2); rails run the full length at the given heights."""
    s = Ship(name)
    for y in posts:
        s.block((-0.07, 0.07, y - 0.07, y + 0.07, -0.4), (-0.06, 0.06, y - 0.06, y + 0.06, 1.25),
                {"top": post_col, "bottom": post_col, "front": post_col, "back": post_col,
                 "left": post_col, "right": post_col})
    for z in rails:
        s.block((-0.1, -0.03, -2.0, 2.0, z - 0.06), (-0.1, -0.03, -2.0, 2.0, z + 0.06),
                {"top": rail_col, "bottom": rail_col, "front": rail_col, "back": rail_col,
                 "left": rail_col, "right": rail_col})
    return s, 6.0


def fence_a():
    return _fence("fence_a", (-2.0, -0.67, 0.67), 26, 14, (0.4, 0.75, 1.1))


def fence_b():
    return _fence("fence_b", (-2.0, 0.0), 15, 15, (0.55, 1.05))


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
         "tree_poplar": tree_poplar, "tree_birch": tree_birch, "bush": bush,
         "tree_dead": tree_dead,
         "grove_conifer_a": grove_conifer_a, "grove_conifer_b": grove_conifer_b,
         "grove_broadleaf_a": grove_broadleaf_a, "grove_broadleaf_b": grove_broadleaf_b,
         "grove_birch": grove_birch, "grove_mixed": grove_mixed,
         "copse_conifer": copse_conifer, "copse_broadleaf": copse_broadleaf,
         "copse_birch": copse_birch, "copse_mixed": copse_mixed,
         "hedge_a": hedge_a, "hedge_b": hedge_b, "hedge_tree": hedge_tree,
         "hedge_birch": hedge_birch,
         "wall_a": wall_a, "wall_b": wall_b, "fence_a": fence_a, "fence_b": fence_b,
         "rock_a": rock_a, "rock_b": rock_b, "rock_c": rock_c,
         "antenna": antenna, "crash_site": crash_site, "crash_debris": crash_debris}

_TREE = """Drawn instanced by Decorations (one draw call per model), scaled
uniformly per tree. Origin at ground level; the trunk reaches y = -0.3
so it stays planted on slopes. Local frame: +Z forward."""
_GROVE = """terra-siege grove, {kind}: a stand of trees as one model,
about 16 m across with a wobbly outline. {verts} verts, {tris} tris.
Built from the single-tree models. Dense forests are filled with these,
drawn instanced, turned at random and sheared onto the local slope (the
trees stay upright); trunk feet reach y = -2.5 so they stay planted.
Origin at ground level, at the centre of the stand."""
_COPSE = """terra-siege copse, {kind}: a clump of a few trees as one model,
about 9 m across. {verts} verts, {tris} tris. Fills the thinner forest
edges, drawn instanced, turned at random and sheared onto the slope.
Origin at ground level, at the centre of the clump."""
_HEDGE = """terra-siege hedge segment, {kind}: a 4 m stretch of hedge along +Z
(z -2..2). {verts} verts, {tris} tris. Laid end to end along field
edges, drawn instanced, stretched along Z to fit and sheared onto the
slope. Bush bottoms reach y = -0.6 so they stay planted.
Origin at ground level, at the middle of the stretch."""
_BOUNDARY = """terra-siege field boundary, {kind}. A 4 m stretch along +Z
(z -2..2). {verts} verts, {tris} tris. Laid end to end along field
edges, drawn instanced, stretched along Z to fit and sheared onto the
slope; the foot reaches y = -0.4 so it stays planted.
Origin at ground level, at the middle of the stretch."""
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
"tree_poplar": "terra-siege tree, poplar: tall six-sided column crown on a short trunk.\n"
               "{verts} verts, {tris} tris, about 7.2 m tall at scale 1. Lines field edges.\n" + _TREE +
               "\nc14 trunk, c24/c28/c25 leaves.",
"tree_birch": "terra-siege tree, birch: white trunk with a black band, light green crown.\n"
              "{verts} verts, {tris} tris, about 4.3 m tall at scale 1.\n" + _TREE +
              "\nc15/c00 trunk, c29/c24/c28 leaves.",
"bush": "terra-siege bush: low six-sided shrub, no trunk. Hedgerows and forest edges.\n"
        "{verts} verts, {tris} tris, about 1.1 m tall and 1.5 m across at scale 1.\n" + _TREE +
        "\nc28/c25/c24 leaves.",
"tree_dead": "terra-siege tree, dead: bare tapering trunk with crooked branches.\n"
             "{verts} verts, {tris} tris, about 4.5 m tall at scale 1.\n" + _TREE +
             "\nc26/c30 bark.",
"grove_conifer_a": _GROVE.replace("{kind}", "conifer A (pure pine)"),
"grove_conifer_b": _GROVE.replace("{kind}", "conifer B (pine with the odd birch and dead tree)"),
"grove_broadleaf_a": _GROVE.replace("{kind}", "broadleaf A (pure)"),
"grove_broadleaf_b": _GROVE.replace("{kind}", "broadleaf B (with poplars and undergrowth)"),
"grove_birch": _GROVE.replace("{kind}", "birch (with undergrowth and the odd broadleaf)"),
"grove_mixed": _GROVE.replace("{kind}", "mixed conifer and broadleaf"),
"copse_conifer": _COPSE.replace("{kind}", "conifer"),
"copse_broadleaf": _COPSE.replace("{kind}", "broadleaf with a bush"),
"copse_birch": _COPSE.replace("{kind}", "birch and bushes"),
"copse_mixed": _COPSE.replace("{kind}", "mixed conifer, broadleaf and bush"),
"hedge_a": _HEDGE.replace("{kind}", "bushes A"),
"hedge_b": _HEDGE.replace("{kind}", "bushes B"),
"hedge_tree": _HEDGE.replace("{kind}", "bushes with a broadleaf tree"),
"hedge_birch": _HEDGE.replace("{kind}", "bushes with a birch"),
"wall_a": _BOUNDARY.replace("{kind}", "dry-stone wall A: about 1.1 m high, 0.7 m wide at\nthe foot, stone courses and upright coping. c08/c15/c26/c30 stone"),
"wall_b": _BOUNDARY.replace("{kind}", "dry-stone wall B: as A with a mossy top.\nc08/c15/c26/c30 stone, c24 moss"),
"fence_a": _BOUNDARY.replace("{kind}", "post-and-rail fence A: weathered timber, three\nrails, posts every 1.33 m, about 1.25 m high. c14 posts, c26 rails"),
"fence_b": _BOUNDARY.replace("{kind}", "post-and-rail fence B: painted, two rails, posts\nevery 2 m, about 1.25 m high. c15"),
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

