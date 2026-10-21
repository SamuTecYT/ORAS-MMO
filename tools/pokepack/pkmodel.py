"""Carga un Pokémon 3D de combate de ORAS (GARC /a/0/0/8 de TU copia del juego) listo para dibujar.
Loads an ORAS 3D battle Pokémon (GARC /a/0/0/8 of YOUR copy of the game), ready to draw.

Estructura de /a/0/0/8 (verificada): entrada 0 = índice; 1 y 2 = comunes; cada forma ocupa 8 entradas desde la 3:
  3+8f   "PC" (2 BCH): [0] modelo pmXXXX_YY, [1] modelo de sombra
  4+8f   "PF"  (animaciones de material)        5+8f "PT" texturas normales     6+8f "PT" texturas variocolor
  7+8f   "PT"  (extra)   8+8f "PB"   9+8f "PK"   10+8f "PC" (datos pequeños)
Índice (entrada 0): 722 x {u16 primera_forma, u8 n_formas, u8 flags} (posición i = especie i+1), luego 1008 x u16.
"""
import struct
import numpy as np

from lgarc import LazyGarc
from romfs import RomFS
import bch
import pica

_ROM = None
_G = None


def set_rom(path):
    """.cxi / .3ds descifrado o carpeta con la RomFS extraída / decrypted .cxi / .3ds or extracted RomFS folder"""
    global _ROM, _G
    _ROM, _G = path, None


def garc():
    global _G
    if _G is None:
        if _ROM is None:
            raise RuntimeError("call pkmodel.set_rom(path) first")
        _G = LazyGarc("/a/0/0/8", RomFS(_ROM))
    return _G


def unpack(d):
    n = struct.unpack_from("<H", d, 2)[0]
    offs = struct.unpack_from("<%dI" % (n + 1), d, 4)
    return [d[offs[i]:offs[i + 1]] for i in range(n)]


def species_forms():
    """especie (1..721) -> (primera_forma, n_formas, flags)"""
    idx = garc().get(0)
    return {i + 1: struct.unpack_from("<HBB", idx, 4 * i) for i in range(721)}


def lut_tables(B):
    """nombre de sampler -> np.array(256) (valor de 12 bits / 4095), tal cual la tabla PICA."""
    b = B.b
    out = {}
    p, n, _ = B.dicts["luts"]
    for i in range(n):
        o = bch._u32(b, p + 4 * i)
        sp, sn = bch._u32(b, o), bch._u32(b, o + 4)
        for k in range(sn):
            fl, cp, cl, nm = struct.unpack_from("<4I", b, sp + 0x10 * k)
            vals = [v for reg, v, mk in pica.read_commands(b, cp, cl) if 0x1c8 <= reg <= 0x1cf][:256]
            if len(vals) == 256:
                out[bch._str(b, nm)] = np.array([(x & 0xfff) / 4095.0 for x in vals], np.float32)
    return out


