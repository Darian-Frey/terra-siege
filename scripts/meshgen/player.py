"""Player ship definition. The builder returns (Ship, preview ortho size)."""
from shiplib import Ship, P

def hovercraft():
    s = Ship("hovercraft"); s.ref = (0, 0, 0.1)
    nose  = P(s.vert(0, 2.6, 0.15))
    noseB = P(s.vert(0, 2.3, -0.15))
    spine = P(s.vert(0, 0.2, 0.55))
    tailT = P(s.vert(0, -1.7, 0.45))
    tailB = P(s.vert(0, -1.7, -0.30))
    keel  = P(s.vert(0, 0.3, -0.40))
    wing  = s.sym(1.9, -1.2, 0.05)
    wingB = s.sym(1.9, -1.2, -0.05)
    rear  = s.sym(0.75, -1.8, 0.35)
    rearB = s.sym(0.75, -1.8, -0.25)
    sh    = s.sym(0.55, 0.4, 0.40)
    s.tri_sym(nose, sh, spine, 15)
    s.tri_sym(nose, wing, sh, 15)
    s.quad_sym(sh, wing, rear, spine, 30)
    s.tri_sym(spine, rear, tailT, 15)
    s.quad_sym(wing, wingB, rearB, rear, 21)
    s.quad_sym(rear, rearB, tailB, tailT, 22)
    s.quad_sym(nose, noseB, wingB, wing, 16)
    s.tri_sym(noseB, keel, wingB, 22)
    s.quad_sym(wingB, keel, tailB, rearB, 30)
    # engine plates
    e1 = s.sym(0.22, -1.81, 0.27); e2 = s.sym(0.62, -1.81, 0.27)
    e3 = s.sym(0.62, -1.81, -0.14); e4 = s.sym(0.22, -1.81, -0.14)
    s.quad_sym(e1, e4, e3, e2, 11)
    # canopy: windscreen, side panes, rear frame (base sunk into the deck)
    cf  = P(s.vert(0, 1.25, 0.38))
    ctf = P(s.vert(0, 0.45, 0.86)); ctb = P(s.vert(0, -0.20, 0.78))
    cs1 = s.sym(0.38, 0.50, 0.45);  cs2 = s.sym(0.30, -0.25, 0.50)
    cb  = P(s.vert(0, -0.80, 0.52))
    s.tri_sym(cf, cs1, ctf, 10)
    s.quad_sym(cs1, cs2, ctb, ctf, 23)
    s.tri_sym(cs2, cb, ctb, 30)
    # dorsal fin (thin wedge)
    bF = P(s.vert(0, -0.75, 0.52)); bB = s.sym(0.08, -1.68, 0.46)
    tF = P(s.vert(0, -1.45, 1.05)); tB = s.sym(0.04, -1.80, 1.05)
    s.quad_sym(bF, bB, tB, tF, 16)
    s.face([bB[0], bB[1], tB[1], tB[0]], 22)
    s.face([tF[0], tB[0], tB[1]], 16)
    # twin nose cannons (triangular prisms)
    for sx in (1, -1):
        x = sx * 0.45
        r0 = [s.vert(x - 0.07, 0.8, 0.27), s.vert(x + 0.07, 0.8, 0.27), s.vert(x, 0.8, 0.39)]
        r1 = [s.vert(x - 0.07, 2.1, 0.27), s.vert(x + 0.07, 2.1, 0.27), s.vert(x, 2.1, 0.39)]
        ax = (x, 1.45, 0.31)
        for k in range(3):
            j = (k + 1) % 3
            s.face([r0[k], r0[j], r1[j], r1[k]], 8, ref=ax)
        s.face(r1, 0, ref=ax); s.face(r0, 8, ref=ax)
    # wingtip nav lights. Model +X is the pilot's LEFT in game (world +X
    # is west, nose faces north at yaw 0), so +X is port = red.
    for sx, c in ((1, 2), (-1, 3)):
        a = s.vert(sx * 1.9, -1.2, 0.07)
        b = s.vert(sx * 1.6, -1.0, 0.09)
        d = s.vert(sx * 1.68, -1.42, 0.12)
        s.face([a, b, d], c, ref=(sx * 1.7, -1.2, -1.0))
    return s, 6.5

SHIPS = {"hovercraft": hovercraft}

HEADERS = {
"hovercraft": """terra-siege hovercraft — player ship.

Swept arrowhead hull, faceted canopy, dorsal fin, twin nose cannons,
rear engine plates, wingtip nav lights. {verts} verts, {tris} tris.
Local frame: +X right, +Y up, +Z forward (nose). Matches the game's
yaw convention so DrawModelEx around world-up rotates correctly.
Note: world +X is west, so the model's +X side is port (red light).

Materials:
  c15  light grey      — top deck
  c30  structure grey  — deck insets, belly, canopy rear frame
  c21/c22  navy        — trailing edges, rear face, fin back
  c16  ship red        — leading-edge lip, dorsal fin
  c10/c23  blues       — windscreen / side panes
  c11  orange          — engine plates
  c08/c00  grey/black  — cannon barrels / muzzles
  c02/c03  red/green   — nav lights""",
}
