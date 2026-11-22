"""Enemy ship definitions. Each builder returns (Ship, preview ortho size).

All enemies share one alien palette so they read as a single faction:
c19/c18/c12 purple and violet hulls, c20 lavender highlights, c22 dark
undersides, c07 cyan glow (eyes, cores, hover pads), c06 magenta drives.
No wings, tail fins or cockpits — those read as human aircraft."""
import math
from mathutils import Vector
from shiplib import Ship, P


def ribbon(s, spine, widths, thick, top, bot, edge):
    """Flat curved blade along spine [(x, y, z)], half-widths across it in
    the xy plane and half-thickness in z (scalars or per-point lists).
    top may be a per-segment list of colours."""
    n = len(spine)
    th = thick if isinstance(thick, (list, tuple)) else [thick] * n
    rows = []
    for i, p in enumerate(spine):
        a, b = Vector(spine[max(i - 1, 0)]), Vector(spine[min(i + 1, n - 1)])
        t = Vector((b.x - a.x, b.y - a.y)).normalized()
        nx, ny = -t.y * widths[i], t.x * widths[i]
        x, y, z = p
        rows.append([s.vert(x + nx, y + ny, z + th[i]), s.vert(x - nx, y - ny, z + th[i]),
                     s.vert(x - nx, y - ny, z - th[i]), s.vert(x + nx, y + ny, z - th[i])])
    for i in range(n - 1):
        r0, r1 = rows[i], rows[i + 1]
        ref = tuple((spine[i][k] + spine[i + 1][k]) / 2 for k in range(3))
        tc = top[i] if isinstance(top, (list, tuple)) else top
        s.face([r0[0], r0[1], r1[1], r1[0]], tc, ref)
        s.face([r0[3], r0[2], r1[2], r1[3]], bot, ref)
        s.face([r0[1], r0[2], r1[2], r1[1]], edge, ref)
        s.face([r0[0], r0[3], r1[3], r1[0]], edge, ref)
    for row, i, j in ((rows[0], 0, 1), (rows[-1], n - 1, n - 2)):
        out = tuple(2 * spine[i][k] - spine[j][k] for k in range(3))
        r = tuple(2 * spine[i][k] - out[k] for k in range(3))
        s.face(row, edge, r)


def mirror(pts):
    return [(-x, y, z) for x, y, z in pts]


def pip(s, x, y, z, r, h, col, n=3):
    """Small upward pyramid (glow spot / spine) standing on (x, y, z)."""
    base = [s.vert(x + r * math.cos(2 * math.pi * k / n + 0.5), y + r * math.sin(2 * math.pi * k / n + 0.5), z)
            for k in range(n)]
    s.spike(base, (x, y, z + h), col)


# ---------------------------------------------------------------- Fighter
def fighter():
    """Claw interceptor: a diamond-section core with a cyan eye and magenta
    drive, flanked by two crescent blades whose tips curl forward."""
    s = Ship("fighter"); s.ref = (0, 0, 0)
    s.loft([
        (2.1, [(0, 0.0)]),
        (1.2, [(0, 0.42), (0.45, 0.0), (0, -0.35)]),
        (-0.4, [(0, 0.62), (0.62, 0.0), (0, -0.45)]),
        (-1.9, [(0, 0.32), (0.36, 0.0), (0, -0.25)]),
    ], [[7, 22], [19, 22], [12, 22]], back=6)
    claw = [(0.3, -1.3, 0.0), (1.05, -1.1, -0.05), (1.5, -0.2, -0.1),
            (1.45, 1.1, -0.1), (1.1, 2.3, -0.05), (0.75, 3.0, 0.0)]
    w = [0.42, 0.4, 0.32, 0.25, 0.14, 0.0]
    t = [0.16, 0.14, 0.11, 0.08, 0.05, 0.02]
    for spine in (claw, mirror(claw)):
        ribbon(s, spine, w, t, [19, 18, 18, 18, 20], 22, 20)
    pip(s, 0, -0.6, 0.55, 0.22, 0.45, 20)
    return s, 7.0