class Form:
    """Un Pokémon (forma) con mallas en pose de reposo, materiales, texturas y tablas de luz."""

    def __init__(self, form, shiny=False):
        self.form = form
        g = garc()
        pc = unpack(g.get(3 + 8 * form))
        self.B = bch.BCH(pc[0])
        self.model = self.B.models[0]
        self.name = self.model.name
        T = bch.BCH(unpack(g.get((6 if shiny else 5) + 8 * form))[0])
        self.tex = {t.name: t for t in T.textures}
        self.luts = lut_tables(self.B)
        m = self.model
        self.meshes = []
        for me in m.meshes:
            vis = True
            if m.mesh_node_visible and me.node_index < len(m.mesh_node_visible):
                vis = m.mesh_node_visible[me.node_index]
            d = me.data
            P = d["position"][:, :3].astype(np.float32)
            N = d["normal"][:, :3].astype(np.float32) if "normal" in d else np.tile([0, 1, 0], (len(P), 1)).astype(np.float32)
            Tg = d["tangent"][:, :3].astype(np.float32) if "tangent" in d else np.tile([1, 0, 0], (len(P), 1)).astype(np.float32)
            UV = d["texcoord0"][:, :2].astype(np.float32) if "texcoord0" in d else np.zeros((len(P), 2), np.float32)
            idx = np.concatenate([sm.indices for sm in me.submeshes]).astype(np.int32)
            mat = m.materials[me.material_index]
            if not hasattr(mat, "lut_rgb"):
                pp = mat.params_ptr
                mat.lut_rgb = [bch._str(self.B.b, bch._u32(self.B.b, pp + o)) if bch._u32(self.B.b, pp + o) else None
                               for o in (0xF8, 0xFC, 0x100)]      # tablas de ReflecR / ReflecG / ReflecB
                # repetición real de cada unidad: está en los "texture mappers" (0x10 bytes por unidad), no en los
                # comandos 0x83/0x93/0x9B (son de relleno: 0x2220). Byte 1 = U, byte 2 = V (códigos PICA:
                # 0 borde, 1 color de borde, 2 repetir, 3 espejo). Verificado con la boca y los ojos de Pikachu.
                W = {0: "ClampToEdge", 1: "ClampToBorder", 2: "Repeat", 3: "Mirror"}
                for u in range(3):
                    w0 = bch._u32(self.B.b, mat.mapper_ptr + 0x10 * u)
                    mat.unit_params[u]["wrap_s"] = W[(w0 >> 8) & 3]
                    mat.unit_params[u]["wrap_t"] = W[(w0 >> 16) & 3]
            # efectos con plantilla (stencil) de fuego/gas: "FireGeom*" (forma de la máscara) y "FireSten*" (escritura de
            # la plantilla) no tienen sentido en un sprite plano (salían como bloques blancos en Koffing/Weezing)
            if mat.name.startswith("FireGeom") or mat.name.startswith("FireSten"):
                vis = False
            self.meshes.append(dict(mesh=me, mat=mat, P=P, N=N, T=Tg, UV=UV,
                                    idx=idx, visible=vis, priority=me.priority))
        # fuego/gas: el color real está en las constantes del pase "FireSten" (borde = etapa 0, centro = etapa 1);
        # "FireCore" solo escribe un valor de marca. Se empareja por sufijo (FireCoreB1 <-> FireStenB1).
        sten = {x["mat"].name[len("FireSten"):]: x["mat"] for x in self.meshes if x["mat"].name.startswith("FireSten")}
        for x in self.meshes:
            nm = x["mat"].name
            if nm.startswith("FireCore") and sten:
                st = sten.get(nm[len("FireCore"):]) or next(iter(sten.values()))
                k = [(c[3] or 0) for c in st.combiners]
                x["fire"] = tuple(tuple(((k[i] >> (8 * j)) & 255) / 255.0 for j in range(3)) for i in (0, 1))
                x["gas"] = x["mat"].texture_names[0] in (None, "DummyTex")
        allP = np.concatenate([x["P"] for x in self.meshes if x["visible"]])
        self.lo, self.hi = allP.min(0), allP.max(0)

    def pose(self, slot=0, frame=0.0, battle=None, entry=8, extra=None):
        """Pone el Pokémon en el cuadro 'frame' de su movimiento de combate 'slot' (0 = reposo): esqueleto (GF1 + IK),
        visibilidad de las piezas y constantes de material (coordenadas de ojos/boca). Las mallas pasan a tener
        P/N/T/UV/C/idx ya deformados (piel suave: todos los submallas de Gen 6 usan vértices en espacio de modelo)."""
        from pkanim import Battle, anim
        bt = battle or Battle(self.form, entry)
        mot = bt.motion(slot)
        m = self.model
        W = anim.sample_pose(m, mot, frame) if mot else [b.world for b in m.bones]
        if extra is not None: W = extra(self, W)              # p. ej. el paso procedural (walk.py)
        S = np.array([W[b.index] @ b.inv_bind for b in m.bones])
        vis = bt.visibility(slot, frame)
        mats = bt.material(slot)
        for x in self.meshes:
            me = x["mesh"]
            node = m.mesh_node_names[me.node_index] if me.node_index < len(m.mesh_node_names) else None
            if node in vis and not (x["mat"].name.startswith("FireGeom") or x["mat"].name.startswith("FireSten")):
                x["visible"] = vis[node]
            # coordenadas de textura animadas (constantes en el reposo)
            xf = []
            for u in range(3):
                tc = x["mat"].tex_coords[u]
                sc = list(tc.scale); tr = list(tc.translation)
                for k, v in enumerate(mats.get((x["mat"].name, 0x13 + 3 * u), [])):
                    if v is not None and k < 2: sc[k] = v
                for k, v in enumerate(mats.get((x["mat"].name, 0x15 + 3 * u), [])):
                    if v is not None and k < 2: tr[k] = v
                xf.append((sc[0], sc[1], tr[0], tr[1]))
            x["xf"] = xf
            d = me.data
            atr = {a_.semantic: a_ for a_ in me.attributes}
            bi = d.get("bone_index"); bw = d.get("bone_weight")
            if "bone_weight" in atr and atr["bone_weight"].fixed is None: nc = atr["bone_weight"].comps
            elif "bone_index" in atr and atr["bone_index"].fixed is None: nc = atr["bone_index"].comps
            else: nc = 1
            P0, N0, T0 = x["P"], x["N"], x["T"]
            col = d.get("color")
            C0 = np.ones((len(P0), 4), np.float32) if col is None else col[:, :4].astype(np.float32)
            if col is not None and C0.max() > 1.001: C0 = C0 / 255.0
            if C0.shape[1] < 4: C0 = np.concatenate([C0, np.ones((len(C0), 4 - C0.shape[1]), np.float32)], 1)
            Ps, Ns, Ts, UVs, Cs, Is = [], [], [], [], [], []
            base = 0
            for sm in me.submeshes:
                used = np.unique(sm.indices)
                remap = np.full(len(P0), -1, np.int64); remap[used] = np.arange(len(used))
                pal = np.array(sm.bone_indices if sm.bone_indices else [0])
                if bi is None:
                    idx = np.zeros((len(used), 1), int); wt = np.ones((len(used), 1), np.float32)
                else:
                    idx = np.clip(bi[used, :nc].astype(int), 0, len(pal) - 1)
                    wt = bw[used, :nc].astype(np.float32) if (bw is not None and nc > 1) else np.ones((len(used), nc), np.float32)
                    if bw is not None and nc == 1 and "bone_weight" in atr and atr["bone_weight"].fixed is None:
                        wt = bw[used, :1].astype(np.float32)
                ws = wt.sum(1, keepdims=True)
                wt = np.where(ws > 1e-6, wt / np.maximum(ws, 1e-6), np.eye(1, nc, dtype=np.float32))
                M = S[pal[idx]]                       # (n, nc, 4, 4)
                p = P0[used]; n_ = N0[used]; t_ = T0[used]
                Pp = np.einsum("nk,nkij,nj->ni", wt, M[:, :, :3, :3], p) + np.einsum("nk,nki->ni", wt, M[:, :, :3, 3])
                Np = np.einsum("nk,nkij,nj->ni", wt, M[:, :, :3, :3], n_)
                Tp = np.einsum("nk,nkij,nj->ni", wt, M[:, :, :3, :3], t_)
                Np /= np.maximum(np.linalg.norm(Np, axis=1, keepdims=True), 1e-9)
                Tp /= np.maximum(np.linalg.norm(Tp, axis=1, keepdims=True), 1e-9)
                Ps.append(Pp); Ns.append(Np); Ts.append(Tp); UVs.append(x["UV"][used]); Cs.append(C0[used])
                Is.append(remap[sm.indices] + base); base += len(used)
            x["Pp"] = np.concatenate(Ps).astype(np.float32); x["Np"] = np.concatenate(Ns).astype(np.float32)
            x["Tp"] = np.concatenate(Ts).astype(np.float32); x["UVp"] = np.concatenate(UVs).astype(np.float32)
            x["Cp"] = np.concatenate(Cs).astype(np.float32); x["idxp"] = np.concatenate(Is).astype(np.int32)
        if not any(x["visible"] for x in self.meshes if "Pp" in x):     # la animación lo ocultaría todo: ignorarla
            for x in self.meshes:
                x["visible"] = not (x["mat"].name.startswith("FireGeom") or x["mat"].name.startswith("FireSten"))
        allP = np.concatenate([x["Pp"] for x in self.meshes if x["visible"]])
        self.lo, self.hi = allP.min(0), allP.max(0)
        return self

    def height(self):
        return float(self.hi[1] - max(self.lo[1], 0.0))
