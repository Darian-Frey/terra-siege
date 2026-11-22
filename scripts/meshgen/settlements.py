"""Settlement buildings and props (Settlements track R.4): the farm and
village sets. Each builder returns (Ship, preview ortho size).

Authoring frame: x across the front, y forward (the front faces +y,
which the game turns towards the road), z up. Origin at ground level in
the middle of the footprint. Walls run down to FOUND (2 m below ground)
so a building sits cleanly on a slope. Windows and doors are single
panels standing PANEL proud of their wall, never coplanar with it, so
they can't z-fight at distance.

Palette: c04 red brick, c05 thatch / hay, c15 white render, c26 stone,
c27 roof tile, c21 slate, c14 timber, c22 window glass, c00 openings."""
import math
from shiplib import Ship

FOUND = -2.0   # foundation depth below ground
PANEL = 0.12   # how far windows and doors stand off their wall
GLASS, DOOR, FRAME = 22, 14, 15


# ---------------------------------------------------------------- toolkit
def _walls(s, w, d, h, front, side=None, back=None, x0=0.0, y0=0.0):
    side = front if side is None else side
    back = front if back is None else back
    s.block((x0 - w / 2, x0 + w / 2, y0 - d / 2, y0 + d / 2, FOUND),
            (x0 - w / 2, x0 + w / 2, y0 - d / 2, y0 + d / 2, h),
            {"top": front, "bottom": front, "front": front, "back": back,
             "left": side, "right": side})


def _gable(s, w, d, h, rise, roof, gable, over=0.35, x0=0.0, y0=0.0, along_y=False):
    """Pitched roof over a w×d box of eave height h; the ridge runs along
    x (or y when along_y), `rise` above the eaves. Gable triangles in the
    wall colour close the ends."""
    def P(a, b, z):  # a along the ridge, b across it
        return s.vert(x0 + b, y0 + a, z) if along_y else s.vert(x0 + a, y0 + b, z)
    L, Wd = (d, w) if along_y else (w, d)
    ref = (x0, y0, h - 0.5)
    A, B = L / 2 + over, Wd / 2 + over
    r0, r1 = P(-A, 0, h + rise), P(A, 0, h + rise)
    e = {(sa, sb): P(sa * A, sb * B, h) for sa in (-1, 1) for sb in (-1, 1)}
    s.face([e[(-1, 1)], e[(1, 1)], r1, r0], roof, ref)
    s.face([e[(1, -1)], e[(-1, -1)], r0, r1], roof, ref)
    s.face([e[(-1, -1)], e[(1, -1)], e[(1, 1)], e[(-1, 1)]], roof, (x0, y0, h + 1.0))  # eave soffit
    for sa in (-1, 1):  # gable ends close the roof under its overhang
        a = sa * A
        s.face([P(a, -B, h), P(a, B, h), P(a, 0, h + rise)], gable, ref)


def _hip(s, w, d, h, rise, roof, over=0.35, x0=0.0, y0=0.0):
    """Hipped roof: four slopes, ridge along the longer side."""
    A, B = w / 2 + over, d / 2 + over
    ref = (x0, y0, h - 0.5)
    ridge = max(0.0, A - B)
    r0, r1 = s.vert(x0 - ridge, y0, h + rise), s.vert(x0 + ridge, y0, h + rise)
    c = {(sa, sb): s.vert(x0 + sa * A, y0 + sb * B, h) for sa in (-1, 1) for sb in (-1, 1)}
    s.face([c[(-1, 1)], c[(1, 1)], r1, r0], roof, ref)
    s.face([c[(1, -1)], c[(-1, -1)], r0, r1], roof, ref)
    s.face([c[(1, 1)], c[(1, -1)], r1], roof, ref)
    s.face([c[(-1, -1)], c[(-1, 1)], r0], roof, ref)
    s.face([c[(-1, -1)], c[(1, -1)], c[(1, 1)], c[(-1, 1)]], roof, (x0, y0, h + 1.0))


def _chimney(s, x, y, z0, z1, col=26, size=0.6):
    s.block((x - size / 2, x + size / 2, y - size / 2, y + size / 2, z0),
            (x - size / 2, x + size / 2, y - size / 2, y + size / 2, z1), col)


