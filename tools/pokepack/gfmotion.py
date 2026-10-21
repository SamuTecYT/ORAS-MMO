"""
gfmotion.py - decoder for the GameFreak "GF1" motion pack used by ORAS/XY models (field characters and the
Pokemon battle / Pokemon-Amie motions), plus pose sampling for the BCH skeleton in bch.py.

Format reference: gdkchan/SPICA  Formats/GFL (GF1MotionPack / GF1Motion / GF1MotBone), re-verified here
on the ORAS ROM bytes (every motion parses to exactly the start of the next one).
"""
import struct, math
import numpy as np

class MotBone:
    def __init__(self): self.name = ""; self.parent = -1; self.flags = 0; self.childs = 0; self.t = (0, 0, 0); self.q = (0, 0, 0, 1)

def read_skeleton(d, o):
    n = d[o]; first = d[o + 1]; p = o + 2
    bones = [MotBone() for _ in range(n)]
    bones[0].name = "Origin"
    for i in range(1, n):
        bones[i].parent, bones[i].flags, bones[i].childs = d[p], d[p + 1], d[p + 2]; p += 3
    for i in range(1, n):
        e = d.index(b"\0", p); bones[i].name = d[p:e].decode("ascii"); p = e + 1
    p = (p + 3) & ~3
    for i in range(n):
        v = struct.unpack_from("<7f", d, p); p += 28
        bones[i].t = v[0:3]; bones[i].q = v[3:7]
    return bones, first, p

ELEM = ["tx", "ty", "tz", "rx", "ry", "rz", "sx", "sy", "sz"]

class Curve:
    """kind: 'const' (value) | 'linear' [(frame,value)] | 'hermite' [(frame,value,slope)]"""
    def __init__(self, kind, keys): self.kind = kind; self.keys = keys
    def __repr__(self): return "%s%s" % (self.kind, self.keys if len(self.keys) < 4 else "[%d keys]" % len(self.keys))
    def eval(self, f):
        k = self.keys
        if self.kind == "const": return k[0][1]
        if f <= k[0][0]: return k[0][1]
        if f >= k[-1][0]: return k[-1][1]
        # binary search
        lo, hi = 0, len(k) - 1
        while hi - lo > 1:
            mid = (lo + hi) >> 1
            if k[mid][0] <= f: lo = mid
            else: hi = mid
        a, b = k[lo], k[hi]
        span = b[0] - a[0]
        if span <= 0: return b[1]
        t = (f - a[0]) / span
        if self.kind == "linear":
            return a[1] + (b[1] - a[1]) * t
        # cubic hermite, slopes in value-per-frame (SLOPE_SCALE applied at parse time)
        t2 = t * t; t3 = t2 * t
        h00 = 2 * t3 - 3 * t2 + 1; h10 = t3 - 2 * t2 + t; h01 = -2 * t3 + 3 * t2; h11 = t3 - t2
        return h00 * a[1] + h10 * span * a[2] + h01 * b[1] + h11 * span * b[2]

SLOPE_SCALE = 1.0 / 30.0  # stored slopes are per second at 30 fps -> per frame (verified vs finite differences)

class Motion:
    def __init__(self, d, o, skel, slot):
        self.slot = slot; self.offset = o
        octals_count, self.frames = struct.unpack_from("<HH", d, o); p = o + 4
        octals = []; kfl = 0
        for i in range(octals_count):
            if (i & 7) == 0:
                cur = d[p] | d[p + 1] << 8 | d[p + 2] << 16; p += 3
            octals.append(cur & 7); cur >>= 3
            if octals[-1] > 5: kfl += 1
        self.octals = octals
        f16 = self.frames > 0xFF
        if f16 and (p & 1): p += 1
        lists = []
        for i in range(kfl):
            if f16: c = struct.unpack_from("<H", d, p)[0]; p += 2
            else: c = d[p]; p += 1
            fr = [0]
            for j in range(c):
                if f16: fr.append(struct.unpack_from("<H", d, p)[0]); p += 2
                else: fr.append(d[p]); p += 1
            fr.append(self.frames)
            lists.append(fr)
        p = (p + 3) & ~3
        self.header_octals = octals[:2]
        # curves[bone_name][elem] = Curve
        self.curves = {}; self.world_space = set()
        li = 0; elem = 0; oi = 2
        while oi < octals_count:
            c = octals[oi]; oi += 1
            if c == 1: elem += 3; continue
            bi = elem // 9
            bn = skel[bi].name
            if skel[bi].flags & 8: self.world_space.add(bn)
            cv = self.curves.setdefault(bn, {})
            e = ELEM[elem % 9]
            if c == 0: cv[e] = Curve("const", [(0, 0.0)])
            elif c == 2: cv[e] = Curve("const", [(0, math.pi * 0.5)])
            elif c == 3: cv[e] = Curve("const", [(0, math.pi)])
            elif c == 4: cv[e] = Curve("const", [(0, -math.pi * 0.5)])
            elif c == 5: cv[e] = Curve("const", [(0, struct.unpack_from("<f", d, p)[0])]); p += 4
            elif c == 6:
                ks = []
                for fr in lists[li]: ks.append((fr, struct.unpack_from("<f", d, p)[0])); p += 4
                li += 1; cv[e] = Curve("linear", ks)
            elif c == 7:
                ks = []
                for fr in lists[li]:
                    v, s = struct.unpack_from("<2f", d, p); p += 8; ks.append((fr, v, s * SLOPE_SCALE))
                li += 1; cv[e] = Curve("hermite", ks)
            elem += 1
        self.end = p; self.n_elems = elem