# ---------------------------------------------------------------- Drone
def drone():
    s = Ship("drone")
    n, R, zr = 6, 0.62, 0.15
    ring = [s.vert(R * math.cos(math.pi / 2 + 2 * math.pi * k / n),
                   R * math.sin(math.pi / 2 + 2 * math.pi * k / n), zr) for k in range(n)]
    top = s.vert(0, 0, 0.9); bot = s.vert(0, 0, -0.5)
    s.ref = (0, 0, zr)
    for k in range(n):
        j = (k + 1) % n
        s.face([ring[k], ring[j], top], 18 if k % 2 == 0 else 12)
        s.face([ring[k], ring[j], bot], 12 if k % 2 == 0 else 18)
    # spikes from each ring vertex; the front one (k=0, +Y) is the red stinger
    for k in range(n):
        a = math.pi / 2 + 2 * math.pi * k / n
        cx, cy = R * math.cos(a), R * math.sin(a)
        tx, ty = -math.sin(a) * 0.14, math.cos(a) * 0.14
        base = [s.vert(cx * 0.8 + tx, cy * 0.8 + ty, zr - 0.08),
                s.vert(cx * 0.8 - tx, cy * 0.8 - ty, zr - 0.08),
                s.vert(cx * 0.8, cy * 0.8, zr + 0.16)]
        L = 1.45 if k == 0 else 1.3
        s.spike(base, (cx * L, cy * L, zr), 2 if k == 0 else 15)
    # white sensor pip on top
    pip = [s.vert(0.12 * math.cos(2 * math.pi * k / 3), 0.12 * math.sin(2 * math.pi * k / 3), 0.78) for k in range(3)]
    s.spike(pip, (0, 0, 1.05), 1)
    return s, 3.2

# ---------------------------------------------------------------- Bomber
def bomber():
    """Brood pod: a ribbed egg-shaped body with a glowing maw, drooping
    membrane vanes, dorsal spines and three egg sacs slung underneath."""
    s = Ship("bomber"); s.ref = (0, 0, 0)
    def egg(r, sq=0.75):
        return [(0, r * sq), (r * 0.7, r * 0.7 * sq), (r, 0), (r * 0.7, -r * 0.7 * sq), (0, -r * sq)]
    rib_a, rib_b = [19, 19, 22, 22], [18, 12, 22, 22]
    s.loft([
        (3.3, [(0, 0.0)]),
        (2.6, egg(0.8)), (1.6, egg(1.3)), (0.2, egg(1.5)),
        (-1.2, egg(1.4)), (-2.4, egg(1.0)), (-3.2, egg(0.5)),
    ], [[7, 7, 12, 22], rib_a, rib_b, rib_a, rib_b, rib_a], back=6)
    vane = [(1.2, 0.9, 0.25), (2.1, 0.3, 0.1), (2.8, -0.7, -0.35), (3.0, -1.8, -0.85)]
    for spine in (vane, mirror(vane)):
        ribbon(s, spine, [0.65, 0.6, 0.42, 0.0], [0.1, 0.08, 0.06, 0.03], 19, 22, 18)
    for y in (1.0, -0.4, -1.8):
        pip(s, 0, y, 1.0 if y > -1 else 0.9, 0.25, 0.6, 20)
    for x, y in ((0, 0.6), (0.9, -1.2), (-0.9, -1.2)):
        s.stack([(-1.75, 0.18, 0.18), (-1.5, 0.36, 0.36), (-1.1, 0.3, 0.3), (-0.8, 0.15, 0.15)],
                [20, 20, 19], n=6, cx=x, cy=y, bottom=7)
    return s, 9.0

# ---------------------------------------------------------------- Seeder
def seeder():
    s = Ship("seeder")
    alt = lambda a, b: (lambda k: a if k % 2 == 0 else b)
    front = lambda base, f: (lambda k: f if k == 1 else base)  # facet 1 is centred on +Y
    s.stack([
        (-0.9, 1.3, 1.0),
        (-0.5, 2.6, 1.8),
        (0.0, 3.0, 2.2),
        (0.35, 2.6, 1.9),
        (0.65, 1.5, 1.2),
        (1.05, 1.0, 0.8),
        (1.4, 0.45, 0.35),
    ], [alt(19, 22), 19, 20, 19, front(19, 17), 20], n=8, rot=math.pi / 8,
        bottom=20, top=1)
    # three dispenser tubes hanging under the hull
    for x, y in ((0, 0.6), (0.75, -0.45), (-0.75, -0.45)):
        s.stack([(-1.25, 0.28, 0.28), (-0.7, 0.32, 0.32)], [22], n=6, cx=x, cy=y, bottom=20)
    # side vanes
    for sx in (1, -1):
        a = s.vert(sx * 2.4, 0.6, 0.25); b = s.vert(sx * 2.4, -1.2, 0.25)
        c = s.vert(sx * 2.9, -1.6, 1.0); d = s.vert(sx * 2.85, -0.6, 1.0)
        s.twoside([a, b, c, d], 19)
    return s, 8.0

