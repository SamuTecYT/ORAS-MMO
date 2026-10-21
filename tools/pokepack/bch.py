"""
bch.py - Reference parser for the H3D / BCH files used by Pokemon Omega Ruby / Alpha Sapphire
(BCH backward/forward compatibility 0x21, version 0xA66F) plus the "MM" pack container.

Written for format understanding / as reference for a C++ runtime loader.
Verified against /a/0/2/1 entries 171 (rstr0001_00_fi, Brendan) and 172 (rstr0002_00_fi, May).

Usage:
    from bch import BCH, mm_unpack
    pack = mm_unpack(garc_entry_bytes)       # -> list of sub-file bytes
    b = BCH(pack[0])
    model = b.models[0]; model.meshes[i].positions ...
    b.textures[i].decode() -> numpy uint8 (H, W, 4)  (row 0 = top of the image, i.e. v=1)
"""
import struct, math
import numpy as np
import pica

# ----------------------------------------------------------------------------------------
# MM pack container ("MM" + u16 count + (count+1) u32 offsets, sub-files 0x80 aligned)
# ----------------------------------------------------------------------------------------
def mm_unpack(d):
    assert d[:2] == b"MM", d[:2]
    n = struct.unpack_from("<H", d, 2)[0]
    offs = struct.unpack_from("<%dI" % (n + 1), d, 4)
    return [d[offs[i]:offs[i + 1]] for i in range(n)]

# ----------------------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------------------
def _u32(b, o): return struct.unpack_from("<I", b, o)[0]
def _i32(b, o): return struct.unpack_from("<i", b, o)[0]
def _u16(b, o): return struct.unpack_from("<H", b, o)[0]
def _i16(b, o): return struct.unpack_from("<h", b, o)[0]
def _f32(b, o): return struct.unpack_from("<f", b, o)[0]
def _str(b, o):
    if o == 0: return None
    e = b.index(b"\0", o)
    return b[o:e].decode("ascii", "replace")
def _rgba(b, o): return tuple(b[o:o + 4])       # bytes R,G,B,A

def mat34(b, o):
    """3x4 row-major float matrix -> 4x4 numpy"""
    m = np.eye(4, dtype=np.float64)
    m[:3, :4] = np.array(struct.unpack_from("<12f", b, o)).reshape(3, 4)
    return m

def euler_xyz(rx, ry, rz):
    """H3D bone rotation: R = Rz * Ry * Rx (applied X first). Verified against InverseTransform."""
    cx, sx, cy, sy, cz, sz = math.cos(rx), math.sin(rx), math.cos(ry), math.sin(ry), math.cos(rz), math.sin(rz)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx

def srt_matrix(s, r, t):
    m = np.eye(4)
    m[:3, :3] = euler_xyz(*r) @ np.diag(s)
    m[:3, 3] = t
    return m

# ----------------------------------------------------------------------------------------
# Header + relocation
# ----------------------------------------------------------------------------------------
SECTION_NAMES = ["models", "materials", "shaders", "textures", "luts", "lights", "cameras", "fogs",
                 "skeletal_anims", "material_anims", "visibility_anims", "light_anims", "camera_anims",
                 "fog_anims", "scenes"]

class Header:
    FIELDS = ["contents_off", "strings_off", "commands_off", "raw_off", "raw_ext_off", "reloc_off",
              "contents_len", "strings_len", "commands_len", "raw_len", "raw_ext_len", "reloc_len",
              "uninit_data_len", "uninit_desc_len"]
    def __init__(self, d):
        assert d[:4] == b"BCH\0"
        self.backward, self.forward, self.version = struct.unpack_from("<BBH", d, 4)
        assert self.backward >= 0x21, "only BCH >= 0x21 (ORAS/XY) handled"
        vals = struct.unpack_from("<14I", d, 8)
        for k, v in zip(self.FIELDS, vals): setattr(self, k, v)
        self.flags, self.address_count = struct.unpack_from("<HH", d, 0x40)
        self.size = 0x44

