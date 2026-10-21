"""GARC perezoso: lee solo la tabla y las entradas pedidas (para /a/0/0/8, 1 GB).
Lazy GARC reader: only the table and the requested entries are read (/a/0/0/8 is 1 GB)."""
import struct


def lz11_decompress(d):
    if d[0] != 0x11: return None
    size = d[1] | d[2] << 8 | d[3] << 16; pos = 4
    if size == 0: size = struct.unpack("<I", d[4:8])[0]; pos = 8
    out = bytearray()
    while len(out) < size:
        flags = d[pos]; pos += 1
        for i in range(8):
            if len(out) >= size: break
            if flags & (0x80 >> i):
                b = d[pos]; ind = b >> 4
                if ind == 0:
                    ln = ((b & 0xF) << 4 | d[pos+1] >> 4) + 0x11; disp = ((d[pos+1] & 0xF) << 8 | d[pos+2]) + 1; pos += 3
                elif ind == 1:
                    ln = ((b & 0xF) << 12 | d[pos+1] << 4 | d[pos+2] >> 4) + 0x111; disp = ((d[pos+2] & 0xF) << 8 | d[pos+3]) + 1; pos += 4
                else:
                    ln = ind + 1; disp = ((b & 0xF) << 8 | d[pos+1]) + 1; pos += 2
                for _ in range(ln): out.append(out[-disp])
            else:
                out.append(d[pos]); pos += 1
    return bytes(out)


class LazyGarc:
    def __init__(self, path, rom):
        self.f, self.base, self.size = rom.open(path)
        f = self.f
        f.seek(self.base); h = f.read(0x20)
        assert h[:4] == b"CRAG"
        hsize, = struct.unpack("<I", h[4:8]); self.data_off, = struct.unpack("<I", h[0x10:0x14])
        f.seek(self.base + hsize); fh = f.read(12)
        assert fh[:4] == b"OTAF"; sz, n = struct.unpack("<IH", fh[4:10])
        f.seek(self.base + hsize); fato_blk = f.read(sz)
        fato = fato_blk[12:12 + 4 * n]
        f.seek(self.base + hsize + sz); bh = f.read(12)
        assert bh[:4] == b"BTAF"; sz2, nb = struct.unpack("<II", bh[4:12])
        f.seek(self.base + hsize + sz); fatb_blk = f.read(sz2)
        self.entries = []
        for i in range(n):
            off = struct.unpack("<I", fato[4 * i:4 * i + 4])[0]; q = 12 + off
            vec, = struct.unpack("<I", fatb_blk[q:q + 4]); q += 4; subs = []
            for b in range(32):
                if vec & (1 << b):
                    s, e, l = struct.unpack("<III", fatb_blk[q:q + 12]); q += 12
                    subs.append((s, e))
            self.entries.append(subs)

    def __len__(self): return len(self.entries)

    def get(self, i, sub=0, decompress=True):
        s, e = self.entries[i][sub]
        f = self.f; f.seek(self.base + self.data_off + s); raw = f.read(e - s)
        if decompress and raw[:1] == b"\x11":
            try:
                d = lz11_decompress(raw)
                if d: return d
            except Exception: pass
        return raw