def _panel(s, face, a, z, pw, ph, w, d, col, x0=0.0, y0=0.0):
    """A window or door panel on one face of a w×d box: `a` is its centre
    along the face, z its bottom. face: 'front' (+y), 'back', 'left' (-x),
    'right' (+x)."""
    if face in ("front", "back"):
        sy = 1 if face == "front" else -1
        y = y0 + sy * (d / 2 + PANEL)
        q = [s.vert(x0 + a - pw / 2, y, z), s.vert(x0 + a + pw / 2, y, z),
             s.vert(x0 + a + pw / 2, y, z + ph), s.vert(x0 + a - pw / 2, y, z + ph)]
    else:
        sx = 1 if face == "right" else -1
        x = x0 + sx * (w / 2 + PANEL)
        q = [s.vert(x, y0 + a - pw / 2, z), s.vert(x, y0 + a + pw / 2, z),
             s.vert(x, y0 + a + pw / 2, z + ph), s.vert(x, y0 + a - pw / 2, z + ph)]
    s.face(q, col, (x0, y0, z + ph / 2))


def _row(s, face, n, span, z, pw, ph, w, d, col=GLASS, x0=0.0, y0=0.0, skip=()):
    for i in range(n):
        if i in skip:
            continue
        a = -span / 2 + span * (i + 0.5) / n
        _panel(s, face, a, z, pw, ph, w, d, col, x0, y0)


# ---------------------------------------------------------------- farm set
def farmhouse_a():
    """Two-storey white farmhouse, slate roof, chimneys at both gables."""
    s = Ship("farmhouse_a")
    w, d, h = 10.0, 7.0, 5.6
    _walls(s, w, d, h, 15)
    _gable(s, w, d, h, 3.0, 21, 15)
    for x in (-w / 2 + 0.5, w / 2 - 0.5):
        _chimney(s, x, 0.0, h, h + 3.9)
    _row(s, "front", 4, w - 1.0, 3.3, 1.0, 1.3, w, d)
    _row(s, "front", 4, w - 1.0, 0.9, 1.0, 1.3, w, d, skip=(1,))
    _panel(s, "front", -1.4, 0.0, 1.0, 2.1, w, d, DOOR)
    _row(s, "back", 3, w - 2.0, 3.3, 1.0, 1.2, w, d)
    return s, 19.0


def farmhouse_b():
    """Stone farmhouse with a red tile roof and a single-storey wing."""
    s = Ship("farmhouse_b")
    w, d, h = 9.0, 7.0, 5.4
    _walls(s, w, d, h, 26)
    _gable(s, w, d, h, 3.0, 27, 26)
    _chimney(s, -w / 2 + 0.6, 0.0, h, h + 3.8)
    _walls(s, 4.0, 6.0, 3.0, 26, x0=w / 2 + 2.0, y0=-0.5)
    _gable(s, 4.0, 6.0, 3.0, 1.8, 27, 26, x0=w / 2 + 2.0, y0=-0.5, along_y=True)
    _row(s, "front", 3, w - 1.5, 3.2, 1.0, 1.2, w, d)
    _row(s, "front", 3, w - 1.5, 0.9, 1.0, 1.2, w, d, skip=(1,))
    _panel(s, "front", 0.0, 0.0, 1.1, 2.1, w, d, DOOR)
    _panel(s, "front", 0.0, 0.0, 1.4, 2.2, 4.0, 6.0, 0, x0=w / 2 + 2.0, y0=-0.5)
    return s, 21.0


def barn_a():
    """Big timber barn: brown plank walls, grey roof, wide dark doors."""
    s = Ship("barn_a")
    w, d, h = 16.0, 9.0, 5.0
    _walls(s, w, d, h, 14)
    _gable(s, w, d, h, 4.0, 21, 14, over=0.4)
    _panel(s, "front", 0.0, 0.0, 4.0, 4.2, w, d, 0)
    _panel(s, "back", 0.0, 0.0, 4.0, 4.2, w, d, 0)
    return s, 24.0


def barn_b():
    """Stone threshing barn with a red tile roof and a cart door."""
    s = Ship("barn_b")
    w, d, h = 15.0, 8.0, 5.5
    _walls(s, w, d, h, 26)
    _gable(s, w, d, h, 3.6, 27, 26, over=0.35)
    _panel(s, "front", 0.0, 0.0, 3.4, 4.0, w, d, 0)
    _row(s, "front", 4, w - 2.0, 3.6, 0.4, 0.9, w, d, col=0, skip=(1, 2))
    return s, 22.0


