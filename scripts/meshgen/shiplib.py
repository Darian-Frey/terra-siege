# Shared helpers for generating terra-siege OBJ meshes in Blender.
# Authoring frame: x right, y forward (nose), z up.
# Blender frame: (x, -y, z). Game OBJ frame: (x, z_up, y_fwd).
import bpy, bmesh, os, math
from mathutils import Vector

# Mirror of src/mesh/Palette.hpp (preview colours only; the game reads cNN).
PAL = {
    0: (0, 0, 0),
    1: (255, 255, 255),
    2: (255, 0, 0),
    3: (0, 255, 0),
    4: (150, 72, 52),    # red brick
    5: (170, 140, 90),   # thatch brown
    6: (255, 0, 255),
    7: (0, 255, 255),
    8: (128, 128, 128),
    9: (192, 128, 64),
    10: (64, 128, 192),
    11: (255, 128, 0),
    12: (128, 0, 255),
    13: (0, 128, 0),
    14: (128, 64, 0),
    15: (200, 200, 200),
    16: (200, 50, 50),
    17: (240, 200, 80),
    18: (200, 80, 220),
    19: (120, 60, 160),
    20: (200, 140, 240),
    21: (70, 80, 110),
    22: (40, 50, 80),
    23: (180, 200, 240),
    24: (120, 130, 80),
    25: (90, 100, 60),
    26: (110, 100, 90),
    27: (160, 70, 60),
    28: (80, 110, 70),
    29: (120, 220, 100),
    30: (90, 100, 110),
    31: (120, 220, 140),
}

def mat(i):
    name = "c%02d" % i
    m = bpy.data.materials.get(name)
    if not m:
        m = bpy.data.materials.new(name)
        r, g, b = PAL[i]
        m.diffuse_color = tuple((c / 255.0) ** 2.2 for c in (r, g, b)) + (1,)
    return m

def P(i):
    return (i, i)

