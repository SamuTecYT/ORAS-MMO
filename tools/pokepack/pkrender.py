"""Dibuja un Pokémon 3D de ORAS con la tarjeta gráfica (moderngl, contexto sin ventana), imitando el aspecto del
juego: los 6 combinadores de textura PICA (TEV) de cada material se traducen a GLSL tal cual (como el generador de
shaders de Citra) y la luz de fragmento se emula con las tablas (LUT) del propio modelo sobre N·V (lo que selecciona
el registro 0x1D1 de los materiales "PokePack"), con el mapa de normales de la unidad 2.

    r = Renderer(); img, (ox, oy) = r.render(form, yaw, elev, ppu, scale)
      img: RGBA float32 (sin premultiplicar), (ox, oy): píxel del origen del modelo (entre los pies)
"""
import math
import numpy as np
import moderngl

WRAP_MODE = {"ClampToEdge": 0, "ClampToBorder": 0, "Repeat": 1, "Mirror": 2}

VS = """
#version 330
uniform mat4 uMVP;
uniform mat3 uNrm;
in vec3 aPos; in vec3 aNrm; in vec3 aTan; in vec2 aUV; in vec4 aCol;
out vec3 vN; out vec3 vT; out vec2 vUV; out vec4 vCol;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vN = uNrm * aNrm; vT = uNrm * aTan; vUV = aUV; vCol = aCol;
}
"""

FS_HEAD = """
#version 330
uniform sampler2D t0; uniform sampler2D t1; uniform sampler2D t2;
uniform sampler2D lutR; uniform sampler2D lutG; uniform sampler2D lutB;
uniform vec4 xf0; uniform vec4 xf1; uniform vec4 xf2;
uniform ivec2 wr0; uniform ivec2 wr1; uniform ivec2 wr2;
uniform ivec3 uMap;         // generación de coordenadas por unidad: 0 = UV, 2 = esfera de entorno (cámara)
uniform vec3 uL;
uniform int  uBump;
uniform float uAlphaRef; uniform int uAlphaFunc; uniform int uOpaque; uniform int uMembrane;
in vec3 vN; in vec3 vT; in vec2 vUV; in vec4 vCol;
layout(location = 0) out vec4 oColor;
float lutS(sampler2D L, float x) {           // entrada con signo (reg 0x1D0: abs desactivado)
    float p = clamp(x, -1.0, 1.0) * 128.0;
    float i = clamp(floor(p), -128.0, 127.0);
    float f = clamp(p - i, 0.0, 1.0);
    int a = int(i); int b = min(a + 1, 127);
    if (a < 0) { a += 256; b = (a == 255) ? 0 : a + 1; }
    return mix(texelFetch(L, ivec2(a, 0), 0).r, texelFetch(L, ivec2(b, 0), 0).r, f);
}
float wrap1(float u, int m) {
    if (m == 1) return fract(u);
    if (m == 2) { float t = mod(u, 2.0); return t > 1.0 ? 2.0 - t : t; }
    return clamp(u, 0.0, 1.0);
}
vec2 gN;                    // normal de vista (para el mapa de esfera)
vec4 samp(sampler2D t, vec4 xf, ivec2 wr, int mp) {
    vec2 base = mp == 2 ? gN * 0.5 + 0.5 : vUV;
    vec2 u = base * xf.xy + xf.zw;
    vec2 g = vec2(wrap1(u.x, wr.x), 1.0 - wrap1(u.y, wr.y));
    return textureGrad(t, g, dFdx(u) * vec2(1.0, -1.0), dFdy(u) * vec2(1.0, -1.0));
}
void main() {
    vec3 N = normalize(vN);
    if (!gl_FrontFacing) N = -N;
    gN = N.xy;
    vec4 tex0 = samp(t0, xf0, wr0, uMap.x);
    vec4 tex1 = samp(t1, xf1, wr1, uMap.y);
    vec4 tex2 = samp(t2, xf2, wr2, uMap.z);
    if (uBump == 1) {
        vec3 n = tex2.rgb * 2.0 - 1.0;
        vec3 T = normalize(vT - N * dot(N, vT));
        vec3 B = cross(N, T);
        N = normalize(T * n.x + B * n.y + N * n.z);
    }
    float nv = N.z;
    vec4 fragPri = vec4(vec3(0.62 + 0.38 * max(dot(N, uL), 0.0)), 1.0);
    // alfa secundario = Fresnel aproximado (membranas, cristal: borde visible y centro transparente)
    vec4 fragSec = vec4(lutS(lutR, nv), lutS(lutG, nv), lutS(lutB, nv), pow(clamp(1.0 - nv, 0.0, 1.0), 1.5));
    // color de vértice: en los materiales "PokePack" su alfa suma un brillo (agua, metal...). El shader de vértices del
    // juego lo limita al borde; aquí se aproxima con (1 - N·V)^2 (sin esto, Gyarados o Seadra salen casi blancos).
    vec4 primary = vec4(vCol.rgb, vCol.a * pow(clamp(1.0 - nv, 0.0, 1.0), 2.0));
"""