def dutch_barn():
    """Open-sided hay barn: posts, a curved roof, hay stacked inside."""
    s = Ship("dutch_barn")
    w, d, h = 14.0, 8.0, 5.0
    for x in (-w / 2, -w / 6, w / 6, w / 2):
        for y in (-d / 2, d / 2):
            s.strut((x, y, FOUND * 0.5), (x, y, h), 0.15, 30)
    n = 6
    arc = [(-(d / 2 + 0.3) + (d + 0.6) * k / n, h + 2.0 * math.sin(math.pi * k / n)) for k in range(n + 1)]
    for k in range(n):
        (y0, z0), (y1, z1) = arc[k], arc[k + 1]
        q = [s.vert(-w / 2 - 0.3, y0, z0), s.vert(w / 2 + 0.3, y0, z0),
             s.vert(w / 2 + 0.3, y1, z1), s.vert(-w / 2 - 0.3, y1, z1)]
        s.twoside(q, 21 if k % 2 else 30)
    s.block((-w / 2 + 0.5, w / 2 - 0.5, -d / 2 + 0.6, d / 2 - 0.6, -0.5),
            (-w / 2 + 0.5, w / 2 - 0.5, -d / 2 + 0.6, d / 2 - 0.6, 3.6), 5)
    return s, 18.0


def grain_silo():
    """Cylindrical grain silo with banding and a domed top."""
    s = Ship("grain_silo")
    alt = lambda a, b: (lambda k: a if k % 2 == 0 else b)
    s.stack([(FOUND, 2.5, 2.5), (3.0, 2.5, 2.5), (3.4, 2.55, 2.55), (7.0, 2.5, 2.5),
             (7.4, 2.55, 2.55), (11.0, 2.5, 2.5), (12.2, 1.6, 1.6), (12.8, 0.5, 0.5)],
            [alt(15, 8), 8, alt(15, 8), 8, alt(15, 8), 8, 8], n=10, top=8)
    return s, 22.0


def outbuilding_a():
    """Small stone shed with a lean-to slate roof."""
    s = Ship("outbuilding_a")
    w, d = 6.0, 4.0
    s.block((-w / 2, w / 2, -d / 2, d / 2, FOUND), (-w / 2, w / 2, -d / 2, d / 2, 2.4), 26)
    lo, hi = 2.4, 3.3
    q = [s.vert(-w / 2 - 0.3, d / 2 + 0.3, lo), s.vert(w / 2 + 0.3, d / 2 + 0.3, lo),
         s.vert(w / 2 + 0.3, -d / 2 - 0.3, hi), s.vert(-w / 2 - 0.3, -d / 2 - 0.3, hi)]
    s.face(q, 21, (0, 0, 0))
    s.face(q[::-1], 21, (0, 0, 10))
    for sx in (-1, 1):  # triangle infill between the walls and the slope
        s.face([s.vert(sx * w / 2, d / 2, lo), s.vert(sx * w / 2, -d / 2, lo),
                s.vert(sx * w / 2, -d / 2, hi)], 26, (0, 0, lo))
    s.block((-w / 2, w / 2, -d / 2, -d / 2 + 0.01, lo), (-w / 2, w / 2, -d / 2, -d / 2 + 0.01, hi), 26)
    _panel(s, "front", 0.8, 0.0, 1.0, 2.0, w, d, DOOR)
    return s, 9.0


def outbuilding_b():
    """Timber stable or store with a gable roof."""
    s = Ship("outbuilding_b")
    w, d, h = 7.0, 4.5, 2.8
    _walls(s, w, d, h, 14)
    _gable(s, w, d, h, 1.8, 21, 14)
    for a in (-2.0, 0.0, 2.0):
        _panel(s, "front", a, 0.0, 1.2, 2.2, w, d, 0)
    return s, 10.0


def greenhouse():
    """Glasshouse: low brick base, glass walls and roof."""
    s = Ship("greenhouse")
    w, d = 8.0, 4.0
    s.block((-w / 2, w / 2, -d / 2, d / 2, FOUND), (-w / 2, w / 2, -d / 2, d / 2, 0.6), 4)
    s.block((-w / 2 + 0.05, w / 2 - 0.05, -d / 2 + 0.05, d / 2 - 0.05, 0.6),
            (-w / 2 + 0.05, w / 2 - 0.05, -d / 2 + 0.05, d / 2 - 0.05, 2.2), 23)
    _gable(s, w - 0.1, d - 0.1, 2.2, 1.2, 10, 23, over=0.05)
    return s, 10.0


