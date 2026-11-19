"""Enemy ship definitions. Each builder returns (Ship, preview ortho size)."""
import math
from shiplib import Ship, P

# ---------------------------------------------------------------- Fighter
def fighter():
    s = Ship("fighter"); s.ref = (0, 0, 0)
    s.loft([
        (3.2, [(0, 0.0)]),
        (2.0, [(0, 0.35), (0.3, 0.15), (0.3, -0.15), (0, -0.3)]),
        (0.3, [(0, 0.55), (0.45, 0.25), (0.45, -0.25), (0, -0.45)]),
        (-1.6, [(0, 0.45), (0.4, 0.2), (0.4, -0.25), (0, -0.4)]),
        (-2.0, [(0, 0.35), (0.32, 0.15), (0.32, -0.2), (0, -0.3)]),
    ], [[17, 17, 30], [16, 16, 30], [16, 16, 30], [22, 22, 22]], back=11)
    # canopy
    cf = P(s.vert(0, 1.7, 0.28)); ct = P(s.vert(0, 0.7, 0.78))
    cs = s.sym(0.27, 0.8, 0.42); cb = P(s.vert(0, -0.3, 0.5))
    s.tri_sym(cf, cs, ct, 17); s.tri_sym(cs, cb, ct, 0)
    # swept anhedral wings
    rf_t = s.sym(0.42, 0.5, 0.08); rb_t = s.sym(0.42, -1.4, 0.08)
    tf_t = s.sym(1.5, -0.9, -0.42); tb_t = s.sym(1.5, -1.6, -0.45)
    rf_b = s.sym(0.42, 0.5, -0.1); rb_b = s.sym(0.42, -1.4, -0.1)
    tf_b = s.sym(1.5, -0.9, -0.52); tb_b = s.sym(1.5, -1.6, -0.55)
    wref = lambda sx: (sx * 0.95, -0.8, -0.2)
    for i, sx in ((0, 1), (1, -1)):
        q = lambda *v: [x[i] for x in v]
        s.face(q(rf_t, tf_t, tb_t, rb_t), 16, wref(sx))
        s.face(q(rf_b, tf_b, tb_b, rb_b), 30, wref(sx))
        s.face(q(rf_t, rf_b, tf_b, tf_t), 17, wref(sx))   # leading edge
        s.face(q(rb_t, rb_b, tb_b, tb_t), 22, wref(sx))   # trailing edge
        s.face(q(tf_t, tf_b, tb_b, tb_t), 16, wref(sx))   # tip
    # canted tail fins
    for sx in (1, -1):
        a = s.vert(sx * 0.25, -0.9, 0.45); b = s.vert(sx * 0.25, -1.95, 0.32)
        c = s.vert(sx * 0.65, -2.05, 1.05); d = s.vert(sx * 0.6, -1.65, 1.05)
        s.twoside([a, b, c, d], 16)
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
    s = Ship("bomber"); s.ref = (0, 0, 0)
    s.loft([
        (3.5, [(0, 0.1), (0.35, 0.0), (0.35, -0.3), (0, -0.4)]),
        (2.2, [(0, 0.75), (0.8, 0.45), (0.9, -0.5), (0, -0.8)]),
        (-1.8, [(0, 0.85), (1.0, 0.5), (1.0, -0.55), (0, -0.85)]),
        (-3.0, [(0, 0.55), (0.7, 0.35), (0.7, -0.4), (0, -0.55)]),
    ], [[24, 25, 25], [24, 24, 25], [24, 24, 25]], front=17, back=25)
    # cockpit
    cf = P(s.vert(0, 2.7, 0.55)); ct = P(s.vert(0, 1.8, 1.05))
    cs = s.sym(0.55, 1.8, 0.6); cb = P(s.vert(0, 0.9, 0.85))
    s.tri_sym(cf, cs, ct, 17); s.tri_sym(cs, cb, ct, 25)
    # wings
    for sx in (1, -1):
        rf_t = s.vert(sx * 0.95, 1.4, 0.2); rb_t = s.vert(sx * 0.95, -1.6, 0.2)
        tf_t = s.vert(sx * 3.0, 0.1, 0.1); tb_t = s.vert(sx * 3.0, -1.3, 0.1)
        rf_b = s.vert(sx * 0.95, 1.4, -0.2); rb_b = s.vert(sx * 0.95, -1.6, -0.2)
        tf_b = s.vert(sx * 3.0, 0.1, -0.05); tb_b = s.vert(sx * 3.0, -1.3, -0.05)
        r = (sx * 2.0, -0.4, 0.05)
        s.face([rf_t, tf_t, tb_t, rb_t], 24, r); s.face([rf_b, tf_b, tb_b, rb_b], 25, r)
        s.face([rf_t, rf_b, tf_b, tf_t], 25, r); s.face([rb_t, rb_b, tb_b, tb_t], 22, r)
        s.face([tf_t, tf_b, tb_b, tb_t], 17, r)
        # underwing pod + pylon
        s.loft([
            (1.9, [(0, -0.75)]),
            (1.2, [(0, -0.4), (0.35, -0.75), (0, -1.1)]),
            (-1.4, [(0, -0.4), (0.35, -0.75), (0, -1.1)]),
            (-1.9, [(0, -0.55), (0.2, -0.75), (0, -0.95)]),
        ], [[25, 25], [25, 22], [22, 22]], cx=sx * 2.0, back=11)
        s.block((sx * 1.95, sx * 2.05, -0.9, 0.6, -0.45), (sx * 1.95, sx * 2.05, -0.9, 0.6, -0.04), 22)
        # tail fin
        a = s.vert(sx * 0.75, -1.4, 0.6); b = s.vert(sx * 0.7, -2.9, 0.45)
        c = s.vert(sx * 0.95, -3.15, 1.55); d = s.vert(sx * 0.95, -2.5, 1.55)
        s.twoside([a, b, c, d], 25)
    # bomb bay
    s.block((-0.5, 0.5, -1.2, 0.8, -0.95), (-0.55, 0.55, -1.25, 0.85, -0.78), {"top": 22, "bottom": 0, "front": 22, "back": 22, "left": 22, "right": 22})
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
    s = Ship("carrier"); s.ref = (0, 0, 0)
    s.loft([
        (6.0, [(0, 0.1), (0.5, 0.0), (0.6, -0.35), (0.4, -0.5), (0, -0.6)]),
        (3.5, [(0, 1.2), (2.6, 0.8), (3.1, -0.4), (2.3, -1.6), (0, -2.0)]),
        (-3.5, [(0, 1.3), (3.0, 0.9), (3.5, -0.4), (2.6, -1.7), (0, -2.1)]),
        (-6.0, [(0, 1.0), (2.6, 0.7), (3.0, -0.4), (2.1, -1.4), (0, -1.7)]),
    ], [[23, 21, 22, 22], [21, 21, 22, 22], [21, 21, 22, 22]], front=23, back=22)
    for sx in (1, -1):
        # hangar sponsons, dark mouths facing forward
        s.block((sx * 3.1, sx * 5.0, -4.2, 2.6, -1.0), (sx * 3.1, sx * 4.8, -4.0, 2.3, 0.5),
                {"top": 21, "bottom": 22, "front": 0, "back": 22, "left": 21, "right": 21})
        # engines on the rear face
        s.block((sx * 0.9, sx * 2.5, -6.25, -5.6, -1.0), (sx * 0.9, sx * 2.5, -6.25, -5.6, 0.3),
                {"top": 22, "bottom": 22, "front": 22, "back": 11, "left": 22, "right": 22})
    # bridge tower
    s.block((-1.3, 1.3, -4.4, -0.8, 1.0), (-0.8, 0.8, -3.9, -1.7, 2.4),
            {"top": 21, "bottom": 21, "front": 0, "back": 23, "left": 23, "right": 23})
    # antenna mast
    base = [s.vert(-0.12, -3.0, 2.3), s.vert(0.12, -3.0, 2.3), s.vert(0, -2.8, 2.3)]
    s.spike(base, (0, -3.0, 3.6), 15)
    # deck stripe
    s.block((-0.35, 0.35, 0.0, 3.2, 1.0), (-0.35, 0.35, 0.0, 3.2, 1.33), 23)
    return s, 16.0