FS_TAIL = """
    if (uAlphaFunc == 1) discard;
    if (uAlphaFunc == 4 && !(prev.a < uAlphaRef)) discard;
    if (uAlphaFunc == 5 && !(prev.a <= uAlphaRef)) discard;
    if (uAlphaFunc == 6 && !(prev.a > uAlphaRef)) discard;
    if (uAlphaFunc == 7 && !(prev.a >= uAlphaRef)) discard;
    if (uMembrane == 1) prev.a = 0.85 * pow(clamp(1.0 - nv, 0.0, 1.0), 1.5);   // membrana/cristal: solo el borde
    if (uMembrane == 2) prev.a = min(1.0, prev.a * 2.5);   // llamas con textura (Lampent): que no se pierdan al 50 %
    oColor = uOpaque == 1 ? vec4(prev.rgb, 1.0) : prev;   // sin mezcla, el alfa del combinador no se ve
}
"""

FS_FIRE = """
#version 330
uniform sampler2D t0; uniform vec4 xf0; uniform ivec2 wr0;
uniform vec3 uEdge; uniform vec3 uCore;
in vec3 vN; in vec3 vT; in vec2 vUV; in vec4 vCol;
layout(location = 0) out vec4 oColor;
float wrap1(float u, int m) {
    if (m == 1) return fract(u);
    if (m == 2) { float t = mod(u, 2.0); return t > 1.0 ? 2.0 - t : t; }
    return clamp(u, 0.0, 1.0);
}
void main() {
    vec2 u = vUV * xf0.xy + xf0.zw;
    float g = texture(t0, vec2(wrap1(u.x, wr0.x), 1.0 - wrap1(u.y, wr0.y))).r;
    oColor = vec4(mix(uEdge, uCore, g), 1.0);
}
"""

ALPHA_FUNC = {"Never": 1, "Always": 0, "Equal": 0, "NotEqual": 0, "Less": 4, "LEqual": 5, "Greater": 6, "GEqual": 7}
BLEND_GL = {"Zero": moderngl.ZERO, "One": moderngl.ONE, "SrcColor": moderngl.SRC_COLOR,
            "1-SrcColor": moderngl.ONE_MINUS_SRC_COLOR, "DstColor": moderngl.DST_COLOR,
            "1-DstColor": moderngl.ONE_MINUS_DST_COLOR, "SrcAlpha": moderngl.SRC_ALPHA,
            "1-SrcAlpha": moderngl.ONE_MINUS_SRC_ALPHA, "DstAlpha": moderngl.DST_ALPHA,
            "1-DstAlpha": moderngl.ONE_MINUS_DST_ALPHA, "ConstColor": moderngl.SRC_ALPHA,
            "1-ConstColor": moderngl.ONE_MINUS_SRC_ALPHA, "ConstAlpha": moderngl.SRC_ALPHA,
            "1-ConstAlpha": moderngl.ONE_MINUS_SRC_ALPHA, "SrcAlphaSaturate": moderngl.SRC_ALPHA}


def _src(code, i):
    return {0: "primary", 1: "fragPri", 2: "fragSec", 3: "tex0", 4: "tex1", 5: "tex2", 6: "tex2",
            13: "cbuf", 14: "konst%d" % i, 15: "prev"}.get(code, "vec4(0.0)")