def windmill():
    """Tower mill: tapered white tower, timber cap, four sails."""
    s = Ship("windmill")
    s.stack([(FOUND, 3.0, 3.0), (0.0, 3.0, 3.0), (10.0, 2.0, 2.0), (10.6, 2.2, 2.2)],
            [15, 15, 26], n=8, rot=math.pi / 8)
    s.stack([(10.6, 2.0, 2.0), (12.4, 1.4, 1.4), (13.2, 0.4, 0.4)], [14, 14], n=8,
            rot=math.pi / 8, top=14)
    hub = (0.0, 2.6, 11.6)
    s.strut((0.0, 1.8, 11.6), hub, 0.25, 14)
    for k in range(4):
        a = math.pi / 4 + k * math.pi / 2
        tip = (math.cos(a) * 7.5, 2.7, 11.6 + math.sin(a) * 7.5)
        s.strut(hub, tip, 0.12, 14)
        # Lattice sail beside the stock: a flat panel, both sides.
        px, pz = -math.sin(a) * 0.9, math.cos(a) * 0.9
        q = [s.vert(math.cos(a) * 1.5, 2.75, 11.6 + math.sin(a) * 1.5),
             s.vert(tip[0], 2.75, tip[2]),
             s.vert(tip[0] + px, 2.75, tip[2] + pz),
             s.vert(math.cos(a) * 1.5 + px, 2.75, 11.6 + math.sin(a) * 1.5 + pz)]
        s.twoside(q, 15)
    _panel(s, "front", 0.0, 0.0, 1.0, 2.0, 5.6, 5.6, DOOR)
    return s, 20.0


def hay_bales_a():
    """A scatter of round hay bales lying on their sides."""
    s = Ship("hay_bales_a")
    for x, y, a in ((-1.6, 0.0, 0.0), (0.0, 0.6, 0.4), (1.6, -0.3, 0.1), (0.4, -1.4, 1.2)):
        c, sn = math.cos(a), math.sin(a)
        n = 8
        ring0, ring1 = [], []
        for k in range(n):
            t = 2 * math.pi * k / n
            yy, zz = 0.7 * math.cos(t), 0.7 + 0.7 * math.sin(t) - 0.05
            for ring, xx in ((ring0, -0.6), (ring1, 0.6)):
                ring.append(s.vert(x + xx * c - yy * sn, y + xx * sn + yy * c, zz))
        for k in range(n):
            j = (k + 1) % n
            s.face([ring0[k], ring0[j], ring1[j], ring1[k]], 5, (x, y, 0.65))
        s.face(ring0, 24, (x + 10 * c, y + 10 * sn, 0.65))
        s.face(ring1, 24, (x - 10 * c, y - 10 * sn, 0.65))
    return s, 6.0


def hay_bales_b():
    """A stack of rectangular bales, two layers."""
    s = Ship("hay_bales_b")
    for layer, (z0, xs) in enumerate(((-0.1, (-1.2, 0.0, 1.2)), (0.5, (-0.6, 0.6)))):
        for x in xs:
            for y in ((-0.5, 0.5) if layer == 0 else (0.0,)):
                s.block((x - 0.55, x + 0.55, y - 0.4, y + 0.4, z0),
                        (x - 0.55, x + 0.55, y - 0.4, y + 0.4, z0 + 0.6), 5)
    return s, 5.0