def relocate(d, h):
    """Apply the relocation table. Returns (patched bytes, list of (flag, patched_addr, old_value)).
    Entry u32: low 25 bits = word offset (byte offset for flag 1), high 7 bits = flag.
      flag 0x00: ptr at contents + off*4 += contents_off            (contents -> contents)
      flag 0x01: ptr at contents + off   += strings_off             (contents -> string)  [off is a BYTE offset]
      flag 0x02: ptr at contents + off*4 += commands_off            (contents -> GPU commands)
      flag 0x03: ptr at contents + off*4 += commands_off            (contents -> GPU commands, 'source' copy)
      flag 0x25: word at commands + off*4 += raw_off  (texture address, reg 0x85/0x95/0x9D)
      flag 0x26: word at commands + off*4 += raw_off  (vertex buffer offset, reg 0x203+3n)
      flag 0x27: word at commands + off*4 += raw_off  (index buffer, reg 0x227, u16 indices)
      flag 0x28: word at commands + off*4 += raw_off  (index buffer, reg 0x227, u8 indices)
      flag 0x2E: word at commands + off*4 += raw_off  (attribute base address, reg 0x200)
    (other 0x2x flags would target raw_ext; none occur in the hero models)."""
    b = bytearray(d); log = []
    for o in range(0, h.reloc_len, 4):
        v = _u32(b, h.reloc_off + o); off = v & 0x1ffffff; fl = v >> 25
        if fl == 0: a, add = h.contents_off + off * 4, h.contents_off
        elif fl == 1: a, add = h.contents_off + off, h.strings_off
        elif fl in (2, 3): a, add = h.contents_off + off * 4, h.commands_off
        elif fl in (0x25, 0x26, 0x27, 0x28, 0x2e): a, add = h.commands_off + off * 4, h.raw_off
        elif fl in (0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2f): a, add = h.commands_off + off * 4, h.raw_ext_off
        else: raise ValueError("unknown relocation flag %x" % fl)
        old = _u32(b, a)
        struct.pack_into("<I", b, a, (old + add) & 0xffffffff)
        log.append((fl, a, old))
    return bytes(b), log

# ----------------------------------------------------------------------------------------
# Textures
# ----------------------------------------------------------------------------------------
FMT_NAMES = {0: "RGBA8", 1: "RGB8", 2: "RGBA5551", 3: "RGB565", 4: "RGBA4", 5: "LA8", 6: "HILO8", 7: "L8",
             8: "A8", 9: "LA4", 10: "L4", 11: "A4", 12: "ETC1", 13: "ETC1A4"}
FMT_BPP = {0: 32, 1: 24, 2: 16, 3: 16, 4: 16, 5: 16, 6: 16, 7: 8, 8: 8, 9: 8, 10: 4, 11: 4, 12: 4, 13: 8}

# Morton order inside an 8x8 tile: index i -> (x, y)
_MORTON = [(((i >> 0) & 1) | ((i >> 1) & 2) | ((i >> 2) & 4), ((i >> 1) & 1) | ((i >> 2) & 2) | ((i >> 3) & 4))
           for i in range(64)]
_MX = np.array([m[0] for m in _MORTON]); _MY = np.array([m[1] for m in _MORTON])

