"""PICA200 GPU command list reader (3DS)."""
import struct

def read_commands(buf, off, nwords):
    """Returns list of (reg, value, mask) in execution order."""
    out = []
    w = struct.unpack_from('<%dI' % nwords, buf, off)
    i = 0
    while i + 1 < nwords:
        param, hdr = w[i], w[i + 1]
        reg = hdr & 0xffff; mask = (hdr >> 16) & 0xf; extra = (hdr >> 20) & 0x7ff; consec = hdr >> 31
        i += 2
        vals = [param] + list(w[i:i + extra]); i += extra
        for k, v in enumerate(vals):
            out.append((reg + k if consec else reg, v, mask))
        if i & 1: i += 1   # align to 8 bytes
    return out

def f24(v):
    v &= 0xffffff
    if v & 0x7fffff == 0: return -0.0 if v >> 23 else 0.0
    s = v >> 23; e = (v >> 16) & 0x7f; m = v & 0xffff
    return (-1.0 if s else 1.0) * (1.0 + m / 65536.0) * 2.0 ** (e - 63)

def f31(v):  # float32 bits
    return struct.unpack('<f', struct.pack('<I', v & 0xffffffff))[0]

class State:
    """Accumulates register state + vertex shader float uniforms + fixed attributes."""
    def __init__(self, cmds):
        self.regs = {}
        self.writes = cmds
        self.uniforms = {}      # vs float uniform index -> [x,y,z,w]
        self.fixed = {}         # fixed attribute index -> [x,y,z,w]
        uidx = 0; f32mode = False; ubuf = []
        fidx = 0; fbuf = []
        for reg, v, mask in cmds:
            self.regs[reg] = v
            if reg == 0x2c0:
                uidx = v & 0xff; f32mode = bool(v >> 31); ubuf = []
            elif 0x2c1 <= reg <= 0x2c8:
                ubuf.append(v)
                n = 4 if f32mode else 3
                if len(ubuf) == n:
                    if f32mode:
                        w_, z_, y_, x_ = [f31(t) for t in ubuf]
                    else:
                        a, b, c = ubuf   # 96 bits: w z y x (24 each)
                        w_ = f24(a >> 8); z_ = f24(((a & 0xff) << 16) | (b >> 16))
                        y_ = f24(((b & 0xffff) << 8) | (c >> 24)); x_ = f24(c & 0xffffff)
                    self.uniforms[uidx] = [x_, y_, z_, w_]; uidx += 1; ubuf = []
            elif reg == 0x232:
                fidx = v & 0xf; fbuf = []
            elif 0x233 <= reg <= 0x235:
                fbuf.append(v)
                if len(fbuf) == 3:
                    a, b, c = fbuf
                    w_ = f24(a >> 8); z_ = f24(((a & 0xff) << 16) | (b >> 16))
                    y_ = f24(((b & 0xffff) << 8) | (c >> 24)); x_ = f24(c & 0xffffff)
                    self.fixed[fidx] = [x_, y_, z_, w_]; fidx += 1; fbuf = []
    def get(self, reg, d=None): return self.regs.get(reg, d)