def tractor():
    """Parked farm tractor: red body, cab frame, big rear wheels."""
    s = Ship("tractor")
    s.block((-0.6, 0.6, 0.2, 2.2, 0.6), (-0.55, 0.55, 0.3, 2.1, 1.5), 16)          # bonnet
    s.block((-0.8, 0.8, -1.2, 0.3, 0.6), (-0.8, 0.8, -1.2, 0.3, 1.4), 16)          # body
    for x in (-0.75, 0.75):
        for y in (-1.15, 0.25):
            s.strut((x, y, 1.4), (x, y, 2.7), 0.05, 0)
    s.block((-0.85, 0.85, -1.25, 0.35, 2.7), (-0.85, 0.85, -1.25, 0.35, 2.8), 16)  # cab roof
    def wheel(x, y, r, wd):
        n = 8
        ring0 = [s.vert(x - wd / 2, y + r * math.cos(2 * math.pi * k / n), r + r * math.sin(2 * math.pi * k / n)) for k in range(n)]
        ring1 = [s.vert(x + wd / 2, y + r * math.cos(2 * math.pi * k / n), r + r * math.sin(2 * math.pi * k / n)) for k in range(n)]
        for k in range(n):
            j = (k + 1) % n
            s.face([ring0[k], ring0[j], ring1[j], ring1[k]], 0, (x, y, r))
        s.face(ring0, 17, (x + 5, y, r))
        s.face(ring1, 17, (x - 5, y, r))
    for sx in (-1, 1):
        wheel(sx * 1.05, -0.6, 0.85, 0.5)
        wheel(sx * 0.85, 1.7, 0.5, 0.35)
    return s, 6.0


# ---------------------------------------------------------------- village set
def cottage_a():
    """Thatched cottage: white walls, a deep hipped thatch roof."""
    s = Ship("cottage_a")
    w, d, h = 8.0, 6.0, 2.8
    _walls(s, w, d, h, 15)
    _hip(s, w, d, h, 3.4, 5, over=0.5)
    _chimney(s, 2.2, 0.0, 4.5, 6.6)
    _row(s, "front", 3, w - 1.0, 0.9, 0.9, 1.1, w, d, skip=(1,))
    _panel(s, "front", 0.0, 0.0, 0.9, 2.0, w, d, DOOR)
    return s, 14.0


def cottage_b():
    """Brick cottage with a slate gable roof."""
    s = Ship("cottage_b")
    w, d, h = 8.0, 6.0, 3.0
    _walls(s, w, d, h, 4)
    _gable(s, w, d, h, 2.6, 21, 4)
    _chimney(s, -w / 2 + 0.5, 0.0, h, h + 3.3, col=4)
    _row(s, "front", 3, w - 1.0, 1.0, 1.0, 1.1, w, d, skip=(1,))
    _panel(s, "front", 0.0, 0.0, 0.9, 2.1, w, d, DOOR)
    return s, 13.0


def cottage_c():
    """Stone cottage with a red tile roof and a lean-to porch."""
    s = Ship("cottage_c")
    w, d, h = 7.5, 5.5, 3.0
    _walls(s, w, d, h, 26)
    _gable(s, w, d, h, 2.6, 27, 26)
    _chimney(s, w / 2 - 0.5, 0.0, h, h + 3.2)
    s.block((-0.9, 0.9, d / 2, d / 2 + 1.2, FOUND), (-0.9, 0.9, d / 2, d / 2 + 1.2, 2.3), 26)
    s.block((-1.1, 1.1, d / 2, d / 2 + 1.4, 2.3), (-1.1, 1.1, d / 2, d / 2 + 1.4, 2.5), 27)
    _row(s, "front", 2, w - 1.5, 1.0, 1.0, 1.1, w, d)
    _panel(s, "front", 0.0, 0.0, 0.9, 2.0, 1.8, d + 2.4, DOOR)
    return s, 13.0


def house_a():
    """Two-storey brick house, slate roof, chimneys at both gables."""
    s = Ship("house_a")
    w, d, h = 9.0, 7.0, 5.6
    _walls(s, w, d, h, 4)
    _gable(s, w, d, h, 2.8, 21, 4)
    for x in (-w / 2 + 0.5, w / 2 - 0.5):
        _chimney(s, x, 0.0, h, h + 3.6, col=4)
    _row(s, "front", 3, w - 1.2, 3.3, 1.0, 1.3, w, d)
    _row(s, "front", 3, w - 1.2, 0.9, 1.0, 1.3, w, d, skip=(1,))
    _panel(s, "front", 0.0, 0.0, 1.0, 2.2, w, d, DOOR)
    return s, 17.0


def house_b():
    """Two-storey white house with a hipped tile roof."""
    s = Ship("house_b")
    w, d, h = 9.0, 7.5, 5.6
    _walls(s, w, d, h, 15)
    _hip(s, w, d, h, 2.8, 27)
    _chimney(s, 2.0, -1.0, h + 1.0, h + 4.0)
    _row(s, "front", 3, w - 1.2, 3.3, 1.0, 1.3, w, d)
    _row(s, "front", 3, w - 1.2, 0.9, 1.0, 1.3, w, d, skip=(1,))
    _panel(s, "front", 0.0, 0.0, 1.0, 2.2, w, d, DOOR)
    return s, 17.0


