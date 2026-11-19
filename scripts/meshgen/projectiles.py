"""Projectile models. Bolt and orb are white/light-grey so the game can
tint them per shot (laser colour, cannon owner, plasma) — the two tones
keep the facets readable after tinting — and draw a larger translucent
copy as the glow. Each builder returns (Ship, preview ortho size)."""
import math
from shiplib import Ship


def _alt(a, b):
    return lambda k: a if k % 2 == 0 else b


def bolt():
    """Tracer bolt for lasers and cannon shots: a six-sided double-ended
    spindle, unit size (y -0.5..0.5 along the flight path, radius 0.5).
    The game scales it per axis and points +y along the velocity."""
    s = Ship("bolt")
    n = 6
    col = _alt(1, 15)
    def ring(y):
        return [s.vert(0.5 * math.cos(2 * math.pi * k / n), y,
                       0.5 * math.sin(2 * math.pi * k / n)) for k in range(n)]
    front, back = ring(0.25), ring(-0.25)
    nose, tail = s.vert(0, 0.5, 0), s.vert(0, -0.5, 0)
    for k in range(n):
        j = (k + 1) % n
        s.face([front[k], front[j], nose], col(k))
        s.face([back[j], back[k], tail], col(k + 1))
        s.face([back[k], back[j], front[j], front[k]], col(k + 1))
    return s, 1.5


def orb():
    """Plasma orb: an icosahedron of radius 0.5, two-tone faces."""
    s = Ship("orb")
    p = (1 + math.sqrt(5)) / 2
    r = 0.5 / math.sqrt(1 + p * p)
    pts = [(-1, p, 0), (1, p, 0), (-1, -p, 0), (1, -p, 0),
           (0, -1, p), (0, 1, p), (0, -1, -p), (0, 1, -p),
           (p, 0, -1), (p, 0, 1), (-p, 0, -1), (-p, 0, 1)]
    v = [s.vert(x * r, y * r, z * r) for x, y, z in pts]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11),
             (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
             (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9),
             (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for k, (a, b, c) in enumerate(faces):
        s.face([v[a], v[b], v[c]], 1 if k % 2 == 0 else 15)
    return s, 1.5


def depth_charge():
    """Gravity bomb, drawn upright at scale 1: an octagonal drum
    (1.4 across, 1 m tall, centred on the origin) with yellow hazard
    bands and crossed yellow tail fins on top."""
    s = Ship("depth_charge")
    s.stack([(-0.5, 0.6, 0.6), (-0.4, 0.7, 0.7), (-0.25, 0.7, 0.7), (-0.15, 0.7, 0.7),
             (0.15, 0.7, 0.7), (0.25, 0.7, 0.7), (0.4, 0.7, 0.7), (0.5, 0.6, 0.6)],
            [_alt(22, 21), _alt(21, 22), 17, _alt(22, 21), 17, _alt(21, 22), _alt(22, 21)],
            n=8, rot=math.pi / 8, top=21, bottom=22)
    for a in (0.0, math.pi / 2):
        c, sn = math.cos(a), math.sin(a)
        fin = [s.vert(-0.35 * c, -0.35 * sn, 0.5), s.vert(0.35 * c, 0.35 * sn, 0.5),
               s.vert(0.25 * c, 0.25 * sn, 0.9), s.vert(-0.25 * c, -0.25 * sn, 0.9)]
        s.twoside(fin, 17)
    return s, 2.5


SHIPS = {"bolt": bolt, "orb": orb, "depth_charge": depth_charge}

HEADERS = {
"bolt": """terra-siege projectile bolt — laser and cannon tracer.
Six-sided double-ended spindle, unit size: z -0.5..0.5 along the flight
path, radius 0.5. {verts} verts, {tris} tris. White/light grey so the
game tints it per shot and draws a larger translucent copy as the glow;
scaled per axis and pointed along the velocity. c01/c15 facets.""",
"orb": """terra-siege projectile orb — plasma shot.
Icosahedron of radius 0.5. {verts} verts, {tris} tris. White/light grey
so the game tints it magenta and draws a larger translucent copy as the
halo. c01/c15 facets.""",
"depth_charge": """terra-siege depth charge — falling gravity bomb.
Octagonal drum 1.4 across and 1 m tall, centred on the origin, with two
yellow hazard bands and crossed tail fins on top (to y = 0.9).
{verts} verts, {tris} tris. Drawn upright at scale 1.
c21/c22 drum, c17 bands and fins.""",
}
