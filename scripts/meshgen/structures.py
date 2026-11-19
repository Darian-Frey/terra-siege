"""Ground structures and vehicles: base control tower, Collector, Radar
Booster and Lander. Each builder returns (Ship, preview ortho size).

Each of these has parts the game still draws in code; the models leave
room for them (see the notes on each builder)."""
import math
from shiplib import Ship


def _alt(a, b):
    return lambda k: a if k % 2 == 0 else b


# ---------------------------------------------------------------- Base tower
def base():
    """Control tower, foot at z = 0, drawn at BASE_TOWER_SCALE (2x). The
    turret (base_turret, z 4.6-5.4, barrels out to r = 2.15) sits on the
    pedestal and rotates, so nothing may rise above the 4.6 pedestal top."""
    s = Ship("base")
    s.block((-1.25, 1.25, -1.25, 1.25, 0.0), (-1.2, 1.2, -1.2, 1.2, 0.4), 30)
    s.block((-1.0, 1.0, -1.0, 1.0, 0.4), (-0.85, 0.85, -0.85, 0.85, 3.0),
            {"top": 8, "bottom": 8, "front": 8, "back": 8, "left": 8, "right": 8})
    # vertical service stripes on the shaft
    for sx in (1, -1):
        x0, x1 = sorted((sx * 1.01, sx * 0.93))
        s.block((x0, x1, -0.15, 0.15, 0.5), (x0, x1, -0.15, 0.15, 2.9), 17)
    # control cab
    s.stack([(2.9, 0.95, 0.95), (3.2, 1.5, 1.5), (3.95, 1.5, 1.5), (4.25, 1.3, 1.3)],
            [15, 10, 15], n=8, rot=math.pi / 8, top=30)
    # turret pedestal under base_turret
    s.stack([(4.25, 0.75, 0.75), (4.6, 0.7, 0.7)], [8], n=8, rot=math.pi / 8, top=0)
    # roof corner lamps
    for sx in (1, -1):
        for sy in (1, -1):
            x, y = sx * 0.82, sy * 0.82
            s.block((x - 0.12, x + 0.12, y - 0.12, y + 0.12, 4.25),
                    (x - 0.1, x + 0.1, y - 0.1, y + 0.1, 4.45), 2)
    return s, 7.0


# ---------------------------------------------------------------- Base turret
# Hinge of the guns, ahead of the yaw axis (model units, before
# BASE_TOWER_SCALE). Mirrored by Config::BASE_TURRET_PIVOT_FWD.
GUN_PIVOT_Y = 0.5


def base_turret():
    """Rotating gun turret housing on top of the control tower, drawn at
    BASE_TOWER_SCALE and yawed to the aim. Origin on the turret's yaw axis
    at the barrel height (BASE_TURRET_BARREL_HEIGHT); the collar sits on
    the tower pedestal at z = -0.4. The guns are base_turret_guns, which
    also pitch, hinged at (0, GUN_PIVOT_Y, 0) at the front of the housing."""
    s = Ship("base_turret")
    s.stack([(-0.4, 0.72, 0.72), (-0.27, 0.7, 0.7)], [0], n=8, rot=math.pi / 8)
    s.stack([(-0.27, 0.68, 0.68), (0.22, 0.66, 0.66), (0.4, 0.46, 0.46)],
            [_alt(27, 9), _alt(9, 27)], n=8, rot=math.pi / 8, top=8)
    # trunnion brackets either side of the gun slot
    for sx in (1, -1):
        x0, x1 = sorted((sx * 0.34, sx * 0.44))
        s.block((x0, x1, GUN_PIVOT_Y - 0.14, GUN_PIVOT_Y + 0.12, -0.14),
                (x0, x1, GUN_PIVOT_Y - 0.14, GUN_PIVOT_Y + 0.12, 0.14), 0)
    # sensor pod with a lens, on the right of the roof
    s.block((0.3, 0.58, -0.3, 0.1, 0.32), (0.3, 0.56, -0.3, 0.08, 0.55),
            {"top": 30, "bottom": 30, "front": 10, "back": 30, "left": 30, "right": 30})
    return s, 5.0