def house_c():
    """Stone house with a slate roof and a gabled front porch."""
    s = Ship("house_c")
    w, d, h = 9.5, 7.0, 5.4
    _walls(s, w, d, h, 26)
    _gable(s, w, d, h, 2.9, 21, 26)
    _chimney(s, -w / 2 + 0.5, 0.0, h, h + 3.7)
    _walls(s, 2.4, 1.6, 2.6, 26, y0=d / 2 + 0.8)
    _gable(s, 2.4, 1.6, 2.6, 1.2, 21, 26, y0=d / 2 + 0.8, along_y=True)
    _row(s, "front", 4, w - 1.0, 3.2, 0.9, 1.2, w, d, skip=(1, 2))
    _row(s, "front", 4, w - 1.0, 0.9, 0.9, 1.2, w, d, skip=(1, 2))
    _row(s, "front", 1, 0.0, 3.2, 0.9, 1.2, w, d)
    _panel(s, "front", 0.0, 0.0, 1.0, 2.1, 2.4, d + 3.2, DOOR)
    return s, 17.0


def _terrace(name, fronts, roof):
    """Three terraced houses in a row (15 m), one front colour each."""
    s = Ship(name)
    w, d, h, n = 15.0, 7.0, 5.8, 3
    hw = w / n
    for i, col in enumerate(fronts):
        x0 = -w / 2 + hw * (i + 0.5)
        _walls(s, hw, d, h, col, side=col, back=4, x0=x0)
        _panel(s, "front", -0.9, 0.0, 0.9, 2.2, hw, d, DOOR, x0=x0)
        _panel(s, "front", 0.9, 0.9, 1.1, 1.3, hw, d, GLASS, x0=x0)
        _row(s, "front", 2, hw - 1.4, 3.4, 0.9, 1.2, hw, d, x0=x0)
    _gable(s, w, d, h, 2.8, roof, 4)
    for i in range(n + 1):
        _chimney(s, -w / 2 + hw * i, 0.0, h + 1.5, h + 3.8, col=4, size=0.8)
    return s, 22.0


def terrace_row_a():
    """Brick terrace of three houses with a slate roof (street segment)."""
    return _terrace("terrace_row_a", (4, 4, 4), 21)


def terrace_row_b():
    """Painted terrace of three houses with a tile roof (street segment)."""
    return _terrace("terrace_row_b", (15, 9, 23), 27)


def _church(name, spire):
    """Stone church: a nave with a steep slate roof and a west tower,
    crenellated or topped with a spire."""
    s = Ship(name)
    nw, nd, nh = 8.0, 18.0, 7.0          # nave: narrow across, long front to back
    _walls(s, nw, nd, nh, 26)
    _gable(s, nw, nd, nh, 4.5, 21, 26, along_y=True)
    for y in (-5.0, -1.0, 3.0):          # lancet windows down both sides
        for face in ("left", "right"):
            _panel(s, face, y, 2.2, 1.0, 3.2, nw, nd, GLASS)
    _panel(s, "back", 0.0, 2.5, 2.0, 3.8, nw, nd, GLASS)  # east window
    tw, ty, th = 5.0, nd / 2 + 2.5, 15.0  # west tower at the front
    _walls(s, tw, tw, th, 26, y0=ty)
    _panel(s, "front", 0.0, 0.0, 1.6, 3.0, tw, tw, DOOR, y0=ty)
    for face in ("front", "left", "right"):
        _panel(s, face, 0.0, th - 3.2, 0.9, 1.8, tw, tw, 0, y0=ty)
    if spire:
        base = [s.vert(tw / 2 * math.cos(math.pi / 4 + k * math.pi / 2) * 1.41,
                       ty + tw / 2 * math.sin(math.pi / 4 + k * math.pi / 2) * 1.41, th)
                for k in range(4)]
        s.spike(base, (0.0, ty, th + 11.0), 21)
    else:
        for gx in (-1, 0, 1):            # battlements: corners and mid-edges
            for gy in (-1, 0, 1):
                if gx == 0 and gy == 0:
                    continue
                x, y = gx * 2.15, ty + gy * 2.15
                s.block((x - 0.35, x + 0.35, y - 0.35, y + 0.35, th),
                        (x - 0.35, x + 0.35, y - 0.35, y + 0.35, th + 1.0), 26)
    return s, 32.0