def _untile(px, w, h):
    """px: (w*h, 4) array in 3DS tiled order (8x8 tiles, row-major tiles, Morton inside) -> (h, w, 4).
    Row 0 of the result is the FIRST tile row in memory, which is the TOP of the image as seen by an
    artist (PICA t=1). So texture coordinate v maps to row (1 - v) * h."""
    out = np.zeros((h, w, px.shape[1]), dtype=np.uint8)
    tiles_x = w // 8
    n = np.arange(w * h)
    tile = n // 64; i = n % 64
    x = (tile % tiles_x) * 8 + _MX[i]
    y = (tile // tiles_x) * 8 + _MY[i]
    out[y, x] = px
    return out

_ETC1_MOD = np.array([[2, 8, -2, -8], [5, 17, -5, -17], [9, 29, -9, -29], [13, 42, -13, -42],
                      [18, 60, -18, -60], [24, 80, -24, -80], [33, 106, -33, -106], [47, 183, -47, -183]])

def _etc1_block(v):
    """Decode one ETC1 block given as a 64-bit int (big-endian ETC1 bit layout). Returns (4,4,3) [y][x]."""
    hi = v >> 32; lo = v & 0xffffffff
    diff = (hi >> 1) & 1; flip = hi & 1
    t1 = (hi >> 5) & 7; t2 = (hi >> 2) & 7
    if diff:
        r1 = (hi >> 27) & 31; g1 = (hi >> 19) & 31; b1 = (hi >> 11) & 31
        dr = ((hi >> 24) & 7); dg = ((hi >> 16) & 7); db = ((hi >> 8) & 7)
        dr = dr - 8 if dr > 3 else dr; dg = dg - 8 if dg > 3 else dg; db = db - 8 if db > 3 else db
        r2, g2, b2 = r1 + dr, g1 + dg, b1 + db
        c1 = [(c << 3) | (c >> 2) for c in (r1, g1, b1)]
        c2 = [((c & 31) << 3) | ((c & 31) >> 2) for c in (r2, g2, b2)]
    else:
        c1 = [((hi >> s) & 15) * 17 for s in (28, 20, 12)]
        c2 = [((hi >> s) & 15) * 17 for s in (24, 16, 8)]
    out = np.zeros((4, 4, 3), dtype=np.int32)
    for x in range(4):
        for y in range(4):
            k = x * 4 + y
            idx = ((lo >> k) & 1) | (((lo >> (k + 16)) & 1) << 1)
            second = (y >= 2) if flip else (x >= 2)
            base, t = (c2, t2) if second else (c1, t1)
            m = _ETC1_MOD[t][idx]
            out[y, x] = [min(255, max(0, c + m)) for c in base]
    return out

def decode_texture(raw, w, h, fmt):
    """Decode a PICA200 texture (level 0) -> numpy (h, w, 4) uint8 RGBA, row 0 = top."""
    n = w * h
    if fmt in (12, 13):
        bs = 16 if fmt == 13 else 8
        out = np.zeros((h, w, 4), dtype=np.uint8)
        pos = 0
        for ty in range(0, h, 8):
            for tx in range(0, w, 8):
                for by, bx in ((0, 0), (0, 4), (4, 0), (4, 4)):   # 4 ETC1 blocks per 8x8 tile, Z order
                    if fmt == 13:
                        alpha = struct.unpack_from("<Q", raw, pos)[0]; pos += 8
                    v = struct.unpack_from("<Q", raw, pos)[0]; pos += 8   # stored little-endian
                    blk = _etc1_block(v)
                    out[ty + by:ty + by + 4, tx + bx:tx + bx + 4, :3] = blk
                    if fmt == 13:
                        a = np.zeros((4, 4), dtype=np.uint8)
                        for x in range(4):
                            for y in range(4):
                                a[y, x] = ((alpha >> ((x * 4 + y) * 4)) & 15) * 17
                        out[ty + by:ty + by + 4, tx + bx:tx + bx + 4, 3] = a
                    else:
                        out[ty + by:ty + by + 4, tx + bx:tx + bx + 4, 3] = 255
        return out
    a = np.frombuffer(raw, dtype=np.uint8)
    if fmt == 0:    # RGBA8, stored A,B,G,R
        p = a[:n * 4].reshape(n, 4)[:, ::-1]
    elif fmt == 1:  # RGB8, stored B,G,R
        p = np.concatenate([a[:n * 3].reshape(n, 3)[:, ::-1], np.full((n, 1), 255, np.uint8)], 1)
    elif fmt in (2, 3, 4):
        v = a[:n * 2].view("<u2").astype(np.uint32)
        if fmt == 2:   # RGBA5551: R15-11 G10-6 B5-1 A0
            r = (v >> 11) & 31; g = (v >> 6) & 31; b = (v >> 1) & 31; al = (v & 1) * 255
            p = np.stack([r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2, al], 1)
        elif fmt == 3:  # RGB565
            r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31
            p = np.stack([r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2, np.full(n, 255)], 1)
        else:           # RGBA4
            r = (v >> 12) & 15; g = (v >> 8) & 15; b = (v >> 4) & 15; al = v & 15
            p = np.stack([r * 17, g * 17, b * 17, al * 17], 1)
        p = p.astype(np.uint8)
    elif fmt == 5:  # LA8 stored A,L
        q = a[:n * 2].reshape(n, 2); L = q[:, 1]; A = q[:, 0]
        p = np.stack([L, L, L, A], 1)
    elif fmt == 6:  # HILO8 stored G(lo?),R
        q = a[:n * 2].reshape(n, 2)
        p = np.stack([q[:, 1], q[:, 0], np.zeros(n, np.uint8), np.full(n, 255, np.uint8)], 1)
    elif fmt == 7:
        L = a[:n]; p = np.stack([L, L, L, np.full(n, 255, np.uint8)], 1)
    elif fmt == 8:
        A = a[:n]; p = np.stack([np.full(n, 255, np.uint8)] * 3 + [A], 1)
    elif fmt == 9:  # LA4: L high nibble, A low
        q = a[:n]; L = (q >> 4) * 17; A = (q & 15) * 17
        p = np.stack([L, L, L, A], 1).astype(np.uint8)
    elif fmt in (10, 11):
        q = a[:n // 2]; nib = np.empty(n, np.uint8); nib[0::2] = q & 15; nib[1::2] = q >> 4
        nib = (nib * 17).astype(np.uint8)
        if fmt == 10: p = np.stack([nib, nib, nib, np.full(n, 255, np.uint8)], 1)
        else: p = np.stack([np.full(n, 255, np.uint8)] * 3 + [nib], 1)
    else:
        raise ValueError(fmt)
    return _untile(p, w, h)

class Texture:
    """H3DTexture (0x20 bytes): 3x {u32 cmdPtr, u32 wordCount} (one copy per texture unit 0/1/2),
    u8 format, u8 mipmapLevels, u16 pad, u32 namePtr, (then next struct)."""
    def __init__(self, b, o):
        self.offset = o
        self.cmd_ptr, self.cmd_words = _u32(b, o), _u32(b, o + 4)
        self.format = b[o + 0x18]; self.mipmaps = b[o + 0x19]
        self.name = _str(b, _u32(b, o + 0x1c))
        st = pica.State(pica.read_commands(b, self.cmd_ptr, self.cmd_words))
        dim = st.get(0x82); self.width = dim >> 16; self.height = dim & 0x7ff
        self.data_off = st.get(0x85)          # byte offset inside the BCH (after relocation)
        assert st.get(0x8e) == self.format
        bpp = FMT_BPP[self.format]
        self.level0_size = self.width * self.height * bpp // 8
        self.total_size = sum((self.width >> i) * (self.height >> i) * bpp // 8 for i in range(self.mipmaps))
        self._b = b
    @property
    def format_name(self): return FMT_NAMES[self.format]
    def raw(self): return self._b[self.data_off:self.data_off + self.level0_size]
    def decode(self): return decode_texture(self.raw(), self.width, self.height, self.format)

# ----------------------------------------------------------------------------------------
# Skeleton
# ----------------------------------------------------------------------------------------
BILLBOARD = {0: "Off", 1: "World", 2: "WorldViewpoint", 3: "Screen", 4: "ScreenViewpoint", 5: "YAxial",
             6: "YAxialViewpoint"}

class Bone:
    """H3DBone (0x64 bytes):
       +00 u32 flags  (bits 16-19 billboard mode, bit 22 segment-scale-compensate, low bits: IsIdentity etc.)
       +04 i16 parentIndex (-1 = root)   +06 u16 pad
       +08 vec3 scale  +14 vec3 rotation (Euler radians, XYZ)  +20 vec3 translation
       +2C mat3x4 inverseBindTransform (row-major, model space -> bone space)
       +5C u32 namePtr  +60 u32 metaDataPtr"""
    SIZE = 0x64
    def __init__(self, b, o, index):
        self.index = index
        self.flags = _u32(b, o)
        self.billboard = (self.flags >> 16) & 0xf
        self.segment_scale_comp = bool(self.flags & 0x00400000)
        self.parent = _i16(b, o + 4)
        self.scale = struct.unpack_from("<3f", b, o + 8)
        self.rotation = struct.unpack_from("<3f", b, o + 0x14)
        self.translation = struct.unpack_from("<3f", b, o + 0x20)
        self.inv_bind = mat34(b, o + 0x2c)
        self.name = _str(b, _u32(b, o + 0x5c))
        self.metadata_ptr = _u32(b, o + 0x60)
        self.local = srt_matrix(self.scale, self.rotation, self.translation)
        self.world = None

# ----------------------------------------------------------------------------------------
# Materials
# ----------------------------------------------------------------------------------------
MAPPING = {0: "UvCoordinateMap", 1: "CameraCubeEnvMap", 2: "CameraSphereEnvMap", 3: "ProjectionMap"}
WRAP = {0: "ClampToEdge", 1: "ClampToBorder", 2: "Repeat", 3: "Mirror"}
TEST_FUNC = {0: "Never", 1: "Always", 2: "Equal", 3: "NotEqual", 4: "Less", 5: "LEqual", 6: "Greater", 7: "GEqual"}
BLEND_FACTOR = {0: "Zero", 1: "One", 2: "SrcColor", 3: "1-SrcColor", 4: "DstColor", 5: "1-DstColor", 6: "SrcAlpha",
                7: "1-SrcAlpha", 8: "DstAlpha", 9: "1-DstAlpha", 10: "ConstColor", 11: "1-ConstColor",
                12: "ConstAlpha", 13: "1-ConstAlpha", 14: "SrcAlphaSaturate"}
CULL = {0: "None", 1: "FrontFace(CW)", 2: "BackFace(CCW kept)"}

class TexCoord:
    """H3DTextureCoord (0x18 bytes): u8 flags, u8 transformType, u8 mappingType, i8 referenceCamera,
       vec2 scale, f32 rotation, vec2 translation"""
    def __init__(self, b, o):
        self.flags, self.transform_type, self.mapping, self.camera = struct.unpack_from("<BBBb", b, o)
        self.scale = struct.unpack_from("<2f", b, o + 4)
        self.rotation = _f32(b, o + 12)
        self.translation = struct.unpack_from("<2f", b, o + 16)
    def __repr__(self):
        return "TexCoord(map=%s cam=%d scale=%s rot=%.3f trans=%s)" % (MAPPING.get(self.mapping), self.camera,
                                                                        self.scale, self.rotation, self.translation)

class Material:
    """H3DMaterial (0x2C bytes, in the model's material array):
       +00 u32 paramsPtr  +04..+0C 3x u32 (0)  +10 u32 texCommandsPtr  +14 u32 texCommandsWords
       +18 u32 textureMappersPtr  +1C texture0 name ptr  +20 texture1 name ptr  +24 texture2 name ptr
       +28 material name ptr
    MaterialParams (pointed by paramsPtr) - layout verified:
       +00 u32 uniqueId/hash  +04 u16 flags (bit0 fragment lighting, bit1 vertex lighting, bit2 hemi, bit3 hemiOcc,
       bit4 fog, bit5 polygonOffset)  +06 u8 fragmentFlags  +07 u8 frameAccessOffset  +08 u32
       +0C 3x TexCoord (0x18 each) -> +54 u16 lightSetIndex +56 u16 fogIndex
       +58 RGBA emission, +5C ambient, +60 diffuse, +64 specular0, +68 specular1, +6C..+80 constant0..5,
       +84 blendColor, +88 f32 colorScale, +8C..+A4 7x u32 (lighting / LUT config),
       +A8.. inline PICA commands (lighting LUT input abs/select/scale regs 0x1D0-0x1D2, 0x18 bytes),
       +C0 u32 ??, +C4 ? , +C8 u32 fragmentCommandsPtr, +CC u32 fragmentCommandsWords, +D0 cmdsSrc ptr/count,
       ... LUT sampler names (+E0 table name, +F8 sampler name), +104 shaderReference ("0@FieldChar"),
       +108 modelReference, +10C metaDataPtr."""
    SIZE = 0x2c
    def __init__(self, b, o):
        self.params_ptr = _u32(b, o)
        self.tex_cmd_ptr, self.tex_cmd_words = _u32(b, o + 0x10), _u32(b, o + 0x14)
        self.mapper_ptr = _u32(b, o + 0x18)
        self.texture_names = [_str(b, _u32(b, o + 0x1c + 4 * i)) for i in range(3)]
        self.name = _str(b, _u32(b, o + 0x28))
        p = self.params_ptr
        self.hash = _u32(b, p)
        self.flags = _u16(b, p + 4); self.fragment_flags = b[p + 6]
        self.tex_coords = [TexCoord(b, p + 0x0c + 0x18 * i) for i in range(3)]
        self.light_set_index, self.fog_index = _u16(b, p + 0x54), _u16(b, p + 0x56)
        cols = [_rgba(b, p + 0x58 + 4 * i) for i in range(12)]
        self.emission, self.ambient, self.diffuse, self.specular0, self.specular1 = cols[:5]
        self.constants = cols[5:11]; self.blend_color = cols[11]
        self.color_scale = _f32(b, p + 0x88)
        self.frag_cmd_ptr, self.frag_cmd_words = _u32(b, p + 0xc8), _u32(b, p + 0xcc)
        self.shader_reference = _str(b, _u32(b, p + 0x104))
        self.lut_names = (_str(b, _u32(b, p + 0xe0)), _str(b, _u32(b, p + 0xf8)))
        # -- GPU state from commands --
        fr = pica.State(pica.read_commands(b, self.frag_cmd_ptr, self.frag_cmd_words))
        tx = pica.State(pica.read_commands(b, self.tex_cmd_ptr, self.tex_cmd_words))
        self.frag_state, self.tex_state = fr, tx
        at = fr.get(0x104, 0)
        self.alpha_test = (bool(at & 1), TEST_FUNC[(at >> 4) & 7], (at >> 8) & 0xff)
        cm = fr.get(0x100, 0)
        self.color_op = cm             # bits 8-15: 0 = logic op, 1 = blend
        bf = fr.get(0x101, 0)
        self.blend = (bf & 7, (bf >> 8) & 7, BLEND_FACTOR[(bf >> 16) & 15], BLEND_FACTOR[(bf >> 20) & 15],
                      BLEND_FACTOR[(bf >> 24) & 15], BLEND_FACTOR[(bf >> 28) & 15])
        dc = fr.get(0x107, 0)
        self.depth = dict(test=bool(dc & 1), func=TEST_FUNC[(dc >> 4) & 7], color_mask=(dc >> 8) & 15,
                          write=bool(dc & 0x1000))
        self.cull = CULL.get(fr.get(0x40, 0) & 3, "?")
        self.combiners = [(fr.get(r_), fr.get(r_ + 1), fr.get(r_ + 2), fr.get(r_ + 3), fr.get(r_ + 4))
                          for r_ in (0xc0, 0xc8, 0xd0, 0xd8, 0xf0, 0xf8)]
        en = tx.get(0x80, 0)
        self.units_enabled = [bool(en & (1 << i)) for i in range(3)]
        self.unit_params = []
        for reg in (0x83, 0x93, 0x9b):
            v = tx.get(reg, 0)
            self.unit_params.append(dict(mag_linear=bool(v & 2), min_linear=bool(v & 4),
                                         wrap_t=WRAP[(v >> 8) & 3], wrap_s=WRAP[(v >> 12) & 3],
                                         type=(v >> 28) & 7))
    @property
    def blend_enabled(self):
        return ((self.color_op >> 8) & 0xff) == 1 and not (self.blend[2] == "One" and self.blend[3] == "Zero")

# ----------------------------------------------------------------------------------------
# Meshes
# ----------------------------------------------------------------------------------------
# Vertex-shader input register -> semantic (H3D shader convention, value from regs 0x2BB/0x2BC)
SEMANTICS = {0: "position", 1: "normal", 2: "tangent", 3: "color", 4: "texcoord0", 5: "texcoord1",
             6: "texcoord2", 7: "bone_index", 8: "bone_weight"}
ATTR_TYPES = {0: ("b", 1), 1: ("B", 1), 2: ("h", 2), 3: ("f", 4)}   # PICA: 0 byte,1 ubyte,2 short,3 float
SKINNING = {0: "None", 1: "Smooth", 2: "Rigid"}   # Ohana numbering; heroes only use 1 (verified: model-space verts + weights)

class Attribute:
    def __init__(self, sem, fmt, comps, scale, fixed=None, buf=None, offset=None):
        self.semantic, self.fmt, self.comps, self.scale = sem, fmt, comps, scale
        self.fixed = fixed; self.buffer = buf; self.offset = offset
    def __repr__(self):
        if self.fixed is not None: return "%s=FIXED%s" % (self.semantic, tuple(round(x, 4) for x in self.fixed))
        return "%s:%s%d@+%d*%g" % (self.semantic, self.fmt, self.comps, self.offset, self.scale)

class SubMesh:
    """H3DSubMesh (0x34 bytes): +00 u8 skinning (0 none,1 rigid,2 smooth) +01 u8 pad
       +02 u16 boneIndicesCount +04 u16[20] boneIndices (palette -> skeleton index)
       +2C u32 indexCommandsPtr +30 u32 indexCommandsWords
       Index commands: reg 0x227 (bit31=0 here; data type given by reloc flag 0x27=u16 / 0x28=u8,
       and by bit 31 of 0x227 after Ohana-style patching), 0x228 = index count, 0x25E primitive mode."""
    SIZE = 0x34
    def __init__(self, b, o, idx_types):
        self.skinning = b[o]
        n = _u16(b, o + 2)
        self.bone_indices = list(struct.unpack_from("<%dH" % n, b, o + 4))
        self.cmd_ptr, self.cmd_words = _u32(b, o + 0x2c), _u32(b, o + 0x30)
        st = pica.State(pica.read_commands(b, self.cmd_ptr, self.cmd_words))
        self.index_addr = st.get(0x227) & 0x0fffffff
        self.index_count = st.get(0x228)
        self.primitive = (st.get(0x25e, 0) >> 8) & 3        # 0 = triangle list
        self.index_u16 = idx_types[self.index_addr]
        if self.index_u16:
            self.indices = np.frombuffer(b, "<u2", self.index_count, self.index_addr).astype(np.int32)
        else:
            self.indices = np.frombuffer(b, "u1", self.index_count, self.index_addr).astype(np.int32)

class Mesh:
    """H3DMesh (0x38 bytes):
       +00 u16 materialIndex +02 u16 flags (bit0 silhouette) +04 u16 nodeIndex (mesh-node name / visibility)
       +06 u16 renderPriority (here: u8 priority + u8 layer)
       +08 u32 enableCommandsPtr +0C u32 words  (attribute layout, buffer 0, fixed attribs, scales (uniform c7/c8))
       +10 u32 subMeshesPtr      +14 u32 subMeshCount
       +18 u32 disableCommandsPtr +1C words    (resets buffers 1..11 / fixed attr)
       +20 vec3 meshCenter +2C u32 parentModelPtr +30 u32 0 +34 u32 metaDataPtr (bounding box)"""
    SIZE = 0x38
    def __init__(self, b, o, idx_types):
        self.material_index, self.flags, self.node_index, self.priority = struct.unpack_from("<4H", b, o)
        self.cmd_ptr, self.cmd_words = _u32(b, o + 8), _u32(b, o + 12)
        sm_ptr, sm_n = _u32(b, o + 0x10), _u32(b, o + 0x14)
        self.center = struct.unpack_from("<3f", b, o + 0x20)
        self.submeshes = [SubMesh(b, sm_ptr + SubMesh.SIZE * i, idx_types) for i in range(sm_n)]
        cmds = pica.read_commands(b, self.cmd_ptr, self.cmd_words)
        st = pica.State(cmds)
        self.state = st
        fmt_lo, fmt_hi = st.get(0x201), st.get(0x202)
        fmt64 = fmt_lo | ((fmt_hi & 0xffff) << 32)
        fixed_mask = (fmt_hi >> 16) & 0xfff
        nattr = (fmt_hi >> 28) + 1
        perm = st.get(0x2bb) | (st.get(0x2bc, 0) << 32)
        # scales: uniform c7 / c8 (see MODEL_FORMAT.md for derivation)
        c7 = st.uniforms.get(7, [1, 1, 1, 1]); c8 = st.uniforms.get(8, [1, 1, 1, 1])
        self.pos_offset = st.uniforms.get(6, [0, 0, 0, 0])
        scale_of = {"color": c7[0], "position": c7[1], "normal": c7[2], "tangent": c7[3],
                    "texcoord0": c8[0], "texcoord1": c8[1], "texcoord2": c8[2], "bone_weight": c8[3],
                    "bone_index": 1.0}
        self.raw_scales = (c7, c8)
        attrs = {}
        for a in range(nattr):
            sem = SEMANTICS.get((perm >> (4 * a)) & 0xf, "user%d" % ((perm >> (4 * a)) & 0xf))
            f = (fmt64 >> (4 * a)) & 0xf
            if fixed_mask & (1 << a):
                attrs[a] = Attribute(sem, None, 4, 1.0, fixed=st.fixed.get(a))
            else:
                attrs[a] = Attribute(sem, ATTR_TYPES[f & 3][0], (f >> 2) + 1, scale_of.get(sem, 1.0))
        # buffers
        self.buffers = []
        for n_ in range(12):
            off = st.get(0x203 + 3 * n_); lo = st.get(0x204 + 3 * n_); hi = st.get(0x205 + 3 * n_)
            if hi is None or (hi >> 28) == 0: continue
            stride = (hi >> 16) & 0xff; ncomp = hi >> 28
            comp = [((lo | ((hi & 0xffff) << 32)) >> (4 * k)) & 0xf for k in range(ncomp)]
            pos = 0
            for c in comp:
                if c >= 12:          # padding component (4/8/12/16 bytes)
                    pos += 4 * (c - 11); continue
                at = attrs[c]; t, sz = ATTR_TYPES[[k for k, v in ATTR_TYPES.items() if v[0] == at.fmt][0]]
                pos = (pos + sz - 1) // sz * sz
                at.buffer = n_; at.offset = pos
                pos += sz * at.comps
            self.buffers.append(dict(index=n_, offset=off, stride=stride, components=comp))
        self.attributes = [attrs[a] for a in range(nattr)]
        # vertex count = max index + 1
        nv = max(int(sm.indices.max()) for sm in self.submeshes) + 1
        self.vertex_count = nv
        self.data = {}
        for at in self.attributes:
            if at.fixed is not None:
                self.data[at.semantic] = np.tile(np.array(at.fixed[:4], np.float32), (nv, 1)); continue
            buf = [x for x in self.buffers if x["index"] == at.buffer][0]
            dt = np.dtype("<" + {"b": "i1", "B": "u1", "h": "i2", "f": "f4"}[at.fmt])
            arr = np.ndarray((nv, at.comps), dtype=dt, buffer=b, offset=buf["offset"] + at.offset,
                             strides=(buf["stride"], dt.itemsize)).astype(np.float32)
            if at.fmt != "f" or at.semantic in ("bone_weight",):
                arr = arr * at.scale
            if at.semantic == "bone_index": arr = arr / at.scale if at.scale else arr
            self.data[at.semantic] = arr
        self.vertex_buffer_off = self.buffers[0]["offset"] if self.buffers else None
        self.vertex_stride = self.buffers[0]["stride"] if self.buffers else None

# ----------------------------------------------------------------------------------------
# Model / file
# ----------------------------------------------------------------------------------------
class Model:
    """H3DModel:
       +00 u8 flags +01 u8 boneScaling +02 u16 silhouetteMaterialCount
       +04 mat3x4 worldTransform
       +34 dict materials {u32 ptr(array of 0x2C), u32 count, u32 nameTreePtr}
       +40 u32 meshesPtr (array of 0x38) +44 u32 meshCount
       +48 4x {u32 begin, u32 end} mesh pointers per render layer (opaque/translucent/subtractive/additive)
       +68 u32 subMeshCullingPtr +6C u32 count
       +70 dict skeleton {ptr (array of 0x64), count, nameTreePtr}
       +7C u32 meshNodeVisibilityPtr (bitfield words) +80 u32 meshNodeCount
       +84 u32 namePtr +88 u32 meshNodeNameCount +8C u32 meshNodeNameTreePtr
       +90 u32 layerId +94 u32 metaDataPtr"""
    def __init__(self, b, o, idx_types):
        self.offset = o
        self.flags, self.bone_scaling, self.silhouette_count = struct.unpack_from("<BBH", b, o)
        self.world_transform = mat34(b, o + 4)
        mp, mn = _u32(b, o + 0x34), _u32(b, o + 0x38)
        self.materials = [Material(b, mp + Material.SIZE * i) for i in range(mn)]
        me, mc = _u32(b, o + 0x40), _u32(b, o + 0x44)
        self.layers = [((_u32(b, o + 0x48 + 8 * k) - me) // Mesh.SIZE, (_u32(b, o + 0x4c + 8 * k) - me) // Mesh.SIZE)
                       for k in range(4)]
        self.meshes = [Mesh(b, me + Mesh.SIZE * i, idx_types) for i in range(mc)]
        sp, sn = _u32(b, o + 0x70), _u32(b, o + 0x74)
        self.bones = [Bone(b, sp + Bone.SIZE * i, i) for i in range(sn)]
        vp, vn = _u32(b, o + 0x7c), _u32(b, o + 0x80)
        self.name = _str(b, _u32(b, o + 0x84))
        nn, nt = _u32(b, o + 0x88), _u32(b, o + 0x8c)
        self.mesh_node_names = self._tree_names(b, nt, nn)
        vis = [_u32(b, vp + 4 * k) for k in range((vn + 31) // 32)] if vp else []
        self.mesh_node_visible = [bool((vis[i // 32] >> (i % 32)) & 1) for i in range(vn)] if vis else []
        self.layer_id = _u32(b, o + 0x90)
        # world (bind) matrices
        for bn in self.bones:
            bn.world = bn.local if bn.parent < 0 else self.bones[bn.parent].world @ bn.local

    @staticmethod
    def _tree_names(b, tree, count):
        """Patricia tree: node 0xC bytes {u32 refBit, u16 left, u16 right, u32 namePtr}; node 0 = root (no name).
        Value i corresponds to node i+1."""
        if not tree: return []
        return [_str(b, _u32(b, tree + 12 * (i + 1) + 8)) for i in range(count)]

class LUT:
    def __init__(self, b, o):
        self.name = _str(b, _u32(b, o + 8))
        sp, sn = _u32(b, o), _u32(b, o + 4)
        self.samplers = []
        for i in range(sn):
            s = sp + 0x0c * i
            self.samplers.append(dict(flags=_u32(b, s), cmd_ptr=_u32(b, s + 4), name=_str(b, _u32(b, s + 8))))

class BCH:
    def __init__(self, data):
        self.header = h = Header(data)
        self.raw = data
        self.b, self.reloc_log = relocate(data, h)
        b = self.b
        # index data types from relocation flags (0x27 -> u16, 0x28 -> u8)
        idx_types = {}
        for fl, a, old in self.reloc_log:
            if fl in (0x27, 0x28): idx_types[old + h.raw_off] = (fl == 0x27)
        self.idx_types = idx_types
        self.dicts = {}
        for i, nm in enumerate(SECTION_NAMES):
            o = h.contents_off + 12 * i
            self.dicts[nm] = (_u32(b, o), _u32(b, o + 4), _u32(b, o + 8))
        def ptrs(nm):
            p, n, _ = self.dicts[nm]
            return [_u32(b, p + 4 * i) for i in range(n)]
        self.models = [Model(b, p, idx_types) for p in ptrs("models")]
        self.textures = [Texture(b, p) for p in ptrs("textures")]
        self.luts = [LUT(b, p) for p in ptrs("luts")]
        self.texture_by_name = {t.name: t for t in self.textures}
