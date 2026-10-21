"""Animaciones de combate de los Pokémon de ORAS (entrada 8+8f de /a/0/0/8, contenedor "PB", 74 sub-archivos):
  sub 0      paquete de movimientos GF1 (mismo formato que los del protagonista: gfmotion.py)
  sub 2..19  visibilidad por ranura de movimiento (BCH visibility_anims), ranura s -> sub 2+s
  sub 20..37 y 38..55  animaciones de material (BCH material_anims), ranura s -> 20+s y 38+s
  sub 56..73 sonidos (SESD)
Ranura 0 = "wait" (reposo en combate, bucle). Verificado en 15 especies.

Elemento de visibilidad (0x1C bytes): u32 nombre (nodo de malla), u32 tipo 0x0008002F, f32 inicio, f32 fin,
  u32 0xFFFF0000, u32 puntero a bits (un bit por cuadro), u32 nº de palabras.
Elemento de material: u32 nombre (material), u32 tipo (bits 0-15 destino, 16-19 nº de componentes: 2 = vec2),
  u32 flags (bit k = componente k constante), luego un u32 por componente (f32 si es constante).
  Destinos: 0x13 + 3*u = escala de la unidad u, +1 rotación, +2 traslación.
"""
import struct
import gfmotion as anim
import bch
from pkmodel import garc, unpack


class Battle:
    """entry 8 = combate ("PB", 18 ranuras), 9 = Poké Recreo/Amie ("PK", 26 ranuras). Mismo reparto:
    2 + N visibilidad, 2 + N material A, 2 + 2N material B, 2 + 3N sonidos."""
    def __init__(self, form, entry=8):
        self.subs = unpack(garc().get(entry + 8 * form))
        self.pack = anim.MotionPack(self.subs[0])
        self.n = (len(self.subs) - 2) // 4

    def motion(self, slot=0):
        return self.pack.motions.get(slot)

    def visibility(self, slot=0, frame=0):
        """{nombre de nodo de malla: visible} en ese cuadro (solo los nodos que la animación toca)."""
        k = 2 + slot
        if slot >= self.n or k >= len(self.subs) or self.subs[k][:3] != b"BCH": return {}
        B = bch.BCH(self.subs[k]); b = B.b
        p, n, _ = B.dicts["visibility_anims"]
        out = {}
        for a in range(n):
            o = bch._u32(b, p + 4 * a)
            tab, ne = bch._u32(b, o + 0xC), bch._u32(b, o + 0x10)
            for i in range(ne):
                e = bch._u32(b, tab + 4 * i)
                name = bch._str(b, bch._u32(b, e))
                start, end = struct.unpack_from("<2f", b, e + 8)
                dp, dn = bch._u32(b, e + 0x14), bch._u32(b, e + 0x18)
                if not dn: continue
                f = max(0, int(frame - start))
                f = min(f, max(0, int(end - start)), dn * 32 - 1)      # fuera de la curva: el último valor
                w = min(f // 32, dn - 1)
                out[name] = bool((bch._u32(b, dp + 4 * w) >> (f % 32)) & 1)
        return out

    def material(self, slot=0):
        """{(material, destino): [valores constantes]} de las dos animaciones de material de la ranura."""
        out = {}
        for base in (2 + self.n, 2 + 2 * self.n):
            k = base + slot
            if slot >= self.n: continue
            if k >= len(self.subs) or self.subs[k][:3] != b"BCH": continue
            B = bch.BCH(self.subs[k]); b = B.b
            p, n, _ = B.dicts["material_anims"]
            for a in range(n):
                o = bch._u32(b, p + 4 * a)
                tab, ne = bch._u32(b, o + 0xC), bch._u32(b, o + 0x10)
                for i in range(ne):
                    e = bch._u32(b, tab + 4 * i)
                    name = bch._str(b, bch._u32(b, e))
                    ty, fl = bch._u32(b, e + 4), bch._u32(b, e + 8)
                    nc = {0: 1, 1: 1, 2: 2, 3: 4}.get((ty >> 16) & 0xF, 1)
                    vals = []
                    for c in range(nc):
                        w = bch._u32(b, e + 12 + 4 * c)
                        vals.append(struct.unpack("<f", struct.pack("<I", w))[0] if (fl >> c) & 1 else None)
                    out[(name, ty & 0xFFFF)] = vals
        return out