def church_a():
    """Stone church with a crenellated west tower."""
    return _church("church_a", spire=False)


def church_b():
    """Stone church with a west tower and spire."""
    return _church("church_b", spire=True)


def chapel():
    """Small stone chapel with a bellcote on the front gable."""
    s = Ship("chapel")
    w, d, h = 6.0, 10.0, 5.0
    _walls(s, w, d, h, 26)
    _gable(s, w, d, h, 3.0, 21, 26, along_y=True)
    s.block((-0.5, 0.5, d / 2 - 0.6, d / 2, h + 2.4), (-0.5, 0.5, d / 2 - 0.6, d / 2, h + 3.8), 26)
    _panel(s, "front", 0.0, 0.0, 1.2, 2.6, w, d, DOOR)
    for face in ("left", "right"):
        _row(s, face, 3, d - 3.0, 1.8, 0.8, 2.2, w, d)
    return s, 14.0


def pub():
    """Village inn: long white two-storey building, slate roof, hanging sign."""
    s = Ship("pub")
    w, d, h = 14.0, 8.0, 6.0
    _walls(s, w, d, h, 15)
    _gable(s, w, d, h, 3.0, 21, 15)
    for x in (-w / 2 + 0.6, 2.0, w / 2 - 0.6):
        _chimney(s, x, 0.0, h, h + 3.7)
    _row(s, "front", 5, w - 1.0, 3.6, 1.1, 1.3, w, d)
    _row(s, "front", 5, w - 1.0, 0.9, 1.1, 1.4, w, d, skip=(2,))
    _panel(s, "front", 0.0, 0.0, 1.2, 2.3, w, d, DOOR)
    s.strut((-w / 2 - 0.2, d / 2 + 0.2, 3.0), (-w / 2 - 0.2, d / 2 + 1.4, 3.0), 0.06, 0)
    s.twoside([s.vert(-w / 2 - 0.2, d / 2 + 0.5, 1.8), s.vert(-w / 2 - 0.2, d / 2 + 1.3, 1.8),
               s.vert(-w / 2 - 0.2, d / 2 + 1.3, 2.9), s.vert(-w / 2 - 0.2, d / 2 + 0.5, 2.9)], 16)
    return s, 21.0


def village_shop():
    """Brick shop with a wide shop window and a red awning; flat above."""
    s = Ship("village_shop")
    w, d, h = 8.0, 7.0, 5.6
    _walls(s, w, d, h, 4)
    _gable(s, w, d, h, 2.6, 21, 4)
    _chimney(s, w / 2 - 0.5, 0.0, h, h + 3.3, col=4)
    _panel(s, "front", -0.8, 0.5, 4.4, 1.8, w, d, 10)
    _panel(s, "front", 2.6, 0.0, 1.0, 2.2, w, d, DOOR)
    _row(s, "front", 2, w - 2.0, 3.4, 1.0, 1.2, w, d)
    s.twoside([s.vert(-3.4, d / 2 + 0.15, 2.9), s.vert(1.8, d / 2 + 0.15, 2.9),
               s.vert(1.8, d / 2 + 1.3, 2.4), s.vert(-3.4, d / 2 + 1.3, 2.4)], 16)
    return s, 14.0


def village_hall():
    """Single-storey brick hall with a slate roof and an entrance porch."""
    s = Ship("village_hall")
    w, d, h = 14.0, 8.0, 4.0
    _walls(s, w, d, h, 4)
    _gable(s, w, d, h, 2.6, 21, 4)
    _walls(s, 3.0, 2.0, 3.0, 4, y0=d / 2 + 1.0)
    _gable(s, 3.0, 2.0, 3.0, 1.3, 21, 4, y0=d / 2 + 1.0, along_y=True)
    _panel(s, "front", 0.0, 0.0, 1.4, 2.3, 3.0, d + 4.0, DOOR)
    _row(s, "front", 6, w - 1.0, 1.2, 1.0, 1.8, w, d, skip=(2, 3))
    for face in ("left", "right"):
        _row(s, face, 2, d - 3.0, 1.2, 1.0, 1.8, w, d)
    return s, 18.0