class Ship:
    ref = (0, 0, 0)
    def __init__(self, name):
        self.name, self.v, self.f = name, [], []
    def vert(self, x, y, z):
        self.v.append((x, -y, z)); return len(self.v) - 1
    def sym(self, x, y, z):
        return self.vert(x, y, z), self.vert(-x, y, z)
    def face(self, idx, c, ref="default"):
        """Add a face, flipped if needed so its normal points away from ref
        (authoring coords). ref=None keeps the given winding."""
        idx = list(idx)
        if ref == "default": ref = self.ref
        if ref is not None:
            pts = [Vector(self.v[i]) for i in dict.fromkeys(idx)]
            if len(pts) < 3: return
            n = Vector((0, 0, 0))
            for k in range(len(pts)):
                a, b = pts[k], pts[(k + 1) % len(pts)]
                n += Vector(((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x),
                             (a.x - b.x) * (a.y + b.y)))
            cen = sum(pts, Vector()) / len(pts)
            r = Vector((ref[0], -ref[1], ref[2]))
            if n.dot(cen - r) < 0: idx = idx[::-1]
        self.f.append((idx, c))
    def twoside(self, idx, c):
        self.face(idx, c, ref=None); self.face(idx[::-1], c, ref=None)
    def tri_sym(self, a, b, c_, col, ref="default"):
        self.face([a[0], b[0], c_[0]], col, ref); self.face([a[1], b[1], c_[1]], col, ref)
    def quad_sym(self, a, b, c_, d, col, ref="default"):
        self.face([a[0], b[0], c_[0], d[0]], col, ref); self.face([a[1], b[1], c_[1], d[1]], col, ref)

    # ---- parts ----
    def loft(self, secs, ecols, cx=0.0, front=None, back=None):
        """secs: [(y, half)] front-to-back; half = right-side profile [(x, z)]
        from top centre (x=0) to bottom centre (x=0), or a single (0, z) point.
        ecols: colour per half-edge, or a list of such lists per segment.
        front/back: cap colours (None = open)."""
        rings, axes = [], []
        for y, half in secs:
            if len(half) == 1:
                i = self.vert(cx + half[0][0], y, half[0][1]); rings.append(None); rings[-1] = [i]
                axes.append((cx, y, half[0][1])); continue
            right = [self.vert(cx + x, y, z) if x != 0 else None for x, z in half]
            top = self.vert(cx, y, half[0][1]); bot = self.vert(cx, y, half[-1][1])
            right[0], right[-1] = top, bot
            left = [self.vert(cx - x, y, z) for x, z in half[1:-1]]
            rings.append(right + left[::-1])
            zs = [z for _, z in half]
            axes.append((cx, y, (max(zs) + min(zs)) / 2))
        n = max(len(r) for r in rings)
        nh = n // 2 + 1  # half-edge count + 1
        for k in range(len(rings) - 1):
            r0, r1 = rings[k], rings[k + 1]
            if len(r0) == 1: r0 = r0 * n
            if len(r1) == 1: r1 = r1 * n
            cols = ecols[k] if isinstance(ecols[0], (list, tuple)) else ecols
            a0, a1 = axes[k], axes[k + 1]
            ref = tuple((a0[i] + a1[i]) / 2 for i in range(3))
            for e in range(n):
                j = (e + 1) % n
                he = e if e < nh - 1 else n - 1 - e
                self.face([r0[e], r0[j], r1[j], r1[e]], cols[he], ref)
        for ring, ax, col, d in ((rings[0], axes[0], front, -1), (rings[-1], axes[-1], back, 1)):
            if col is not None and len(ring) > 2:
                self.face(ring, col, (ax[0], ax[1] + d, ax[2]))
        return rings

    def block(self, bot, top, cols, ref=None):
        """bot/top: (x0, x1, y0, y1, z). cols: dict top/bottom/front/back/left/right
        or one colour."""
        if not isinstance(cols, dict): cols = {k: cols for k in ("top", "bottom", "front", "back", "left", "right")}
        def rect(r):
            x0, x1, y0, y1, z = r
            return [self.vert(x0, y0, z), self.vert(x1, y0, z), self.vert(x1, y1, z), self.vert(x0, y1, z)]
        b, t = rect(bot), rect(top)
        pts = [Vector(self.v[i]) for i in b + t]
        c = sum(pts, Vector()) / 8
        ref = ref or (c.x, -c.y, c.z)
        self.face(t, cols["top"], ref); self.face(b, cols["bottom"], ref)
        self.face([b[0], b[1], t[1], t[0]], cols["back"], ref)
        self.face([b[3], b[2], t[2], t[3]], cols["front"], ref)
        self.face([b[1], b[2], t[2], t[1]], cols["right"], ref)
        self.face([b[0], b[3], t[3], t[0]], cols["left"], ref)

    def stack(self, layers, bandcols, n=8, cx=0.0, cy=0.0, rot=0.0, top=None, bottom=None):
        """Vertical lathe: layers [(z, rx, ry)] bottom-to-top, n-gon rings.
        bandcols: per band, a colour or a function(facet_index)->colour."""
        rings = []
        for z, rx, ry in layers:
            ring = []
            for k in range(n):
                a = rot + 2 * math.pi * k / n
                ring.append(self.vert(cx + rx * math.cos(a), cy + ry * math.sin(a), z))
            rings.append(ring)
        for b in range(len(layers) - 1):
            ref = (cx, cy, (layers[b][0] + layers[b + 1][0]) / 2)
            for k in range(n):
                j = (k + 1) % n
                col = bandcols[b](k) if callable(bandcols[b]) else bandcols[b]
                self.face([rings[b][k], rings[b][j], rings[b + 1][j], rings[b + 1][k]], col, ref)
        if bottom is not None: self.face(rings[0], bottom, (cx, cy, layers[0][0] + 1))
        if top is not None: self.face(rings[-1], top, (cx, cy, layers[-1][0] - 1))
        return rings

    def strut(self, a, b, w, col):
        """Square-section beam of half-width w from point a to point b."""
        a, b = Vector(a), Vector(b)
        d = (b - a).normalized()
        u = d.cross(Vector((0, 0, 1)))
        if u.length < 1e-4: u = d.cross(Vector((1, 0, 0)))
        u.normalize(); v = d.cross(u)
        offs = [(u + v) * w, (u - v) * w, (-u - v) * w, (-u + v) * w]
        ra = [self.vert(*(a + o)) for o in offs]
        rb = [self.vert(*(b + o)) for o in offs]
        m = (a + b) / 2
        for k in range(4):
            j = (k + 1) % 4
            self.face([ra[k], ra[j], rb[j], rb[k]], col, tuple(m))
        self.face(ra, col, tuple(b)); self.face(rb, col, tuple(a))

    def spike(self, base, tip, col):
        """base: list of vertex indices (polygon), tip: (x,y,z)."""
        t = self.vert(*tip)
        pts = [Vector(self.v[i]) for i in base]
        c = sum(pts, Vector()) / len(pts)
        ref = (c.x, -c.y, c.z)
        for k in range(len(base)):
            self.face([base[k], base[(k + 1) % len(base)], t], col, ref)