# ---------------------------------------------------------------- Carrier
def carrier():
    """Manta mothership: a broad ribbed ray hull with forward-curling
    horns, a crystal crest, rows of cyan glow spots, two drone-bay tubes
    underneath and a magenta drive with a tail spike."""
    s = Ship("carrier"); s.ref = (0, 0, 0)
    def wing(w, t):
        return [(0, t), (w * 0.35, t * 0.8), (w, 0), (w * 0.35, -t * 0.7), (0, -t)]
    rib_a, rib_b = [19, 18, 22, 22], [12, 19, 22, 22]
    secs = [(5.2, 1.3, 0.5), (3.4, 3.6, 1.4), (1.0, 5.0, 2.0),
            (-1.6, 4.6, 2.1), (-3.8, 2.4, 1.6), (-5.2, 1.0, 0.8)]
    s.loft([(y, wing(w, t)) for y, w, t in secs],
           [rib_a, rib_b, rib_a, rib_b, rib_a], front=22, back=6)
    def deck(x, y):
        """Hull top height at (x, y), so studs sit on the surface."""
        for (y0, w0, t0), (y1, w1, t1) in zip(secs, secs[1:]):
            if y1 <= y <= y0:
                f = (y0 - y) / (y0 - y1)
                w, t = w0 + (w1 - w0) * f, t0 + (t1 - t0) * f
                x = abs(x)
                if x <= 0.35 * w: return t - 0.2 * t * x / (0.35 * w)
                return 0.8 * t * (w - x) / (0.65 * w)
    horn = [(1.0, 4.3, 0.2), (2.0, 5.1, 0.1), (2.35, 6.1, 0.0), (1.8, 7.0, -0.1)]
    for spine in (horn, mirror(horn)):
        ribbon(s, spine, [0.6, 0.48, 0.3, 0.0], [0.3, 0.24, 0.15, 0.05], 18, 22, 20)
    s.spike([s.vert(0.35, -5.2, 0.2), s.vert(-0.35, -5.2, 0.2), s.vert(0, -5.2, -0.4)], (0, -7.0, -0.1), 20)
    crest = [s.vert(0.5 * math.cos(2 * math.pi * k / 6), -0.6 + 0.9 * math.sin(2 * math.pi * k / 6),
                    deck(0, -0.6) - 0.3) for k in range(6)]
    s.spike(crest, (0, -1.0, 3.6), 20)
    for x, y in ((0, 3.0), (0, 1.8), (0, -2.8), (2.1, 1.4), (3.1, 0.0), (1.9, -2.3)):
        for sx in ((1, -1) if x else (1,)):
            pip(s, sx * x, y, deck(x, y) - 0.15, 0.5, 0.65, 7)
    for sx in (1, -1):
        s.stack([(-2.3, 0.55, 0.55), (-1.4, 0.7, 0.7)], [22], n=6, cx=sx * 1.6, cy=1.0, bottom=0)
    return s, 16.0