def war_memorial():
    """Stepped stone base with a tall cross, for the village green."""
    s = Ship("war_memorial")
    for z0, z1, r in ((FOUND * 0.25, 0.3, 1.6), (0.3, 0.6, 1.2), (0.6, 0.9, 0.8)):
        s.block((-r, r, -r, r, z0), (-r, r, -r, r, z1), 8)
    s.block((-0.3, 0.3, -0.3, 0.3, 0.9), (-0.22, 0.22, -0.22, 0.22, 4.2), 15)
    s.block((-0.9, 0.9, -0.15, 0.15, 3.1), (-0.9, 0.9, -0.15, 0.15, 3.5), 15)
    return s, 6.0


def _garden_wall(name, col, cap):
    """A 4 m stretch of low garden wall along +y (y -2..2), about 1 m
    high with a coping course; laid end to end along frontages."""
    s = Ship(name)
    s.block((-0.15, 0.15, -2.0, 2.0, -0.5), (-0.15, 0.15, -2.0, 2.0, 0.85), col)
    s.block((-0.2, 0.2, -2.0, 2.0, 0.85), (-0.2, 0.2, -2.0, 2.0, 1.0), cap)
    return s, 6.0


def garden_wall_a():
    """Low brick garden wall with a stone coping."""
    return _garden_wall("garden_wall_a", 4, 8)


def garden_wall_b():
    """Low stone garden wall."""
    return _garden_wall("garden_wall_b", 26, 26)


def picket_fence():
    """A 4 m stretch of white picket fence along +y, about 1 m high."""
    s = Ship("picket_fence")
    for y in (-2.0, 0.0):
        s.block((-0.06, 0.06, y - 0.06, y + 0.06, -0.4), (-0.06, 0.06, y - 0.06, y + 0.06, 1.05), 15)
    for z in (0.3, 0.75):
        s.twoside([s.vert(0.06, -2.0, z), s.vert(0.06, 2.0, z),
                   s.vert(0.06, 2.0, z + 0.08), s.vert(0.06, -2.0, z + 0.08)], 15)
    for k in range(10):
        y = -1.8 + 0.4 * k
        s.twoside([s.vert(0.08, y - 0.05, 0.05), s.vert(0.08, y + 0.05, 0.05),
                   s.vert(0.08, y + 0.05, 0.85), s.vert(0.08, y, 0.95),
                   s.vert(0.08, y - 0.05, 0.85)], 1)
    return s, 6.0


SHIPS = {
    "farmhouse_a": farmhouse_a, "farmhouse_b": farmhouse_b, "barn_a": barn_a, "barn_b": barn_b,
    "dutch_barn": dutch_barn, "grain_silo": grain_silo, "outbuilding_a": outbuilding_a,
    "outbuilding_b": outbuilding_b, "greenhouse": greenhouse, "windmill": windmill,
    "hay_bales_a": hay_bales_a, "hay_bales_b": hay_bales_b, "tractor": tractor,
    "cottage_a": cottage_a, "cottage_b": cottage_b, "cottage_c": cottage_c,
    "house_a": house_a, "house_b": house_b, "house_c": house_c,
    "terrace_row_a": terrace_row_a, "terrace_row_b": terrace_row_b,
    "church_a": church_a, "church_b": church_b, "chapel": chapel, "pub": pub,
    "village_shop": village_shop, "village_hall": village_hall, "war_memorial": war_memorial,
    "garden_wall_a": garden_wall_a, "garden_wall_b": garden_wall_b, "picket_fence": picket_fence,
}

_COMMON = """Settlement model (Settlements track R.4), drawn instanced. Origin at
ground level in the middle of the footprint; the front faces +Z (turned
towards the road by the game). Walls reach y = -2 so it sits cleanly on
a slope. Windows and doors stand 0.12 m proud of their walls.
{verts} verts, {tris} tris."""
_SEGMENT = """Settlement boundary segment (Settlements track R.4): a 4 m stretch
along +Z (z -2..2), laid end to end and stretched to fit like hedge
segments. Origin at ground level. {verts} verts, {tris} tris."""

HEADERS = {name: "terra-siege " + name.replace("_", " ") + " — " +
           (SHIPS[name].__doc__ or "").strip().split("\n")[0] + "\n" +
           (_SEGMENT if name in ("garden_wall_a", "garden_wall_b", "picket_fence") else _COMMON)
           for name in SHIPS}
