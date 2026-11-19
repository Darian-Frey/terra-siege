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
    turret cap (z 4.6-5.4) and its barrel (out to r = 2.0) are procedural,
    so nothing may rise above the 4.6 pedestal top."""
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
    # turret pedestal under the procedural cap
    s.stack([(4.25, 0.75, 0.75), (4.6, 0.7, 0.7)], [8], n=8, rot=math.pi / 8, top=0)
    # roof corner lamps
    for sx in (1, -1):
        for sy in (1, -1):
            x, y = sx * 0.82, sy * 0.82
            s.block((x - 0.12, x + 0.12, y - 0.12, y + 0.12, 4.25),
                    (x - 0.1, x + 0.1, y - 0.1, y + 0.1, 4.45), 2)
    return s, 7.0


# ---------------------------------------------------------------- Collector
def collector():
    """Friendly harvester. The cargo-coloured cab is procedural: a
    1.4 x 0.6 x 1.4 box centred at z = 0.9, so the hull top stays at
    0.55 across x, y in [-0.7, 0.7]."""
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


# ---------------------------------------------------------------- Radar Booster
def radar_booster():
    """Booster mast, foot at z = 0, drawn with yaw 0. The spinning dish
    (z 3.35-3.85, swept out to r ~ 1.9) is procedural, so the mast stays
    below 3.3."""
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


SHIPS = {"base": base, "collector": collector,
         "radar_booster": radar_booster, "lander": lander}

HEADERS = {
"base": """terra-siege base — Collector delivery destination + defensive turret.
Control tower: plinth, tapered shaft with yellow service stripes, octagonal
glazed control cab, roof lamps and a turret pedestal. {verts} verts, {tris} tris.
Foot at y = 0; drawn at BASE_TOWER_SCALE at the platform's NE corner.
The platform, ramps, corner lights and the turret cap/barrel/tip are
procedural — see EntityManager::renderEnemy (Base); nothing here rises
above y = 4.6 so the rotating barrel stays clear. Local frame: +Z forward.
c30 plinth/roof, c08 shaft/pedestal, c17 stripes, c15 cab, c10 glazing,
c02 lamps, c00 pedestal top.""",
"collector": """terra-siege collector — friendly ground vehicle.
Hull with a yellow front scoop, two track units, headlights and a rear
hopper. {verts} verts, {tris} tris. The cab stays procedural (its colour
changes with cargo) and sits on the hull top at y = 0.55.
Local frame: +Z forward. c28 hull, c14 tracks/hopper load, c17 scoop,
c01 headlights, c00 track ends.""",
"radar_booster": """terra-siege radar booster — extends radar range while alive.
Octagonal foundation, tapered six-sided mast, bearing housing, two
equipment cabinets and a cable run. {verts} verts, {tris} tris.
The rotating dish and tip pip stay procedural (animated yaw) at y ~ 3.6;
the mast tops out at 3.3. Local frame: +Z forward, foot at y = 0.
c30 foundation, c21/c22/c23 mast, c08 bearing, c17 cabinet panels, c00 cable.""",
"lander": """terra-siege lander — grounded enemy base (Slice C C.3).
Octagonal hull with four drone-bay doors, four landing legs, deck
warning lights, a production silo with a glowing hatch, two exhaust
stacks and a forward command bridge. {verts} verts, {tris} tris.
e.pos sits 0.6 above the terrain; feet reach y = -1.1. Shields are
procedural. Local frame: +Z forward. c28/c26/c24/c25 hull, c22 underside,
c00 doors/feet/stack tops, c02 lights, c29 hatch, c17 bridge window.""",
}