class MotionPack:
    def __init__(self, d):
        self.data = d
        n = struct.unpack_from("<I", d, 0)[0]
        self.offsets = struct.unpack_from("<%dI" % n, d, 4)
        self.skeleton, self.first_bone, self.skel_end = read_skeleton(d, self.offsets[0])
        self.motions = {}
        for i in range(1, n):
            if self.offsets[i]: self.motions[i - 1] = Motion(d, self.offsets[i], self.skeleton, i - 1)

# ---------------------------------------------------------------- pose sampling on the BCH skeleton
def euler_mat(rx, ry, rz):
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]]); Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx

def sample_locals(model, mot, frame, use_world_space=True):
    """Returns list of 4x4 local matrices for every BCH bone at `frame` (float).  Bones / elements without
    a curve keep their bind-pose value (BCH bone S/R/T)."""
    out = []
    for b in model.bones:
        s = list(b.scale); r = list(b.rotation); t = list(b.translation)
        cv = mot.curves.get(b.name)
        if cv:
            for i, k in enumerate(("tx", "ty", "tz")):
                if k in cv: t[i] = cv[k].eval(frame)
            for i, k in enumerate(("rx", "ry", "rz")):
                if k in cv: r[i] = cv[k].eval(frame)
            for i, k in enumerate(("sx", "sy", "sz")):
                if k in cv: s[i] = cv[k].eval(frame)
        M = np.eye(4); M[:3, :3] = euler_mat(*r) @ np.diag(s); M[:3, 3] = t
        out.append(M)
    return out

def world_from_locals(model, L):
    W = []
    for b, l in zip(model.bones, L):
        W.append(l if b.parent < 0 else W[b.parent] @ l)
    return W

def _rot_between(a, b):
    a = a / np.linalg.norm(a); b = b / np.linalg.norm(b)
    v = np.cross(a, b); c = float(np.dot(a, b)); s = np.linalg.norm(v)
    if s < 1e-9: return np.eye(3)
    k = v / s; K = np.array([[0, -k[2], k[1]], [k[2], 0, -k[0]], [-k[1], k[0], 0]])
    ang = math.atan2(s, c)
    return np.eye(3) + math.sin(ang) * K + (1 - math.cos(ang)) * (K @ K)

def ik_targets(mot, frame):
    """World-space (model-space) effector curves: {bone: (T(3), R_euler(3) or None)}"""
    out = {}
    for bn in mot.world_space:
        cv = mot.curves[bn]
        t = np.array([cv[k].eval(frame) for k in ("tx", "ty", "tz")])
        r = [cv[k].eval(frame) for k in ("rx", "ry", "rz")] if "rx" in cv else None
        out[bn] = (t, r)
    return out

# Motion slots (index into MotionPack.motions; GF offset-table index = slot + 1). Verified visually on both heroes.
SLOTS = {"idle": 0, "walk": 2, "idle_long": 3, "run": 4, "ledge_jump": 5}
FPS = 30.0
LOOP = {"idle": True, "walk": True, "run": True, "idle_long": False, "ledge_jump": False}

def sample_pose(model, mot, frame, ik=True):
    """Full pose at `frame` (float, 0..mot.frames): FK from curves + analytic 2-bone IK for the
    world-space effectors (GF bone flag 0x08: LFoot/RFoot). Returns list of 4x4 world (model-space) matrices,
    one per BCH bone.  Effector curves: T = target position in model space, R = effector LOCAL Euler
    rotation (relative to its parent, the shin) - verified: planted feet come out flat (<0.5 deg)."""
    L = sample_locals(model, mot, frame)
    byname = {b.name: b for b in model.bones}
    tg = ik_targets(mot, frame) if ik else {}
    for bn, (T, r) in sorted(tg.items()):
        eff = byname[bn]; mid = model.bones[eff.parent]; root = model.bones[mid.parent]
        L[eff.index][:3, 3] = eff.translation                     # bone length stays the bind one
        if r is not None: L[eff.index][:3, :3] = euler_mat(*r)
        W = world_from_locals(model, L)
        H = W[root.index][:3, 3]
        a = np.linalg.norm(L[mid.index][:3, 3]); b = np.linalg.norm(L[eff.index][:3, 3])
        d = np.clip(np.linalg.norm(T - H), abs(a - b) + 1e-4, a + b - 1e-4)
        bend = math.pi - math.acos(np.clip((a * a + b * b - d * d) / (2 * a * b), -1, 1))
        # knee hinge = local +Y of the middle joint (positive = shin goes backwards)
        L[mid.index][:3, :3] = np.array(mid.local)[:3, :3] @ euler_mat(0, bend, 0)
        W = world_from_locals(model, L)
        Rw = _rot_between(W[eff.index][:3, 3] - H, T - H)        # swing the thigh so the ankle hits T
        Wr = W[root.index].copy(); Wr[:3, :3] = Rw @ Wr[:3, :3]
        L[root.index] = np.linalg.inv(W[root.parent]) @ Wr
    return world_from_locals(model, L)

def bake(model, mot, frames=None):
    """Bake skin matrices (World x InvBind, 3x4) at integer frames 0..N -> array (N+1, nbones, 3, 4)."""
    N = mot.frames if frames is None else frames
    out = np.zeros((N + 1, len(model.bones), 3, 4), np.float32)
    for f in range(N + 1):
        W = sample_pose(model, mot, f)
        for b in model.bones: out[f, b.index] = (W[b.index] @ b.inv_bind)[:3, :]
    return out