def _rgb_mod(s, m):
    return {0: "%s.rgb" % s, 1: "(vec3(1.0) - %s.rgb)" % s, 2: "vec3(%s.a)" % s, 3: "vec3(1.0 - %s.a)" % s,
            4: "vec3(%s.r)" % s, 5: "vec3(1.0 - %s.r)" % s, 8: "vec3(%s.g)" % s, 9: "vec3(1.0 - %s.g)" % s,
            12: "vec3(%s.b)" % s, 13: "vec3(1.0 - %s.b)" % s}.get(m, "%s.rgb" % s)


def _a_mod(s, m):
    return {0: "%s.a" % s, 1: "(1.0 - %s.a)" % s, 2: "%s.r" % s, 3: "(1.0 - %s.r)" % s, 4: "%s.g" % s,
            5: "(1.0 - %s.g)" % s, 6: "%s.b" % s, 7: "(1.0 - %s.b)" % s}.get(m, "%s.a" % s)


def _op(op, a, b, c, one, half):
    return {0: a, 1: "%s * %s" % (a, b), 2: "%s + %s" % (a, b), 3: "%s + %s - %s" % (a, b, half),
            4: "%s * %s + %s * (%s - %s)" % (a, c, b, one, c), 5: "%s - %s" % (a, b),
            8: "%s * %s + %s" % (a, b, c), 9: "min(%s + %s, %s) * %s" % (a, b, one, c)}.get(op, a)


def combiner_glsl(mat):
    """GLSL de los 6 combinadores del material (registros 0xC0.., búfer 0xE0/0xFD), como el generador de Citra."""
    fs = mat.frag_state
    e0 = fs.get(0xE0, 0) or 0
    bufc = fs.get(0xFD, 0) or 0
    out = [FS_HEAD, "    vec4 cbuf = vec4(0.0);\n",
           "    vec4 ncbuf = vec4(%f, %f, %f, %f);\n" % tuple(((bufc >> (8 * k)) & 255) / 255.0 for k in range(4)),
           "    vec4 prev = vec4(0.0);\n"]
    for i, c in enumerate(mat.combiners):
        src, opd, op, col, sc = [x or 0 for x in c]
        out.append("    cbuf = ncbuf;\n")
        out.append("    vec4 konst%d = vec4(%f, %f, %f, %f);\n" % ((i,) + tuple(((col >> (8 * k)) & 255) / 255.0 for k in range(4))))
        rs = [_src((src >> (4 * k)) & 15, i) for k in range(3)]
        as_ = [_src((src >> (16 + 4 * k)) & 15, i) for k in range(3)]
        rm = [_rgb_mod(rs[k], (opd >> (4 * k)) & 15) for k in range(3)]
        am = [_a_mod(as_[k], (opd >> (12 + 3 * k)) & 7) for k in range(3)]
        rop, aop = op & 15, (op >> 16) & 15
        dot = "4.0 * dot(%s - vec3(0.5), %s - vec3(0.5))" % (rm[0], rm[1])
        rgb = "vec3(%s)" % dot if rop in (6, 7) else _op(rop, rm[0], rm[1], rm[2], "vec3(1.0)", "vec3(0.5)")
        alp = dot if rop == 7 else (_op(aop, am[0], am[1], am[2], "1.0", "0.5") if aop not in (6, 7) else
                                    "4.0 * ((%s - 0.5) * (%s - 0.5))" % (am[0], am[1]))
        rscale, ascale = 1 << (sc & 3), 1 << ((sc >> 16) & 3)
        out.append("    prev = vec4(min(clamp(%s, 0.0, 1.0) * %d.0, vec3(1.0)), min(clamp(%s, 0.0, 1.0) * %d.0, 1.0));\n"
                   % (rgb, rscale, alp, ascale))
        if i < 4:
            if (e0 >> (8 + i)) & 1: out.append("    ncbuf.rgb = prev.rgb;\n")
            if (e0 >> (12 + i)) & 1: out.append("    ncbuf.a = prev.a;\n")
    out.append(FS_TAIL)
    return "".join(out)