# ---------------------------------------------------------------- Tank
def tank():
    s = Ship("tank"); s.ref = (0, 0, -0.7)
    # hull (ground is z = -1.6)
    s.loft([
        (1.75, [(0, -0.6), (0.95, -0.6), (0.95, -1.15), (0, -1.2)]),
        (0.9, [(0, -0.1), (0.95, -0.1), (0.95, -1.25), (0, -1.3)]),
        (-1.3, [(0, -0.1), (0.95, -0.1), (0.95, -1.25), (0, -1.3)]),
        (-1.5, [(0, -0.3), (0.9, -0.3), (0.9, -1.1), (0, -1.15)]),
    ], [[26, 22, 22], [26, 26, 22], [22, 22, 22]], front=26, back=22)
    # track units
    for sx in (1, -1):
        x0, x1 = sorted((sx * 0.95, sx * 1.5))
        s.block((x0, x1, -1.25, 1.35, -1.6), (x0, x1, -1.55, 1.75, -0.75),
                {"top": 26, "bottom": 0, "front": 0, "back": 0, "left": 22, "right": 22})
    # turret
    s.stack([(-0.15, 0.8, 0.9), (0.4, 0.75, 0.85), (0.75, 0.45, 0.55)],
            [27, 27], n=8, cy=-0.25, rot=math.pi / 8, top=27)
    # barrel + muzzle
    s.loft([
        (0.4, [(0, 0.5), (0.12, 0.43), (0.12, 0.27), (0, 0.2)]),
        (2.25, [(0, 0.5), (0.12, 0.43), (0.12, 0.27), (0, 0.2)]),
    ], [15, 15, 15])
    s.loft([
        (2.2, [(0, 0.55), (0.18, 0.47), (0.18, 0.23), (0, 0.15)]),
        (2.575, [(0, 0.55), (0.18, 0.47), (0.18, 0.23), (0, 0.15)]),
    ], [17, 17, 17], front=0, back=17)
    # antenna
    base = [s.vert(-0.45, -0.7, 0.6), s.vert(-0.38, -0.7, 0.6), s.vert(-0.42, -0.64, 0.6)]
    s.spike(base, (-0.42, -0.75, 1.6), 15)
    return s, 6.0