# ---------------------------------------------------------------- Base platform
# Mirrors of Config values the platform bakes in. The game checks the
# loaded model's size against Config and warns if they drift; change both
# and regenerate.
PAD_HALF = 18.0         # BASE_PAD_HALF_SIZE
PAD_HEIGHT = 1.5        # BASE_PAD_HEIGHT
PAD_BURY = 1.0          # slab runs this far below the levelled ground
RAMP_LENGTH = 8.0       # BASE_RAMP_LENGTH
RAMP_WIDTH = 8.0        # BASE_RAMP_WIDTH
RAMP_SINK = 0.3         # BASE_RAMP_DRAW_SINK
EAST_SIGN = -1.0        # BASE_EAST_SIGN (world +X is west)
LIFT_SPOTS = [(-10.0, -10.0), (10.0, -10.0), (-11.0, 1.0)]  # BASE_DEFENDER_LIFT_SPOTS {east, north}
HATCH_W, HATCH_L, HATCH_RIM = 4.5, 6.5, 0.3  # BASE_DEFENDER_HATCH_W / _L, rim


def base_platform():
    """Raised landing platform under every base, origin at the centre of
    the deck (top at z = 0). The deck is one flat grid of faces: 6 m
    tiles in two tones, a darker centre landing square and the three lift
    hatches with yellow rims are all coplanar faces, never overlays, so
    nothing z-fights. Sides carry a yellow/black hazard band. Corner lamps
    stand on the deck. Ramps are base_ramp, drawn per base."""
    s = Ship("base_platform")
    h = PAD_HALF
    hatches = []
    for e, n in LIFT_SPOTS:
        x, y = EAST_SIGN * e, n  # authoring x = world X, authoring y = world Z
        hatches.append((x, y))
    xs, ys = {-h, h}, {-h, h}
    for k in range(-3, 4):
        xs.add(6.0 * k); ys.add(6.0 * k)
    for x, y in hatches:
        for d in (HATCH_W / 2, HATCH_W / 2 + HATCH_RIM):
            xs.update((x - d, x + d))
        for d in (HATCH_L / 2, HATCH_L / 2 + HATCH_RIM):
            ys.update((y - d, y + d))
    xs, ys = sorted(xs), sorted(ys)
    def colour(cx, cy):
        for x, y in hatches:
            dx, dy = abs(cx - x), abs(cy - y)
            if dx < HATCH_W / 2 and dy < HATCH_L / 2:
                return 22
            if dx < HATCH_W / 2 + HATCH_RIM and dy < HATCH_L / 2 + HATCH_RIM:
                return 17
        if abs(cx) < 6 and abs(cy) < 6:
            return 21
        return 30 if (int((cx + h) // 6) + int((cy + h) // 6)) % 2 == 0 else 8
    for i in range(len(xs) - 1):
        for j in range(len(ys) - 1):
            x0, x1, y0, y1 = xs[i], xs[i + 1], ys[j], ys[j + 1]
            c = colour((x0 + x1) / 2, (y0 + y1) / 2)
            s.face([s.vert(x0, y0, 0), s.vert(x1, y0, 0), s.vert(x1, y1, 0), s.vert(x0, y1, 0)],
                   c, (0, 0, -1))
    # sides: hazard band (2 m yellow/black segments) over concrete
    bot = -(PAD_HEIGHT + PAD_BURY)
    band = -0.4
    for side in range(4):
        for k in range(18):
            a, b = -h + 2 * k, -h + 2 * (k + 1)
            def pt(t, z):
                return {0: (t, -h, z), 1: (h, t, z), 2: (t, h, z), 3: (-h, t, z)}[side]
            cband = 17 if k % 2 == 0 else 0
            s.face([s.vert(*pt(a, 0)), s.vert(*pt(b, 0)), s.vert(*pt(b, band)), s.vert(*pt(a, band))],
                   cband, (0, 0, band / 2))
            s.face([s.vert(*pt(a, band)), s.vert(*pt(b, band)), s.vert(*pt(b, bot)), s.vert(*pt(a, bot))],
                   21, (0, 0, (band + bot) / 2))
    # corner lamps
    for sx in (1, -1):
        for sy in (1, -1):
            x, y = sx * (h - 1), sy * (h - 1)
            s.block((x - 0.35, x + 0.35, y - 0.35, y + 0.35, 0.0),
                    (x - 0.3, x + 0.3, y - 0.3, y + 0.3, 0.7), 17)
    return s, 40.0


def base_ramp():
    """One platform ramp, on the south side (-y, world -Z), origin at the
    platform deck centre so it is drawn at the platform's position; the
    game turns a copy to face east. Its slope carries on RAMP_SINK below
    the ground so it crosses the terrain at an angle (a flush foot
    z-fights); collision uses the true ramp, which ends at the foot."""
    s = Ship("base_ramp")
    h, w = PAD_HALF, RAMP_WIDTH / 2
    drop = PAD_HEIGHT + RAMP_SINK
    run = RAMP_LENGTH * drop / PAD_HEIGHT
    tA, tB = s.vert(-w, -h, 0), s.vert(w, -h, 0)
    fA, fB = s.vert(-w, -h - run, -drop), s.vert(w, -h - run, -drop)
    bA, bB = s.vert(-w, -h, -drop), s.vert(w, -h, -drop)
    s.face([tA, tB, fB, fA], 30, (0, -h - run / 2, -10))
    s.face([tA, fA, bA], 21, (10, -h, -drop / 2))
    s.face([tB, bB, fB], 21, (-10, -h, -drop / 2))
    return s, 14.0


def base_turret_guns():
    """The turret's elevating guns: a mantlet and twin barrels with yellow
    muzzles. Origin on the elevation hinge; the game places it at
    GUN_PIVOT_Y ahead of the turret axis, yawed and pitched to the aim.
    Muzzle ends at y = 1.65 (Config::BASE_TURRET_MUZZLE_REACH)."""
    s = Ship("base_turret_guns")
    s.block((-0.32, 0.32, -0.05, 0.28, -0.2), (-0.3, 0.3, -0.05, 0.25, 0.24),
            {"top": 8, "bottom": 30, "front": 30, "back": 30, "left": 30, "right": 30})
    for sx in (1, -1):
        s.strut((sx * 0.13, 0.25, 0.0), (sx * 0.13, 1.45, 0.0), 0.07, 15)
        s.block((sx * 0.13 - 0.1, sx * 0.13 + 0.1, 1.45, 1.65, -0.1),
                (sx * 0.13 - 0.1, sx * 0.13 + 0.1, 1.45, 1.65, 0.1), 17)
    return s, 4.0


# ---------------------------------------------------------------- Collector
def collector():
    """Friendly harvester. The cab is a separate model (collector_cab_*)
    sitting on the hull top at z = 0.55 across x, y in [-0.7, 0.7]."""
    s = Ship("collector")
    s.loft([
        (2.25, [(0, -0.1), (1.3, -0.1), (1.3, -0.55), (0, -0.55)]),
        (1.6, [(0, 0.55), (1.35, 0.5), (1.4, -0.6), (0, -0.6)]),
        (-2.0, [(0, 0.55), (1.35, 0.5), (1.4, -0.6), (0, -0.6)]),
        (-2.25, [(0, 0.4), (1.25, 0.35), (1.3, -0.5), (0, -0.5)]),
    ], [[28, 28, 14], [28, 28, 14], [28, 28, 14]], front=28, back=14)
    for sx in (1, -1):
        x0, x1 = sorted((sx * 1.45, sx * 2.05))
        s.block((x0, x1, -1.9, 1.9, -0.7), (x0, x1, -2.25, 2.25, -0.05),
                {"top": 14, "bottom": 0, "front": 0, "back": 0, "left": 14, "right": 14})
        # headlights
        x0, x1 = sorted((sx * 0.75, sx * 1.15))
        s.block((x0, x1, 2.0, 2.27, 0.05), (x0, x1, 2.0, 2.27, 0.3), 1)
    # front scoop (hazard yellow)
    s.block((-1.4, 1.4, 2.25, 2.95, -0.7), (-1.4, 1.4, 2.25, 2.4, -0.15),
            {"top": 17, "bottom": 0, "front": 17, "back": 17, "left": 17, "right": 17})
    # rear hopper
    s.block((-1.0, 1.0, -2.0, -0.85, 0.5), (-1.25, 1.25, -2.2, -0.75, 1.3),
            {"top": 14, "bottom": 28, "front": 28, "back": 28, "left": 28, "right": 28})
    return s, 6.5


def _collector_cab(name, body):
    """Collector cab in its cargo colour: a sloped-windscreen cabin on the
    hull top (z = 0.55) with a roof light bar. Same frame and origin as
    collector, so the game draws both at the same transform."""
    s = Ship(name)
    s.block((-0.7, 0.7, -0.6, 0.7, 0.55), (-0.62, 0.62, -0.55, 0.4, 1.2),
            {"top": body, "bottom": 28, "front": 10, "back": body, "left": body, "right": body})
    s.block((-0.3, 0.3, -0.1, 0.08, 1.2), (-0.28, 0.28, -0.08, 0.06, 1.32), 11)
    return s, 2.5


def collector_cab_empty():
    return _collector_cab("collector_cab_empty", 17)


def collector_cab_loaded():
    return _collector_cab("collector_cab_loaded", 29)


# ---------------------------------------------------------------- Radar Booster
def radar_booster():
    """Booster mast, foot at z = 0, drawn with yaw 0. The spinning dish
    (radar_booster_dish) sits on the bearing top at z = 3.3."""
    s = Ship("radar_booster")
    s.stack([(-0.2, 1.5, 1.5), (0.3, 1.5, 1.5), (0.5, 1.2, 1.2)], [30, 30],
            n=8, rot=math.pi / 8, top=30)
    s.stack([(0.5, 0.75, 0.75), (1.7, 0.6, 0.6), (2.9, 0.45, 0.45)],
            [_alt(21, 22), _alt(21, 23)], n=6, top=21)
    s.stack([(2.9, 0.6, 0.6), (3.3, 0.6, 0.6)], [8], n=8, rot=math.pi / 8, top=0)
    # equipment cabinets on the foundation
    s.block((0.75, 1.15, -0.4, 0.4, 0.5), (0.75, 1.1, -0.35, 0.35, 1.1),
            {"top": 21, "bottom": 21, "front": 21, "back": 21, "left": 21, "right": 17})
    s.block((-1.15, -0.75, -0.4, 0.4, 0.5), (-1.1, -0.75, -0.35, 0.35, 1.1),
            {"top": 21, "bottom": 21, "front": 21, "back": 21, "left": 17, "right": 21})
    # cable run up the mast
    s.strut((0.0, 0.62, 0.5), (0.0, 0.42, 2.9), 0.06, 0)
    return s, 6.0


def radar_booster_dish():
    """The booster's spinning radar dish. Origin on the mast axis at the
    bearing top (z = 3.3 on radar_booster); the game spins it with the
    entity's yaw. Turntable, post, a two-sided faceted bowl facing +y and
    three struts holding a yellow feed horn at the focus."""
    s = Ship("radar_booster_dish")
    s.stack([(0.0, 0.45, 0.45), (0.15, 0.4, 0.4)], [8], n=8, rot=math.pi / 8, top=30)
    s.block((-0.12, 0.12, -0.12, 0.12, 0.15), (-0.1, 0.1, -0.1, 0.1, 0.6), 30)
    cz, n = 0.65, 8
    def ring(r, y):
        return [s.vert(r * math.cos(2 * math.pi * k / n), y, cz + r * math.sin(2 * math.pi * k / n))
                for k in range(n)]
    back, front = (0, -3, cz), (0, 3, cz)
    for side, ref, cols in ((0, back, _alt(15, 23)), (1, front, _alt(8, 30))):
        rim, mid = ring(0.85, 0.55), ring(0.5, 0.33)
        apex = s.vert(0, 0.22, cz)
        for k in range(n):
            j = (k + 1) % n
            s.face([rim[k], rim[j], mid[j], mid[k]], cols(k), ref)
            s.face([mid[k], mid[j], apex], cols(k + 1), ref)
    focus = (0, 1.2, cz)
    for k in range(3):
        a = 2 * math.pi * k / 3 + math.pi / 2
        s.strut((0.8 * math.cos(a), 0.53, cz + 0.8 * math.sin(a)), focus, 0.03, 8)
    s.block((-0.12, 0.12, 1.15, 1.4, cz - 0.12), (-0.1, 0.1, 1.15, 1.4, cz + 0.12), 17)
    return s, 3.0


# ---------------------------------------------------------------- Lander
def lander():
    """Grounded enemy base. e.pos sits 0.6 above the terrain; the feet
    reach z = -1.1 so slopes never show a gap. Footprint about 14 x 14."""
    s = Ship("lander")
    R = 7.0
    s.stack([(0.6, 5.6, 5.6), (1.2, R, R), (3.4, R, R), (4.3, 6.0, 6.0)],
            [25, _alt(28, 26), 24], n=8, rot=math.pi / 8, bottom=22, top=25)
    ap = R * math.cos(math.pi / 8)  # apothem of the octagon's flat faces
    # drone-bay doors on the four axis faces
    for ang in (0, 90, 180, 270):
        c, sn = round(math.cos(math.radians(ang))), round(math.sin(math.radians(ang)))
        if c:
            x0, x1 = sorted((c * (ap - 0.05), c * (ap + 0.15)))
            s.block((x0, x1, -1.6, 1.6, 1.5), (x0, x1, -1.6, 1.6, 3.0), 0)
        else:
            y0, y1 = sorted((sn * (ap - 0.05), sn * (ap + 0.15)))
            s.block((-1.6, 1.6, y0, y1, 1.5), (-1.6, 1.6, y0, y1, 3.0), 0)
    # landing legs and feet
    for sx in (1, -1):
        for sy in (1, -1):
            s.strut((sx * 4.0, sy * 4.0, 1.0), (sx * 5.5, sy * 5.5, -0.6), 0.35, 26)
            s.block((sx * 5.5 - 0.8, sx * 5.5 + 0.8, sy * 5.5 - 0.8, sy * 5.5 + 0.8, -1.1),
                    (sx * 5.5 - 0.6, sx * 5.5 + 0.6, sy * 5.5 - 0.6, sy * 5.5 + 0.6, -0.5), 0)
            # deck warning lights
            x, y = sx * 3.7, sy * 3.7
            s.block((x - 0.3, x + 0.3, y - 0.3, y + 0.3, 4.3), (x - 0.25, x + 0.25, y - 0.25, y + 0.25, 4.7), 2)
    # central production silo with a glowing hatch
    s.stack([(4.3, 2.6, 2.6), (5.0, 2.6, 2.6), (7.2, 1.8, 1.8), (7.7, 1.2, 1.2)],
            [26, _alt(24, 25), 26], n=8, rot=math.pi / 8, top=29)
    # exhaust stacks
    for x, y in ((3.6, -1.2), (-3.6, -1.2)):
        s.stack([(4.3, 0.55, 0.55), (6.6, 0.45, 0.45)], [26], n=6, cx=x, cy=y, top=0)
    # forward command bridge
    s.block((-1.5, 1.5, 3.0, 4.6, 4.3), (-1.3, 1.3, 3.1, 4.3, 5.4),
            {"top": 24, "bottom": 24, "front": 17, "back": 24, "left": 24, "right": 24})
    return s, 20.0


SHIPS = {"base": base, "base_platform": base_platform, "base_ramp": base_ramp,
         "base_turret": base_turret,
         "base_turret_guns": base_turret_guns, "collector": collector,
         "collector_cab_empty": collector_cab_empty,
         "collector_cab_loaded": collector_cab_loaded,
         "radar_booster": radar_booster, "radar_booster_dish": radar_booster_dish, "lander": lander}

HEADERS = {
"base": """terra-siege base — Collector delivery destination + defensive turret.
Control tower: plinth, tapered shaft with yellow service stripes, octagonal
glazed control cab, roof lamps and a turret pedestal. {verts} verts, {tris} tris.
Foot at y = 0; drawn at BASE_TOWER_SCALE at the platform's NE corner.
The platform, ramps and corner lights are procedural and the turret is
base_turret.obj — see EntityManager::renderEnemy (Base); nothing here
rises above y = 4.6 so the rotating barrels stay clear. Local frame: +Z forward.
c30 plinth/roof, c08 shaft/pedestal, c17 stripes, c15 cab, c10 glazing,
c02 lamps, c00 pedestal top.""",
"base_platform": """terra-siege base platform — raised landing apron under every base.
36 m square slab, deck at y = 0 (the origin), sides down to y = -2.5
(1 m below the levelled ground). The deck is a flat grid of coplanar
faces: two-tone 6 m tiles, a dark centre landing square, and three
defender lift hatches with yellow rims. Yellow/black hazard band round
the sides; corner lamps on the deck. {verts} verts, {tris} tris.
Bakes BASE_PAD_HALF_SIZE, BASE_PAD_HEIGHT, BASE_DEFENDER_LIFT_SPOTS and the
hatch sizes — the game warns at load if its size drifts from Config.
Drawn at the base position, unrotated. c30/c08 tiles, c21 centre square
and sides, c22 hatches, c17 rims/band/lamps, c00 band.""",
"base_ramp": """terra-siege base ramp — one platform ramp, south side.
Sloped deck from the platform edge down past the ground, with side
wedges. {verts} verts, {tris} tris. Origin at the platform deck centre;
drawn at the base position, and turned 90 degrees for the east ramp.
Bakes BASE_PAD_HALF_SIZE, BASE_PAD_HEIGHT, BASE_RAMP_LENGTH / _WIDTH and
BASE_RAMP_DRAW_SINK. c30 deck, c21 sides.""",
"base_turret": """terra-siege base turret housing — rotating mount on the control tower.
Octagonal rust-red housing on a dark collar, trunnion brackets and a
sensor pod. {verts} verts, {tris} tris. The guns are base_turret_guns.
Origin on the yaw axis at the barrel height; drawn at the tower's x/z,
BASE_TURRET_BARREL_HEIGHT above the platform, BASE_TOWER_SCALE, yawed to
the aim. Local frame: +Z = aim. c27/c09 housing, c08 roof, c00 collar and
brackets, c30 pod, c10 lens.""",
"base_turret_guns": """terra-siege base turret guns — the elevating part of the turret.
Mantlet and twin barrels with yellow muzzles. {verts} verts, {tris} tris.
Origin on the elevation hinge, placed 0.5 ahead of the turret axis
(BASE_TURRET_PIVOT_FWD) and drawn yawed and pitched to the aim at
BASE_TOWER_SCALE. Muzzles end 1.65 ahead of the hinge.
Local frame: +Z = barrels. c08 mantlet top, c30 mantlet, c15 barrels,
c17 muzzles.""",
"collector": """terra-siege collector — friendly ground vehicle.
Hull with a yellow front scoop, two track units, headlights and a rear
hopper. {verts} verts, {tris} tris. The cab is collector_cab_empty /
collector_cab_loaded (its colour shows cargo), drawn at the same transform
on the hull top at y = 0.55.
Local frame: +Z forward. c28 hull, c14 tracks/hopper load, c17 scoop,
c01 headlights, c00 track ends.""",
"collector_cab_empty": """terra-siege collector cab, empty (hazard yellow).
Cabin with a sloped blue windscreen and an orange roof light bar.
{verts} verts, {tris} tris. Drawn at the collector's transform when it
carries no cargo; collector_cab_loaded is the same in green.
Local frame: +Z forward. c17 body, c10 windscreen, c28 floor, c11 light bar.""",
"collector_cab_loaded": """terra-siege collector cab, loaded (green).
Cabin with a sloped blue windscreen and an orange roof light bar.
{verts} verts, {tris} tris. Drawn at the collector's transform while it
carries cargo; collector_cab_empty is the same in yellow.
Local frame: +Z forward. c29 body, c10 windscreen, c28 floor, c11 light bar.""",
"radar_booster": """terra-siege radar booster — extends radar range while alive.
Octagonal foundation, tapered six-sided mast, bearing housing, two
equipment cabinets and a cable run. {verts} verts, {tris} tris.
The spinning dish is radar_booster_dish, on the bearing top at y = 3.3.
Local frame: +Z forward, foot at y = 0.
c30 foundation, c21/c22/c23 mast, c08 bearing, c17 cabinet panels, c00 cable.""",
"radar_booster_dish": """terra-siege radar booster dish — spins on top of the booster mast.
Turntable, post, a two-sided eight-facet bowl facing +Z, and three struts
holding a yellow feed horn at the focus. {verts} verts, {tris} tris.
Origin on the mast axis at the bearing top; drawn at RADAR_BOOSTER_DISH_HEIGHT
above the booster's foot, yawed with the entity's spin.
c15/c23 bowl face, c08/c30 bowl back and struts, c30 post, c17 feed horn.""",
"lander": """terra-siege lander — grounded enemy base (Slice C C.3).
Octagonal hull with four drone-bay doors, four landing legs, deck
warning lights, a production silo with a glowing hatch, two exhaust
stacks and a forward command bridge. {verts} verts, {tris} tris.
e.pos sits 0.6 above the terrain; feet reach y = -1.1. Shields are
procedural. Local frame: +Z forward. c28/c26/c24/c25 hull, c22 underside,
c00 doors/feet/stack tops, c02 lights, c29 hatch, c17 bridge window.""",
}