def view_matrix(yaw_deg, elev_deg):
    """Rotación modelo -> vista: el modelo (mira a +Z) gira 'yaw' (0 = de frente a la cámara, 90 = mira a la derecha
    de la pantalla) y la cámara lo ve desde arriba con 'elev' grados de inclinación."""
    e = math.radians(elev_deg); y = math.radians(yaw_deg)
    Ry = np.array([[math.cos(y), 0, math.sin(y)], [0, 1, 0], [-math.sin(y), 0, math.cos(y)]])
    Rc = np.array([[1, 0, 0], [0, math.cos(e), -math.sin(e)], [0, math.sin(e), math.cos(e)]])
    return Rc @ Ry


class Renderer:
    def __init__(self):
        self.ctx = moderngl.create_standalone_context()
        self.progs = {}
        self.white = self._tex(np.full((1, 1, 4), 255, np.uint8))
        self.gray = self._tex(np.array([[[128, 128, 128, 255]]], np.uint8))   # "DummyTex": gris neutro opaco
        self.flat = self._tex(np.array([[[128, 128, 255, 255]]], np.uint8))
        self.zero_lut = self._lut(np.zeros(256, np.float32))
        self.one_lut = self._lut(np.ones(256, np.float32))

    def _tex(self, rgba):
        h, w = rgba.shape[:2]
        t = self.ctx.texture((w, h), 4, np.ascontiguousarray(rgba, np.uint8).tobytes())
        t.build_mipmaps()
        t.filter = (moderngl.LINEAR_MIPMAP_LINEAR, moderngl.LINEAR)
        t.repeat_x = t.repeat_y = True
        return t

    def _lut(self, arr):
        t = self.ctx.texture((256, 1), 1, np.ascontiguousarray(arr, np.float32).tobytes(), dtype="f4")
        t.filter = (moderngl.NEAREST, moderngl.NEAREST)
        return t

    def _prog(self, mat):
        key = (tuple(tuple(x or 0 for x in c) for c in mat.combiners), mat.frag_state.get(0xE0, 0), mat.frag_state.get(0xFD, 0))
        if key not in self.progs:
            self.progs[key] = self.ctx.program(vertex_shader=VS, fragment_shader=combiner_glsl(mat))
        return self.progs[key]

    @staticmethod
    def _set(prog, name, value):
        if name in prog:
            prog[name].value = value

    def bounds_px(self, F, yaw, elev, ppu, scale, P_extra=None):
        V = view_matrix(yaw, elev)
        pts = np.concatenate([m.get("Pp", m["P"]) for m in F.meshes if m["visible"]])
        Pv = (V @ pts.T).T * scale * ppu
        return Pv

    def render(self, F, yaw, elev, ppu, scale, bump=True, pad=3, light=(0.35, 0.75, 0.55), ss=4, max_px=1024,
               edges=True, edge_dark=0.55, edge_px=4.0):
        V = view_matrix(yaw, elev)
        Pv = self.bounds_px(F, yaw, elev, ppu, scale)
        x0, x1 = Pv[:, 0].min(), Pv[:, 0].max(); y0, y1 = Pv[:, 1].min(), Pv[:, 1].max()
        z0, z1 = Pv[:, 2].min(), Pv[:, 2].max()
        ox, oy = math.floor(x0) - pad, math.floor(y0) - pad
        W = min(int(math.ceil(x1)) + pad - ox, max_px); H = min(int(math.ceil(y1)) + pad - oy, max_px)
        Ws, Hs = W * ss, H * ss
        ctex = self.ctx.texture((Ws, Hs), 4)
        depth = self.ctx.depth_texture((Ws, Hs))
        fbo = self.ctx.framebuffer(color_attachments=[ctex], depth_attachment=depth)
        fbo.use(); fbo.clear(0, 0, 0, 0, depth=1.0)
        self.ctx.enable(moderngl.DEPTH_TEST)
        P = np.eye(4)
        P[0, 0] = 2.0 / W; P[0, 3] = -1.0 - 2.0 * ox / W
        P[1, 1] = 2.0 / H; P[1, 3] = -1.0 - 2.0 * oy / H
        zr = max(z1 - z0, 1.0) + 4.0
        P[2, 2] = -2.0 / zr; P[2, 3] = (z1 + z0) / zr
        Mv = np.eye(4); Mv[:3, :3] = V * scale * ppu
        MVP = (P @ Mv).astype("f4")
        L = np.array(light, float); L /= np.linalg.norm(L)
        # gas (fuego sin textura, p. ej. Gastly) primero y sin profundidad: queda detrás del cuerpo como un aura;
        # el resto en orden: opacos, luego los que mezclan
        def rank(i):
            m = F.meshes[i]
            if m.get("gas"): return (-1, 0)
            return (1 if m["mat"].blend_enabled else 0, m["priority"])
        order = sorted(range(len(F.meshes)), key=rank)
        texcache, lutcache = {}, {}

        def gettex(name):
            if not name or name not in F.tex: return None
            if name not in texcache: texcache[name] = self._tex(F.tex[name].decode())
            return texcache[name]

        def getlut(name):
            if name not in lutcache: lutcache[name] = self._lut(F.luts[name]) if name in F.luts else None
            return lutcache[name]
        for i in order:
            m = F.meshes[i]
            if not m["visible"]: continue
            mat = m["mat"]
            if "fire" in m:
                if not hasattr(self, "_fire"): self._fire = self.ctx.program(vertex_shader=VS, fragment_shader=FS_FIRE)
                prog = self._fire
                prog["uEdge"].value = m["fire"][0]; prog["uCore"].value = m["fire"][1]
            else:
                prog = self._prog(mat)
            prog["uMVP"].write(MVP.T.copy().tobytes())
            if "uNrm" in prog: prog["uNrm"].write(V.astype("f4").T.copy().tobytes())
            self._set(prog, "uL", tuple(float(x) for x in L))
            self._set(prog, "uBump", 1 if bump else 0)
            ae, af, ar = mat.alpha_test
            self._set(prog, "uAlphaFunc", ALPHA_FUNC.get(af, 0) if ae else 0)
            self._set(prog, "uAlphaRef", ar / 255.0)
            self._set(prog, "uOpaque", 0 if mat.blend_enabled else 1)
            # membranas de Solosis/Duosion/Reuniclus y el cristal de Lampent/Chandelure ("..._Cell")
            self._set(prog, "uMembrane", 1 if (mat.blend_enabled and "_Cell" in mat.name) else
                      2 if (mat.blend_enabled and "Fire" in mat.name) else 0)
            units = [self.white, self.gray, self.flat]
            for u in range(3):
                t = gettex(mat.texture_names[u]) if mat.units_enabled[u] else None
                if t is not None: units[u] = t
                if "t%d" % u in prog:
                    units[u].use(location=u); prog["t%d" % u].value = u
                tc = mat.tex_coords[u]
                xf = m["xf"][u] if "xf" in m else (tc.scale[0], tc.scale[1], tc.translation[0], tc.translation[1])
                self._set(prog, "xf%d" % u, tuple(float(v) for v in xf))
                up = mat.unit_params[u]
                self._set(prog, "wr%d" % u, (WRAP_MODE.get(up["wrap_s"], 1), WRAP_MODE.get(up["wrap_t"], 1)))
            self._set(prog, "uMap", tuple(int(mat.tex_coords[u].mapping) for u in range(3)))
            names = (getattr(mat, "lut_rgb", None) or [None] * 3)
            for k, nm in enumerate(("lutR", "lutG", "lutB")):
                if nm in prog:
                    t = getlut(names[k]) if names[k] else None
                    t = t or (self.one_lut if k == 0 else self.zero_lut)
                    t.use(location=3 + k); prog[nm].value = 3 + k
            if "Pp" in m:
                P_, N_, T_, UV, C, IDX = m["Pp"], m["Np"], m["Tp"], m["UVp"], m["Cp"], m["idxp"]
            else:
                P_, N_, T_, UV, IDX = m["P"], m["N"], m["T"], m["UV"], m["idx"]
                C = np.ones((len(P_), 4), "f4")
            vb = np.concatenate([P_, N_, T_, UV, C], 1).astype("f4")
            vbo = self.ctx.buffer(vb.tobytes())
            ibo = self.ctx.buffer(IDX.astype("i4").tobytes())
            attrs = [("aPos", 3), ("aNrm", 3), ("aTan", 3), ("aUV", 2), ("aCol", 4)]
            fmt = " ".join(("%df" % n) if a in prog else ("%dx4" % n) for a, n in attrs)
            vao = self.ctx.vertex_array(prog, [(vbo, fmt, *[a for a, n in attrs if a in prog])], ibo)
            cull = mat.cull
            if cull.startswith("BackFace"):
                self.ctx.enable(moderngl.CULL_FACE); self.ctx.cull_face = "back"
            elif cull.startswith("FrontFace"):
                self.ctx.enable(moderngl.CULL_FACE); self.ctx.cull_face = "front"
            else:
                self.ctx.disable(moderngl.CULL_FACE)
            self.ctx.front_face = "ccw"
            if "fire" in m:
                self.ctx.disable(moderngl.BLEND)
                if m.get("gas"):
                    self.ctx.disable(moderngl.DEPTH_TEST); self.ctx.depth_mask = False
                else:
                    self.ctx.enable(moderngl.DEPTH_TEST); self.ctx.depth_mask = True
                self.ctx.disable(moderngl.CULL_FACE)
            elif mat.blend_enabled:
                self.ctx.enable(moderngl.DEPTH_TEST)
                self.ctx.enable(moderngl.BLEND)
                eq = mat.blend[0]
                self.ctx.blend_equation = {1: moderngl.FUNC_SUBTRACT, 2: moderngl.FUNC_REVERSE_SUBTRACT, 3: moderngl.MIN,
                                           4: moderngl.MAX}.get(eq, moderngl.FUNC_ADD)
                # brillos aditivos / multiplicativos: solo cambian el color de lo que hay debajo, no crean silueta
                keep_a = mat.blend[3] == "One" or mat.blend[2] in ("DstColor", "Zero")
                self.ctx.blend_func = (BLEND_GL.get(mat.blend[2], moderngl.SRC_ALPHA), BLEND_GL.get(mat.blend[3], moderngl.ONE_MINUS_SRC_ALPHA),
                                       moderngl.ZERO if keep_a else moderngl.ONE, moderngl.ONE if keep_a else moderngl.ONE_MINUS_SRC_ALPHA)
                self.ctx.depth_mask = bool(mat.depth.get("write", False))
            else:
                self.ctx.enable(moderngl.DEPTH_TEST)
                self.ctx.disable(moderngl.BLEND)
                self.ctx.depth_mask = True
            vao.render(moderngl.TRIANGLES)
            vao.release(); vbo.release(); ibo.release()
        self.ctx.depth_mask = True
        data = np.frombuffer(ctex.read(), np.uint8).reshape(Hs, Ws, 4).astype(np.float32) / 255.0
        zbuf = np.frombuffer(depth.read(alignment=1), np.float32).reshape(Hs, Ws) if edges else None
        ctex.release(); depth.release(); fbo.release()
        if edges:
            # líneas interiores (como el contorno de los modelos del juego): donde la profundidad salta más de
            # edge_px píxeles finales entre dos muestras vecinas, se oscurece el lado de atrás (borde del de delante)
            zpx = zbuf * zr                                   # profundidad en píxeles finales
            cov = data[..., 3] > 0.5
            e = np.zeros((Hs, Ws), bool)
            dzx = zpx[:, 1:] - zpx[:, :-1]; both = cov[:, 1:] & cov[:, :-1]
            e[:, 1:] |= both & (dzx > edge_px); e[:, :-1] |= both & (dzx < -edge_px)
            dzy = zpx[1:, :] - zpx[:-1, :]; both = cov[1:, :] & cov[:-1, :]
            e[1:, :] |= both & (dzy > edge_px); e[:-1, :] |= both & (dzy < -edge_px)
            e2 = e.copy(); e2[1:, :] |= e[:-1, :]; e2[:, 1:] |= e[:, :-1]      # 2 muestras de grosor
            data[e2, :3] *= edge_dark
        for t in list(texcache.values()) + [x for x in lutcache.values() if x is not None]:
            t.release()
        data = data[::-1]                      # GL: fila 0 abajo
        pm = data.copy(); pm[..., :3] *= pm[..., 3:4]
        pm = pm.reshape(H, ss, W, ss, 4).mean((1, 3))
        a = pm[..., 3:4]
        rgb = np.where(a > 1e-4, pm[..., :3] / np.maximum(a, 1e-4), 0.0)
        img = np.concatenate([rgb, a], 2)
        return img, (float(-ox), float(H + oy))