SHIPS = {"fighter": fighter, "drone": drone, "bomber": bomber,
         "seeder": seeder, "carrier": carrier, "tank": tank}

HEADERS = {
"fighter": """terra-siege fighter — fast interceptor.
Fuselage loft, yellow cabin, swept anhedral wings, canted twin tail fins,
orange exhaust. {verts} verts, {tris} tris.
Local frame: +X right, +Y up, +Z forward. c16 body, c17 nose/cabin/leading
edges, c30 belly, c22 trailing edges, c00 canopy rear, c11 exhaust.""",
"drone": """terra-siege drone — kamikaze swarm enemy.
Hexagonal bipyramid core with six spikes; the front spike (+Z) is red so
facing reads. White sensor pip on top. {verts} verts, {tris} tris.
Local frame: +Z forward. c18/c12 core facets, c15 spikes, c02 stinger, c01 pip.""",
"bomber": """terra-siege bomber — slow heavy.
Thick fuselage, yellow cockpit, straight wings, underwing pods with
orange exhausts, twin tail fins, dark bomb bay. {verts} verts, {tris} tris.
Local frame: +Z forward. c24 upper, c25 lower/pods, c17 nose/cockpit/wingtips,
c22 trailing edges/pylons, c11 exhausts, c00 bomb bay.""",
"seeder": """terra-siege seeder — slow drone-dispenser.
Octagonal saucer with a lavender rim band, a yellow forward window, a glowing
underside hatch, three dispenser tubes and twin side vanes. {verts} verts, {tris} tris.
Local frame: +Z forward. c19 hull, c22 vents/tubes, c20 rim/hatch/dome, c17 window, c01 top pip.""",
"carrier": """terra-siege carrier — boss-tier hull. Shields stay procedural (per-sector HP).
Lofted hull with pale prow, side hangar sponsons (dark mouths forward),
bridge tower, antenna, deck stripe, twin rear engines. {verts} verts, {tris} tris.
Local frame: +Z forward. c21 hull, c22 belly/rear, c23 prow/tower/stripe,
c00 hangar mouths/bridge window, c11 engines, c15 antenna.""",
"tank": """terra-siege tank — ground unit. Drives toward player.
Hull with sloped glacis, two track units, octagonal turret, barrel with
muzzle brake, antenna. {verts} verts, {tris} tris.
Local frame: +Z forward (rotates as one with yaw). c26 hull, c22 sides/tracks,
c00 track ends, c27 turret, c15 barrel/antenna, c17 muzzle.

Note: e.pos sits at TURRET_MOUNT_HEIGHT (1.6) above terrain in the
game, so the ground is at local Y = -1.6.""",
}