# ---------------------------------------------------------------- Tank
def tank():
    """Hover crawler: a hexagonal hull floating on four cyan hover pads,
    a domed core and twin crystal prongs cradling a cyan orb, with a crystal antenna
    at the back. No tracks or gun barrel."""
    s = Ship("tank"); s.ref = (0, 0, -0.7)
    # hull (ground is z = -1.6)
    s.stack([(-1.25, 1.1, 1.5), (-0.9, 1.45, 1.95), (-0.45, 1.3, 1.8), (-0.2, 0.8, 1.15)],
            [22, 19, 18], n=6, rot=math.pi / 2, bottom=22, top=19)
    for x, y in ((1.05, 1.05), (-1.05, 1.05), (1.05, -1.05), (-1.05, -1.05)):
        s.stack([(-1.42, 0.34, 0.34), (-1.15, 0.42, 0.42)], [22], n=6, cx=x, cy=y, bottom=7)
    s.stack([(-0.22, 0.75, 0.75), (0.2, 0.65, 0.65), (0.6, 0.3, 0.3)], [19, 20],
            n=6, cy=-0.3, top=7)
    def diamond(r, zc=0.3):
        return [(0, zc + r), (r, zc), (0, zc - r)]
    for sx in (1, -1):
        s.loft([(-0.1, diamond(0.2)), (0.9, diamond(0.24)), (2.4, [(0, 0.25)])],
               [[20, 12], [20, 12]], cx=sx * 0.42)
    orb = [s.vert(0.22 * math.cos(2 * math.pi * k / 5), 1.5 + 0.22 * math.sin(2 * math.pi * k / 5), 0.3)
           for k in range(5)]
    s.spike(orb, (0, 1.5, 0.62), 7); s.spike(orb, (0, 1.5, -0.02), 7)
    for sx in (1, -1):
        for y in (0.6, -0.7):
            base = [s.vert(sx * 1.3, y + 0.2, -0.7), s.vert(sx * 1.3, y - 0.2, -0.7), s.vert(sx * 1.3, y, -0.45)]
            s.spike(base, (sx * 1.9, y, -1.15), 20)
    ant = [s.vert(0.12 * math.cos(2 * math.pi * k / 3), -1.2 + 0.12 * math.sin(2 * math.pi * k / 3), -0.25)
           for k in range(3)]
    s.spike(ant, (0, -1.3, 1.5), 20)
    return s, 6.0

SHIPS = {"fighter": fighter, "drone": drone, "bomber": bomber,
         "seeder": seeder, "carrier": carrier, "tank": tank}

HEADERS = {
"fighter": """terra-siege fighter — fast interceptor (alien claw).
Diamond-section core with a cyan eye and magenta drive, two crescent
blades curling forward, a lavender dorsal spine. {verts} verts, {tris} tris.
Local frame: +X right, +Y up, +Z forward. c19/c18/c12 hull, c20 edges and
spine, c22 undersides, c07 eye, c06 drive.""",
"drone": """terra-siege drone — kamikaze swarm enemy.
Hexagonal bipyramid core with six spikes; the front spike (+Z) is red so
facing reads. White sensor pip on top. {verts} verts, {tris} tris.
Local frame: +Z forward. c18/c12 core facets, c15 spikes, c02 stinger, c01 pip.""",
"bomber": """terra-siege bomber — slow heavy (alien brood pod).
Ribbed egg body with a cyan maw, drooping membrane vanes, three dorsal
spines and three egg sacs underneath with glowing tips.
{verts} verts, {tris} tris.
Local frame: +Z forward. c19/c18/c12 ribs, c22 undersides, c20 spines and
sacs, c07 maw and sac tips, c06 drive.""",
"seeder": """terra-siege seeder — slow drone-dispenser.
Octagonal saucer with a lavender rim band, a yellow forward window, a glowing
underside hatch, three dispenser tubes and twin side vanes. {verts} verts, {tris} tris.
Local frame: +Z forward. c19 hull, c22 vents/tubes, c20 rim/hatch/dome, c17 window, c01 top pip.""",
"carrier": """terra-siege carrier — boss-tier hull (alien manta). Shields stay procedural (per-sector HP).
Broad ribbed ray hull, forward-curling horns, crystal crest, cyan glow
spots, two drone-bay tubes underneath (dark mouths), magenta drive and
tail spike. {verts} verts, {tris} tris.
Local frame: +Z forward. c19/c18/c12 ribs, c22 belly, c20 horns/crest/tail,
c07 glow spots, c00 bay mouths, c06 drive.""",
"tank": """terra-siege tank — ground unit (alien hover crawler). Drives toward player.
Hexagonal hull on four cyan hover pads, domed core, twin crystal
prongs with a cyan orb, side claws and a crystal antenna. {verts} verts, {tris} tris.
Local frame: +Z forward (rotates as one with yaw). c19/c18 hull, c22
underside/pads, c20 dome/prongs/claws/antenna, c12 prong bellies,
c07 pad glow, dome cap and orb.

Note: e.pos sits at TURRET_MOUNT_HEIGHT (1.6) above terrain in the
game, so the ground is at local Y = -1.6.""",
}