def clean(idx):
    u = []
    for i in idx:
        if not u or u[-1] != i: u.append(i)
    if len(u) > 1 and u[0] == u[-1]: u.pop()
    return u if len(set(u)) >= 3 else None

def build_blender(ship):
    me = bpy.data.meshes.new(ship.name)
    bm = bmesh.new()
    mats = sorted({c for _, c in ship.f})
    for idx, c in ship.f:
        u = clean(idx)
        if not u: continue
        f = bm.faces.new([bm.verts.new(Vector(ship.v[i])) for i in u])
        f.material_index = mats.index(c)
    bm.to_mesh(me); bm.free()
    for c in mats: me.materials.append(mat(c))
    ob = bpy.data.objects.new(ship.name, me)
    bpy.context.collection.objects.link(ob)
    return ob

def write_obj(ship, path, header):
    used = set(i for idx, _ in ship.f if clean(idx) for i in idx)
    remap, verts = {}, []
    for i, p in enumerate(ship.v):
        if i in used:
            remap[i] = len(verts) + 1; verts.append(p)
    lines, tris = [], 0
    for c in sorted({c for _, c in ship.f}):
        lines.append("usemtl c%02d" % c)
        for idx, cc in ship.f:
            u = clean(idx) if cc == c else None
            if not u: continue
            for k in range(1, len(u) - 1):
                a, b, c_ = (Vector(ship.v[u[0]]), Vector(ship.v[u[k]]), Vector(ship.v[u[k + 1]]))
                if (b - a).cross(c_ - a).length < 1e-6: continue
                lines.append("f %d %d %d" % (remap[u[0]], remap[u[k]], remap[u[k + 1]])); tris += 1
    with open(path, "w") as fh:
        for h in header.format(verts=len(verts), tris=tris).splitlines():
            fh.write(("# " + h).rstrip() + "\n")
        fh.write("#\n# Generated by scripts/meshgen/build.py. Edit the definition there and\n"
                 "# regenerate rather than hand-editing this file.\n")
        fh.write("o %s\n\n" % ship.name)
        for bx, by, bz in verts:
            fh.write("v %.4f %.4f %.4f\n" % (bx, bz, -by))
        fh.write("\n" + "\n".join(lines) + "\n")
    return len(verts), tris

def setup_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"; sh.color_type = "MATERIAL"
    sh.show_object_outline = True; sh.show_backface_culling = True
    sc.display.render_aa = "8"; sc.view_settings.view_transform = "Standard"
    sc.render.resolution_x, sc.render.resolution_y = 600, 400
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam"))
    sc.collection.objects.link(cam); sc.camera = cam
    cam.data.type = "ORTHO"
    return sc, cam

VIEWS = {"three_quarter": (1.0, -1.0, 0.62), "front": (-0.6, -1.0, 0.4),
         "rear": (-0.7, 1.0, 0.55), "side": (1.0, 0.0, 0.08),
         "top": (0.0001, 0.0, 1.0), "below": (0.6, -0.8, -0.6)}

def render(ship, outdir, size, views=VIEWS):
    sc, cam = bpy.context.scene, bpy.context.scene.camera
    for o in list(bpy.data.objects):
        if o.type == "MESH": bpy.data.objects.remove(o)
    build_blender(ship)
    used = [Vector(ship.v[i]) for idx, _ in ship.f for i in idx]
    centre = (Vector([min(p[k] for p in used) for k in range(3)]) +
              Vector([max(p[k] for p in used) for k in range(3)])) / 2
    cam.data.ortho_scale = size
    files = []
    for vn, d in views.items():
        pos = Vector(d).normalized() * size * 3
        cam.location = centre + pos
        cam.rotation_euler = (-pos).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(outdir, "%s_%s.png" % (ship.name, vn))
        bpy.ops.render.render(write_still=True)
        files.append(sc.render.filepath)
    return files
