/*
 * HeroAssets.cpp — ORAS MMO v13
 * Cargador de los modelos y animaciones REALES del protagonista, leídos del RomFS del juego.
 * Port fiel de las referencias verificadas en Python (como tools/pokepack/bch.py).
 * Todas las lecturas están acotadas: datos corruptos -> fallo limpio, nunca un crash.
 * Código portable (sin CTRPF) para poder probarlo en PC.
 */
#include "HeroAssets.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cmath>

namespace Hero {

static Model g_models[MODEL_COUNT];
static bool  g_bikeErr = false;

// ============================================================================================
// Utilidades
// ============================================================================================
static void SetErr(char *err, size_t cap, const char *msg) {
    if (err && cap) { strncpy(err, msg, cap - 1); err[cap - 1] = 0; }
}

static void M34Inverse(Mat34 &o, const Mat34 &a) {
    const float *m = a.m;
    float c00 = m[5] * m[10] - m[6] * m[9], c01 = m[6] * m[8] - m[4] * m[10], c02 = m[4] * m[9] - m[5] * m[8];
    float det = m[0] * c00 + m[1] * c01 + m[2] * c02;
    if (fabsf(det) < 1e-12f) { M34Identity(o); return; }
    float id = 1.0f / det;
    float r[9] = {
        c00 * id, (m[2] * m[9] - m[1] * m[10]) * id, (m[1] * m[6] - m[2] * m[5]) * id,
        c01 * id, (m[0] * m[10] - m[2] * m[8]) * id, (m[2] * m[4] - m[0] * m[6]) * id,
        c02 * id, (m[1] * m[8] - m[0] * m[9]) * id, (m[0] * m[5] - m[1] * m[4]) * id
    };
    for (int i = 0; i < 3; i++) {
        o.m[i * 4 + 0] = r[i * 3 + 0];
        o.m[i * 4 + 1] = r[i * 3 + 1];
        o.m[i * 4 + 2] = r[i * 3 + 2];
        o.m[i * 4 + 3] = -(r[i * 3 + 0] * m[3] + r[i * 3 + 1] * m[7] + r[i * 3 + 2] * m[11]);
    }
}

// ============================================================================================
// Lector acotado sobre un búfer
// ============================================================================================
struct Reader {
    uint8_t *d; uint32_t n; bool bad;
    uint32_t U32(uint32_t o) { if (o + 4 > n || o + 4 < o) { bad = true; return 0; } uint32_t v; memcpy(&v, d + o, 4); return v; }
    uint16_t U16(uint32_t o) { if (o + 2 > n || o + 2 < o) { bad = true; return 0; } uint16_t v; memcpy(&v, d + o, 2); return v; }
    int16_t  S16(uint32_t o) { return (int16_t)U16(o); }
    uint8_t  U8(uint32_t o)  { if (o >= n) { bad = true; return 0; } return d[o]; }
    float    F32(uint32_t o) { uint32_t v = U32(o); float f; memcpy(&f, &v, 4); return f; }
    void     Put32(uint32_t o, uint32_t v) { if (o + 4 > n || o + 4 < o) { bad = true; return; } memcpy(d + o, &v, 4); }
    const char *Str(uint32_t o) {
        if (o == 0 || o >= n) return "";
        for (uint32_t i = o; i < n && i < o + 64; i++) if (!d[i]) return (const char *)d + o;
        return "";
    }
};

// ============================================================================================
// GARC + LZ11 (con límite de salida)
// ============================================================================================
static uint32_t Lz11(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t cap) {
    if (srcLen < 4 || src[0] != 0x11) return 0;
    uint32_t size = src[1] | (src[2] << 8) | (src[3] << 16), pos = 4;
    if (size == 0) { if (srcLen < 8) return 0; memcpy(&size, src + 4, 4); pos = 8; }
    uint32_t limit = size < cap ? size : cap, out = 0;
    while (out < limit && pos < srcLen) {
        uint8_t flags = src[pos++];
        for (int i = 0; i < 8 && out < limit; i++) {
            if (flags & (0x80 >> i)) {
                if (pos + 1 >= srcLen) return out;
                uint8_t b = src[pos];
                uint32_t len, disp, ind = b >> 4;
                if (ind == 0) {
                    if (pos + 2 >= srcLen) return out;
                    len = (((b & 0xF) << 4) | (src[pos + 1] >> 4)) + 0x11;
                    disp = (((src[pos + 1] & 0xF) << 8) | src[pos + 2]) + 1; pos += 3;
                } else if (ind == 1) {
                    if (pos + 3 >= srcLen) return out;
                    len = (((b & 0xF) << 12) | (src[pos + 1] << 4) | (src[pos + 2] >> 4)) + 0x111;
                    disp = (((src[pos + 2] & 0xF) << 8) | src[pos + 3]) + 1; pos += 4;
                } else {
                    len = ind + 1;
                    disp = (((b & 0xF) << 8) | src[pos + 1]) + 1; pos += 2;
                }
                if (disp > out) return out;
                for (uint32_t k = 0; k < len && out < limit; k++, out++) dst[out] = dst[out - disp];
            } else {
                if (pos >= srcLen) return out;
                dst[out++] = src[pos++];
            }
        }
    }
    return out;
}

// v16.2: LZ11 para otros módulos (los acompañantes 3D vienen comprimidos así)
uint32_t Lz11Decode(const uint8_t *src, uint32_t srcLen, uint8_t *dst, uint32_t cap) { return Lz11(src, srcLen, dst, cap); }

// v17: dónde está (sin leerlo) el subarchivo 0 de la entrada 'index' del GARC
bool GarcRange(ByteSource &src, uint64_t g, uint64_t gsize, uint32_t index, uint64_t &absOff, uint32_t &len) {
    uint8_t head[0x1C];
    if (gsize < 0x40 || !src.Read(g, head, sizeof(head)) || memcmp(head, "CRAG", 4)) return false;
    uint32_t hsize, dataOff;
    memcpy(&hsize, head + 4, 4);
    memcpy(&dataOff, head + 0x10, 4);
    uint8_t fato[12];
    if (!src.Read(g + hsize, fato, 12) || memcmp(fato, "OTAF", 4)) return false;
    uint32_t fatoSize; uint16_t count;
    memcpy(&fatoSize, fato + 4, 4);
    memcpy(&count, fato + 8, 2);
    if (index >= count) return false;
    uint32_t entOff;
    if (!src.Read(g + hsize + 12 + 4 * (uint64_t)index, &entOff, 4)) return false;
    uint64_t fatb = g + hsize + fatoSize;
    uint8_t fb[4];
    if (!src.Read(fatb, fb, 4) || memcmp(fb, "BTAF", 4)) return false;
    uint32_t rec[4];
    if (!src.Read(fatb + 12 + entOff, rec, sizeof(rec)) || !(rec[0] & 1)) return false;
    if (rec[2] <= rec[1] || rec[2] - rec[1] > 0x400000) return false;
    absOff = g + dataOff + rec[1];
    len = rec[2] - rec[1];
    return true;
}

// Lee el subarchivo 0 de la entrada 'index' del GARC. Devuelve malloc() descomprimido (hasta maxOut bytes).
uint8_t *GarcEntry(ByteSource &src, uint64_t g, uint64_t gsize, uint32_t index, uint32_t maxOut, uint32_t &outSize) {
    outSize = 0;
    uint64_t at; uint32_t len;
    if (!GarcRange(src, g, gsize, index, at, len)) return nullptr;
    uint8_t *raw = (uint8_t *)malloc(len);
    if (!raw) return nullptr;
    if (!src.Read(at, raw, len)) { free(raw); return nullptr; }
    if (raw[0] != 0x11) { outSize = len; return raw; }
    uint32_t full = raw[1] | (raw[2] << 8) | (raw[3] << 16);
    uint32_t cap = (maxOut && maxOut < full) ? maxOut : full;
    uint8_t *out = (cap && cap <= 0x200000) ? (uint8_t *)malloc(cap) : nullptr;
    if (!out) { free(raw); return nullptr; }
    outSize = Lz11(raw, len, out, cap);
    free(raw);
    if (outSize < cap) { free(out); outSize = 0; return nullptr; }
    return out;
}

// ============================================================================================
// PICA200: lectura de listas de comandos
// ============================================================================================
static float F24(uint32_t v) {
    v &= 0xFFFFFF;
    if ((v & 0x7FFFFF) == 0) return 0.0f;
    uint32_t s = v >> 23, e = (v >> 16) & 0x7F, m = v & 0xFFFF;
    float f = (1.0f + m / 65536.0f) * ldexpf(1.0f, (int)e - 63);
    return s ? -f : f;
}

struct PicaState {
    uint32_t reg[0x300];
    uint8_t  set[0x300];
    float    uni[16][4];  bool uniSet[16];
    float    fixed[12][4]; bool fixedSet[12];
};

static void ParseCmds(Reader &r, uint32_t off, uint32_t words, PicaState &st) {
    memset(&st, 0, sizeof(st));
    int uidx = 0; bool f32mode = false; uint32_t ubuf[4]; int un = 0;
    int fidx = 0; uint32_t fbuf[3]; int fn = 0;
    uint32_t i = 0;
    while (i + 1 < words && !r.bad) {
        uint32_t param = r.U32(off + 4 * i), hdr = r.U32(off + 4 * i + 4);
        uint32_t reg = hdr & 0xFFFF, extra = (hdr >> 20) & 0x7FF; bool consec = (hdr >> 31) != 0;
        i += 2;
        for (uint32_t k = 0; k <= extra; k++) {
            uint32_t v = (k == 0) ? param : r.U32(off + 4 * (i + k - 1));
            uint32_t rg = consec ? reg + k : reg;
            if (rg < 0x300) { st.reg[rg] = v; st.set[rg] = 1; }
            if (rg == 0x2C0) { uidx = v & 0xFF; f32mode = (v >> 31) != 0; un = 0; }
            else if (rg >= 0x2C1 && rg <= 0x2C8) {
                ubuf[un++] = v;
                int need = f32mode ? 4 : 3;
                if (un == need) {
                    float x, y, z, w;
                    if (f32mode) { memcpy(&w, &ubuf[0], 4); memcpy(&z, &ubuf[1], 4); memcpy(&y, &ubuf[2], 4); memcpy(&x, &ubuf[3], 4); }
                    else {
                        w = F24(ubuf[0] >> 8); z = F24(((ubuf[0] & 0xFF) << 16) | (ubuf[1] >> 16));
                        y = F24(((ubuf[1] & 0xFFFF) << 8) | (ubuf[2] >> 24)); x = F24(ubuf[2] & 0xFFFFFF);
                    }
                    if (uidx >= 0 && uidx < 16) { st.uni[uidx][0] = x; st.uni[uidx][1] = y; st.uni[uidx][2] = z; st.uni[uidx][3] = w; st.uniSet[uidx] = true; }
                    uidx++; un = 0;
                }
            } else if (rg == 0x232) { fidx = v & 0xF; fn = 0; }
            else if (rg >= 0x233 && rg <= 0x235) {
                fbuf[fn++] = v;
                if (fn == 3) {
                    if (fidx < 12) {
                        st.fixed[fidx][3] = F24(fbuf[0] >> 8);
                        st.fixed[fidx][2] = F24(((fbuf[0] & 0xFF) << 16) | (fbuf[1] >> 16));
                        st.fixed[fidx][1] = F24(((fbuf[1] & 0xFFFF) << 8) | (fbuf[2] >> 24));
                        st.fixed[fidx][0] = F24(fbuf[2] & 0xFFFFFF);
                        st.fixedSet[fidx] = true;
                    }
                    fidx++; fn = 0;
                }
            }
        }
        i += extra;
        if (i & 1) i++;
    }
}

// ============================================================================================
// Texturas
// ============================================================================================
static const int kMortonX[64] = {0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3,4,5,4,5,6,7,6,7,4,5,4,5,6,7,6,7,0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3,4,5,4,5,6,7,6,7,4,5,4,5,6,7,6,7};
static const int kMortonY[64] = {0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3,0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3,4,4,5,5,4,4,5,5,6,6,7,7,6,6,7,7,4,4,5,5,4,4,5,5,6,6,7,7,6,6,7,7};

static inline uint16_t Pack5551(int r, int g, int b, int a) {
    return (uint16_t)(((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >= 128 ? 1 : 0));
}

static const int kEtcMod[8][4] = { {2,8,-2,-8},{5,17,-5,-17},{9,29,-9,-29},{13,42,-13,-42},
                                   {18,60,-18,-60},{24,80,-24,-80},{33,106,-33,-106},{47,183,-47,-183} };
static inline int Clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void Etc1Block(uint64_t v, uint64_t alpha, bool hasAlpha, uint16_t *out, int W, int H, int x0, int y0) {
    uint32_t hi = (uint32_t)(v >> 32), lo = (uint32_t)v;
    int diff = (hi >> 1) & 1, flip = hi & 1, t1 = (hi >> 5) & 7, t2 = (hi >> 2) & 7;
    int c1[3], c2[3];
    if (diff) {
        int r1 = (hi >> 27) & 31, g1 = (hi >> 19) & 31, b1 = (hi >> 11) & 31;
        int dr = (hi >> 24) & 7, dg = (hi >> 16) & 7, db = (hi >> 8) & 7;
        if (dr > 3) dr -= 8;
        if (dg > 3) dg -= 8;
        if (db > 3) db -= 8;
        int r2 = (r1 + dr) & 31, g2 = (g1 + dg) & 31, b2 = (b1 + db) & 31;
        c1[0] = (r1 << 3) | (r1 >> 2); c1[1] = (g1 << 3) | (g1 >> 2); c1[2] = (b1 << 3) | (b1 >> 2);
        c2[0] = (r2 << 3) | (r2 >> 2); c2[1] = (g2 << 3) | (g2 >> 2); c2[2] = (b2 << 3) | (b2 >> 2);
    } else {
        c1[0] = ((hi >> 28) & 15) * 17; c1[1] = ((hi >> 20) & 15) * 17; c1[2] = ((hi >> 12) & 15) * 17;
        c2[0] = ((hi >> 24) & 15) * 17; c2[1] = ((hi >> 16) & 15) * 17; c2[2] = ((hi >> 8) & 15) * 17;
    }
    for (int x = 0; x < 4; x++)
        for (int y = 0; y < 4; y++) {
            int k = x * 4 + y;
            int idx = ((lo >> k) & 1) | (((lo >> (k + 16)) & 1) << 1);
            bool second = flip ? (y >= 2) : (x >= 2);
            const int *base = second ? c2 : c1;
            int m = kEtcMod[second ? t2 : t1][idx];
            int a = hasAlpha ? (int)(((alpha >> ((x * 4 + y) * 4)) & 15) * 17) : 255;
            if (x0 + x >= W || y0 + y >= H) continue;
            out[(y0 + y) * W + (x0 + x)] = Pack5551(Clamp255(base[0] + m), Clamp255(base[1] + m), Clamp255(base[2] + m), a);
        }
}

// Decodifica a RGBA5551 lineal (fila 0 = arriba). Devuelve false si el formato no se soporta.
static bool DecodeTexture(Reader &r, uint32_t off, int W, int H, int fmt, uint16_t *out, bool &hasAlpha) {
    hasAlpha = false;
    if (fmt == 12 || fmt == 13) {
        bool a4 = fmt == 13;
        uint32_t pos = off;
        for (int ty = 0; ty < H; ty += 8)
            for (int tx = 0; tx < W; tx += 8)
                for (int b = 0; b < 4; b++) {
                    int bx = (b & 1) ? 4 : 0, by = (b & 2) ? 4 : 0;
                    uint64_t alpha = 0, v;
                    if (a4) { alpha = (uint64_t)r.U32(pos) | ((uint64_t)r.U32(pos + 4) << 32); pos += 8; }
                    v = (uint64_t)r.U32(pos) | ((uint64_t)r.U32(pos + 4) << 32); pos += 8;
                    if (r.bad) return false;
                    Etc1Block(v, alpha, a4, out, W, H, tx + bx, ty + by);
                }
        if (a4) for (int i = 0; i < W * H; i++) if (!(out[i] & 1)) { hasAlpha = true; break; }
        return true;
    }
    int bpp = (fmt == 0) ? 4 : (fmt == 1) ? 3 : (fmt >= 2 && fmt <= 6) ? 2 : (fmt == 7 || fmt == 8 || fmt == 9) ? 1 : 0;
    if (!bpp) return false;
    uint32_t needBytes = (uint32_t)(W * H * bpp);
    if (off > r.n || needBytes > r.n - off) { r.bad = true; return false; }
    const uint8_t *s = r.d + off;
    int tilesX = W / 8;
    for (int n = 0; n < W * H; n++) {
        int tile = n / 64, i = n % 64;
        int x = (tile % tilesX) * 8 + kMortonX[i], y = (tile / tilesX) * 8 + kMortonY[i];
        const uint8_t *p = s + n * bpp;
        int R = 255, G = 255, B = 255, A = 255;
        uint16_t v = (uint16_t)(p[0] | (bpp > 1 ? p[1] << 8 : 0));
        switch (fmt) {
            case 0: A = p[0]; B = p[1]; G = p[2]; R = p[3]; break;
            case 1: B = p[0]; G = p[1]; R = p[2]; break;
            case 2: R = ((v >> 11) & 31) << 3; G = ((v >> 6) & 31) << 3; B = ((v >> 1) & 31) << 3; A = (v & 1) ? 255 : 0; break;
            case 3: R = ((v >> 11) & 31) << 3; G = ((v >> 5) & 63) << 2; B = (v & 31) << 3; break;
            case 4: R = ((v >> 12) & 15) * 17; G = ((v >> 8) & 15) * 17; B = ((v >> 4) & 15) * 17; A = (v & 15) * 17; break;
            case 5: A = p[0]; R = G = B = p[1]; break;
            case 6: R = p[1]; G = p[0]; B = 0; break;
            case 7: R = G = B = p[0]; break;
            case 8: A = p[0]; break;
            case 9: R = G = B = (p[0] >> 4) * 17; A = (p[0] & 15) * 17; break;
        }
        if (A < 128) hasAlpha = true;
        out[y * W + x] = Pack5551(R, G, B, A);
    }
    return true;
}

// ============================================================================================
// BCH: modelo
// ============================================================================================
struct IdxType { uint32_t addr; bool u16; };

struct BchCtx {
    Reader r;
    uint32_t contentsOff, stringsOff, commandsOff, rawOff, rawExtOff;
    IdxType idx[96]; int idxCount;
};

static bool Relocate(BchCtx &c) {
    Reader &r = c.r;
    if (r.n < 0x44 || memcmp(r.d, "BCH", 3) || r.d[4] < 0x21) return false;
    c.contentsOff = r.U32(0x08); c.stringsOff = r.U32(0x0C); c.commandsOff = r.U32(0x10);
    c.rawOff = r.U32(0x14); c.rawExtOff = r.U32(0x18);
    uint32_t relocOff = r.U32(0x1C), relocLen = r.U32(0x34);
    c.idxCount = 0;
    for (uint32_t o = 0; o + 4 <= relocLen && !r.bad; o += 4) {
        uint32_t v = r.U32(relocOff + o), off = v & 0x1FFFFFF, fl = v >> 25;
        uint32_t a, add;
        if (fl == 0) { a = c.contentsOff + off * 4; add = c.contentsOff; }
        else if (fl == 1) { a = c.contentsOff + off; add = c.stringsOff; }
        else if (fl == 2 || fl == 3) { a = c.contentsOff + off * 4; add = c.commandsOff; }
        else if (fl >= 0x25 && fl <= 0x28) { a = c.commandsOff + off * 4; add = c.rawOff; }
        else if (fl == 0x2E) { a = c.commandsOff + off * 4; add = c.rawOff; }
        else if (fl >= 0x29 && fl <= 0x2F) { a = c.commandsOff + off * 4; add = c.rawExtOff; }
        else return false;
        uint32_t old = r.U32(a);
        if (r.bad) return false;
        r.Put32(a, old + add);
        if ((fl == 0x27 || fl == 0x28) && c.idxCount < 96) {
            c.idx[c.idxCount].addr = old + add;
            c.idx[c.idxCount].u16 = (fl == 0x27);
            c.idxCount++;
        }
    }
    return !r.bad;
}

static int FindBone(const Model &m, const char *name) {
    for (int i = 0; i < m.boneCount; i++) if (!strcmp(m.boneName[i], name)) return i;
    return -1;
}

// Bind de cada hueso (para animación)
struct BindBone { float s[3], r[3], t[3]; };

static bool ParseModel(BchCtx &c, Model &M, BindBone *bind, char *err, size_t cap) {
    Reader &r = c.r;
    uint32_t modelsPtr = r.U32(c.contentsOff + 0), modelsCount = r.U32(c.contentsOff + 4);
    uint32_t texPtr = r.U32(c.contentsOff + 36), texCount = r.U32(c.contentsOff + 40);
    if (r.bad || modelsCount < 1) { SetErr(err, cap, "BCH sin modelo"); return false; }
    uint32_t mo = r.U32(modelsPtr);

    // --- esqueleto
    uint32_t sp = r.U32(mo + 0x70), sn = r.U32(mo + 0x74);
    if (sn == 0 || sn > (uint32_t)MAX_BONES) { SetErr(err, cap, "esqueleto invalido"); return false; }
    M.boneCount = (int)sn;
    Mat34 world[MAX_BONES];
    for (uint32_t i = 0; i < sn; i++) {
        uint32_t o = sp + 0x64 * i;
        M.parent[i] = r.S16(o + 4);
        if (M.parent[i] >= (int)i) M.parent[i] = -1;
        for (int k = 0; k < 3; k++) {
            bind[i].s[k] = r.F32(o + 0x08 + 4 * k);
            bind[i].r[k] = r.F32(o + 0x14 + 4 * k);
            bind[i].t[k] = r.F32(o + 0x20 + 4 * k);
        }
        for (int k = 0; k < 12; k++) M.invBind[i].m[k] = r.F32(o + 0x2C + 4 * k);
        snprintf(M.boneName[i], sizeof(M.boneName[i]), "%s", r.Str(r.U32(o + 0x5C)));
        Mat34 local;
        M34FromSRT(local, bind[i].s, bind[i].r, bind[i].t);
        if (M.parent[i] < 0) world[i] = local; else M34Mul(world[i], world[M.parent[i]], local);
    }
    if (r.bad) { SetErr(err, cap, "esqueleto corrupto"); return false; }

    // --- texturas (todas las del BCH)
    struct TexInfo { const char *name; int w, h, fmt; uint32_t off; };
    TexInfo tinfo[16]; int tn = 0;
    for (uint32_t i = 0; i < texCount && tn < 16; i++) {
        uint32_t to = r.U32(texPtr + 4 * i);
        PicaState *st = (PicaState *)malloc(sizeof(PicaState));
        if (!st) return false;
        ParseCmds(r, r.U32(to), r.U32(to + 4), *st);
        tinfo[tn].name = r.Str(r.U32(to + 0x1C));
        tinfo[tn].fmt = r.U8(to + 0x18);
        tinfo[tn].w = (st->reg[0x82] >> 16) & 0x7FF;
        tinfo[tn].h = st->reg[0x82] & 0x7FF;
        tinfo[tn].off = st->reg[0x85];
        free(st);
        tn++;
    }

    // --- nombres de nodos de malla
    uint32_t nodeCount = r.U32(mo + 0x88), nodeTree = r.U32(mo + 0x8C);
    auto nodeName = [&](uint32_t idx) -> const char * {
        if (!nodeTree || idx >= nodeCount) return "";
        return r.Str(r.U32(nodeTree + 12 * (idx + 1) + 8));
    };

    // --- mallas
    uint32_t matPtr = r.U32(mo + 0x34), matCount = r.U32(mo + 0x38);
    uint32_t meshPtr = r.U32(mo + 0x40), meshCount = r.U32(mo + 0x44);
    if (meshCount == 0 || meshCount > 32) { SetErr(err, cap, "mallas invalidas"); return false; }

    // primera pasada: contar vértices/índices visibles
    struct MeshInfo { bool visible; uint32_t nv; };
    MeshInfo mi[32];
    uint32_t totalV = 0, totalI = 0;
    PicaState *st = (PicaState *)malloc(sizeof(PicaState));
    if (!st) return false;
    for (uint32_t m = 0; m < meshCount; m++) {
        uint32_t o = meshPtr + 0x38 * m;
        const char *nn = nodeName(r.U16(o + 4));
        mi[m].visible = !(strcmp(nn, "sw_parts01") == 0 || strcmp(nn, "sw_parts03") == 0 ||
                          strcmp(nn, "sw_parts04") == 0 || strcmp(nn, "sw_parts05") == 0);
        mi[m].nv = 0;
        uint32_t smp = r.U32(o + 0x10), smn = r.U32(o + 0x14);
        for (uint32_t s = 0; s < smn && s < 32; s++) {
            uint32_t so = smp + 0x34 * s;
            ParseCmds(r, r.U32(so + 0x2C), r.U32(so + 0x30), *st);
            uint32_t ia = st->reg[0x227] & 0x0FFFFFFF, ic = st->reg[0x228];
            bool u16 = true;
            for (int k = 0; k < c.idxCount; k++) if (c.idx[k].addr == ia) u16 = c.idx[k].u16;
            if (ic > 60000) { r.bad = true; break; }
            for (uint32_t k = 0; k < ic && !r.bad; k++) {
                uint32_t v = u16 ? r.U16(ia + 2 * k) : r.U8(ia + k);
                if (v + 1 > mi[m].nv) mi[m].nv = v + 1;
            }
            if (mi[m].visible) totalI += ic;
        }
        if (mi[m].visible) totalV += mi[m].nv;
    }
    if (r.bad || totalV == 0 || totalV > 20000 || totalI > 60000) { free(st); SetErr(err, cap, "mallas corruptas"); return false; }

    M.verts = (Vertex *)calloc(totalV, sizeof(Vertex));
    M.indices = (uint16_t *)malloc(totalI * 2);
    if (!M.verts || !M.indices) { free(st); SetErr(err, cap, "sin memoria (mallas)"); return false; }
    M.vertCount = 0; M.indexCount = 0; M.rangeCount = 0; M.texCount = 0;
    int texMap[16]; for (int i = 0; i < 16; i++) texMap[i] = -1;

    for (uint32_t m = 0; m < meshCount && !r.bad; m++) {
        if (!mi[m].visible || mi[m].nv == 0) continue;
        uint32_t o = meshPtr + 0x38 * m;
        uint32_t base = (uint32_t)M.vertCount;
        uint16_t matIndex = r.U16(o + 0);

        // material -> textura de la unidad 1 y transformación de UV
        float uvScale[2] = { 1, 1 }, uvTrans[2] = { 0, 0 };
        int texIdx = -1;
        if (matIndex < matCount) {
            uint32_t mt = matPtr + 0x2C * matIndex;
            const char *tname = r.Str(r.U32(mt + 0x20));
            uint32_t params = r.U32(mt);
            uint32_t tc = params + 0x0C + 0x18;   // coordenada de textura 1
            uvScale[0] = r.F32(tc + 4); uvScale[1] = r.F32(tc + 8);
            uvTrans[0] = r.F32(tc + 0x10); uvTrans[1] = r.F32(tc + 0x14);
            if (!(fabsf(uvScale[0]) > 1e-6f)) uvScale[0] = 1;
            if (!(fabsf(uvScale[1]) > 1e-6f)) uvScale[1] = 1;
            for (int t = 0; t < tn; t++) if (!strcmp(tinfo[t].name, tname)) texIdx = t;
        }
        if (texIdx < 0) continue;
        if (texMap[texIdx] < 0) {
            if (M.texCount >= MAX_TEX) continue;
            TexInfo &ti = tinfo[texIdx];
            if (ti.w < 8 || ti.h < 8 || ti.w > 512 || ti.h > 512 || (ti.w & 7) || (ti.h & 7)) continue;
            Texture &T = M.tex[M.texCount];
            T.w = (uint16_t)ti.w; T.h = (uint16_t)ti.h;
            T.px = (uint16_t *)malloc(ti.w * ti.h * 2);
            bool hasA = false;
            if (!T.px || !DecodeTexture(r, ti.off, ti.w, ti.h, ti.fmt, T.px, hasA)) { free(T.px); T.px = nullptr; continue; }
            // copia de los bytes originales (la skin perfecta busca y recolorea la textura del juego)
            {
                uint32_t bppx2 = (ti.fmt == 12) ? 1 : (ti.fmt == 13) ? 2 : (ti.fmt == 0) ? 8 : (ti.fmt == 1) ? 6 :
                                 (ti.fmt >= 2 && ti.fmt <= 6) ? 4 : (ti.fmt >= 7 && ti.fmt <= 9) ? 2 : 0;
                uint32_t sz = (uint32_t)(ti.w * ti.h) * bppx2 / 2;
                T.raw = nullptr; T.rawSize = 0; T.fmt = (uint8_t)ti.fmt;
                if (sz && ti.off <= r.n && sz <= r.n - ti.off) {
                    T.raw = (uint8_t *)malloc(sz);
                    if (T.raw) { memcpy(T.raw, r.d + ti.off, sz); T.rawSize = sz; }
                }
            }
            texMap[texIdx] = M.texCount++;
            (void)hasA;
        }

        // formato de vértice
        ParseCmds(r, r.U32(o + 8), r.U32(o + 12), *st);
        uint64_t fmt64 = (uint64_t)st->reg[0x201] | ((uint64_t)(st->reg[0x202] & 0xFFFF) << 32);
        uint32_t fixedMask = (st->reg[0x202] >> 16) & 0xFFF;
        int nattr = (int)(st->reg[0x202] >> 28) + 1;
        if (nattr > 12) nattr = 12;
        uint64_t perm = (uint64_t)st->reg[0x2BB] | ((uint64_t)st->reg[0x2BC] << 32);
        float c7[4] = { 1, 1, 1, 1 }, c8[4] = { 1, 1, 1, 1 };
        if (st->uniSet[7]) memcpy(c7, st->uni[7], sizeof(c7));
        if (st->uniSet[8]) memcpy(c8, st->uni[8], sizeof(c8));
        struct Attr { int sem, type, comps, offset; float scale; bool fixed; float fv[4]; } at[12];
        for (int a = 0; a < nattr && a < 12; a++) {
            at[a].sem = (int)((perm >> (4 * a)) & 0xF);
            int f = (int)((fmt64 >> (4 * a)) & 0xF);
            at[a].type = f & 3; at[a].comps = (f >> 2) + 1; at[a].offset = -1;
            at[a].fixed = (fixedMask >> a) & 1;
            if (at[a].fixed) memcpy(at[a].fv, st->fixed[a], sizeof(at[a].fv));
            switch (at[a].sem) {
                case 0: at[a].scale = c7[1]; break;      // posición
                case 1: at[a].scale = c7[2]; break;      // normal
                case 4: at[a].scale = c8[0]; break;      // uv0
                case 8: at[a].scale = c8[3]; break;      // peso
                default: at[a].scale = 1.0f; break;
            }
        }
        uint32_t vbOff = st->reg[0x203], lo = st->reg[0x204], hi = st->reg[0x205];
        uint32_t stride = (hi >> 16) & 0xFF, ncomp = hi >> 28;
        uint64_t comp = (uint64_t)lo | ((uint64_t)(hi & 0xFFFF) << 32);
        uint32_t pos = 0;
        static const int tsize[4] = { 1, 1, 2, 4 };
        for (uint32_t k = 0; k < ncomp; k++) {
            int cidx = (int)((comp >> (4 * k)) & 0xF);
            if (cidx >= 12) { pos += 4 * (cidx - 11); continue; }
            if (cidx >= nattr) continue;
            int sz = tsize[at[cidx].type];
            pos = (pos + sz - 1) / sz * sz;
            at[cidx].offset = (int)pos;
            pos += sz * at[cidx].comps;
        }
        auto readAttr = [&](int a, uint32_t v, float out[4]) {
            out[0] = out[1] = out[2] = 0; out[3] = 1;
            if (at[a].fixed) { memcpy(out, at[a].fv, 16); return; }
            if (at[a].offset < 0) return;
            uint32_t p = vbOff + v * stride + at[a].offset;
            for (int k = 0; k < at[a].comps && k < 4; k++) {
                float val;
                switch (at[a].type) {
                    case 0: val = (float)(int8_t)r.U8(p + k); break;
                    case 1: val = (float)r.U8(p + k); break;
                    case 2: val = (float)r.S16(p + 2 * k); break;
                    default: val = r.F32(p + 4 * k); break;
                }
                if (at[a].type != 3 || at[a].sem == 8) val *= at[a].scale;
                out[k] = val;
            }
        };

        // vértices de la malla
        float bi[4], bw[4];
        for (uint32_t v = 0; v < mi[m].nv; v++) {
            Vertex &V = M.verts[base + v];
            float tmp[4];
            for (int a = 0; a < nattr; a++) {
                readAttr(a, v, tmp);
                switch (at[a].sem) {
                    case 0: V.px = tmp[0]; V.py = tmp[1]; V.pz = tmp[2]; break;
                    case 1: V.nx = tmp[0]; V.ny = tmp[1]; V.nz = tmp[2]; break;
                    case 4: V.u = tmp[0] * uvScale[0] + uvTrans[0]; V.v = tmp[1] * uvScale[1] + uvTrans[1]; break;
                    default: break;
                }
            }
            float l = sqrtf(V.nx * V.nx + V.ny * V.ny + V.nz * V.nz);
            if (l > 1e-6f) { V.nx /= l; V.ny /= l; V.nz /= l; } else { V.ny = 1; }
        }
        // submallas: índices + traducción de la paleta de huesos
        uint32_t smp = r.U32(o + 0x10), smn = r.U32(o + 0x14);
        for (uint32_t s = 0; s < smn && s < 32 && !r.bad; s++) {
            uint32_t so = smp + 0x34 * s;
            uint16_t palN = r.U16(so + 2);
            if (palN > 20) palN = 20;
            uint16_t pal[20];
            for (int k = 0; k < 20; k++) pal[k] = (k < palN) ? r.U16(so + 4 + 2 * k) : 0;
            ParseCmds(r, r.U32(so + 0x2C), r.U32(so + 0x30), *st);
            uint32_t ia = st->reg[0x227] & 0x0FFFFFFF, ic = st->reg[0x228];
            bool u16 = true;
            for (int k = 0; k < c.idxCount; k++) if (c.idx[k].addr == ia) u16 = c.idx[k].u16;
            if (M.rangeCount >= MAX_RANGES) break;
            DrawRange &R = M.ranges[M.rangeCount++];
            R.firstIndex = (uint32_t)M.indexCount;
            R.indexCount = ic - ic % 3;
            R.texture = (uint8_t)texMap[texIdx];
            const Texture &TT = M.tex[R.texture];
            R.alphaTest = 0;
            for (int q = 0; q < TT.w * TT.h; q++) if (!(TT.px[q] & 1)) { R.alphaTest = 1; break; }
            R.isFace = strstr(tinfo[texIdx].name, "face") != nullptr;
            R.isHead = strstr(tinfo[texIdx].name, "_head") != nullptr;    // rstr000N_00_fi_head
            for (uint32_t k = 0; k < R.indexCount; k++) {
                uint32_t v = u16 ? r.U16(ia + 2 * k) : r.U8(ia + k);
                if (v >= mi[m].nv) v = 0;
                M.indices[M.indexCount++] = (uint16_t)(base + v);
                // huesos del vértice (se escriben una vez; todas las submallas coinciden según la referencia)
                Vertex &V = M.verts[base + v];
                if (V.weight[0] | V.weight[1] | V.weight[2] | V.weight[3]) continue;
                bi[0] = bi[1] = bi[2] = bi[3] = 0; bw[0] = 1; bw[1] = bw[2] = bw[3] = 0;
                for (int a = 0; a < nattr; a++) {
                    float tmp[4];
                    if (at[a].sem == 7) { readAttr(a, v, tmp); for (int q = 0; q < 4; q++) bi[q] = (q < at[a].comps || at[a].fixed) ? tmp[q] : 0; }
                    if (at[a].sem == 8) {
                        readAttr(a, v, tmp);
                        for (int q = 0; q < 4; q++) bw[q] = (q < at[a].comps || at[a].fixed) ? tmp[q] : 0;
                        if (at[a].fixed) { bw[1] = bw[2] = bw[3] = 0; }
                    }
                }
                int tot = 0, best = 0;
                for (int q = 0; q < 4; q++) {
                    int pi = (int)(bi[q] + 0.5f);
                    int sk = (pi >= 0 && pi < palN) ? pal[pi] : 0;
                    if (sk >= M.boneCount) sk = 0;
                    V.bone[q] = (uint8_t)sk;
                    float w = bw[q] < 0 ? 0 : bw[q];
                    int wi = (int)(w * 255.0f + 0.5f);
                    if (wi > 255) wi = 255;
                    V.weight[q] = (uint8_t)wi;
                    tot += wi;
                    if (V.weight[q] > V.weight[best]) best = q;
                }
                if (tot == 0) { V.weight[0] = 255; }
                else { int fix = (int)V.weight[best] + (255 - tot); V.weight[best] = (uint8_t)(fix < 0 ? 0 : fix > 255 ? 255 : fix); }
            }
        }
        M.vertCount += (int)mi[m].nv;
    }
    free(st);
    if (r.bad || M.rangeCount == 0) { SetErr(err, cap, "mallas corruptas (2)"); return false; }

    // altura del modelo (máximo Y en pose de enlace)
    M.height = 0;
    for (int i = 0; i < M.vertCount; i++) if (M.verts[i].py > M.height) M.height = M.verts[i].py;
    return true;
}

// ============================================================================================
// Animaciones GF1 (paquete de movimientos) + horneado con IK de pies
// ============================================================================================
enum CurveKind { CV_NONE = 0, CV_CONST, CV_LINEAR, CV_HERMITE };
struct Curve { uint8_t kind; uint16_t nkeys; float cval; const uint16_t *frames; float *vals; float *slopes; };

struct GfBone { char name[24]; uint8_t flags; };

struct MotionData {
    int frames;
    Curve curve[64][9];      // por hueso GF y elemento (tx ty tz rx ry rz sx sy sz)
    uint16_t *keyFrames;     // almacenamiento
    float *store;
};

static float EvalCurve(const Curve &c, float f) {
    if (c.kind == CV_CONST) return c.cval;
    int n = c.nkeys;
    if (n <= 0) return 0;
    if (f <= c.frames[0]) return c.vals[0];
    if (f >= c.frames[n - 1]) return c.vals[n - 1];
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) { int mid = (lo + hi) >> 1; if (c.frames[mid] <= f) lo = mid; else hi = mid; }
    float span = (float)(c.frames[hi] - c.frames[lo]);
    if (span <= 0) return c.vals[hi];
    float t = (f - c.frames[lo]) / span;
    if (c.kind == CV_LINEAR) return c.vals[lo] + (c.vals[hi] - c.vals[lo]) * t;
    float t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * c.vals[lo] + (t3 - 2 * t2 + t) * span * c.slopes[lo] +
           (-2 * t3 + 3 * t2) * c.vals[hi] + (t3 - t2) * span * c.slopes[hi];
}

// Analiza el movimiento en 'o' (relativo al subarchivo). Devuelve false si está corrupto.
static bool ParseMotion(Reader &r, uint32_t o, int gfBones, MotionData &md) {
    memset(md.curve, 0, sizeof(md.curve));
    md.keyFrames = nullptr; md.store = nullptr;
    uint32_t octCount = r.U16(o), frames = r.U16(o + 2);
    md.frames = (int)frames;
    if (octCount < 2 || octCount > 2000 || frames == 0 || frames > 2000) return false;
    uint8_t *oct = (uint8_t *)malloc(octCount);
    if (!oct) return false;
    uint32_t p = o + 4, cur = 0;
    int kfl = 0;
    for (uint32_t i = 0; i < octCount; i++) {
        if ((i & 7) == 0) { cur = r.U8(p) | (r.U8(p + 1) << 8) | (r.U8(p + 2) << 16); p += 3; }
        oct[i] = cur & 7; cur >>= 3;
        if (oct[i] > 5) kfl++;
    }
    bool f16 = frames > 0xFF;
    if (f16 && (p & 1)) p++;
    // listas de cuadros clave
    uint16_t *kf = (uint16_t *)malloc(sizeof(uint16_t) * (kfl * 2 + 4096));
    uint32_t *kfStart = (uint32_t *)malloc(sizeof(uint32_t) * (kfl + 1));
    uint16_t *kfCount = (uint16_t *)malloc(sizeof(uint16_t) * (kfl + 1));
    if (!kf || !kfStart || !kfCount) { free(oct); free(kf); free(kfStart); free(kfCount); return false; }
    uint32_t kfUsed = 0, kfCap = kfl * 2 + 4096;
    for (int i = 0; i < kfl; i++) {
        uint32_t cnt = f16 ? r.U16(p) : r.U8(p); p += f16 ? 2 : 1;
        if (kfUsed + cnt + 2 > kfCap) { r.bad = true; break; }
        kfStart[i] = kfUsed; kfCount[i] = (uint16_t)(cnt + 2);
        kf[kfUsed++] = 0;
        for (uint32_t j = 0; j < cnt; j++) { kf[kfUsed++] = f16 ? r.U16(p) : r.U8(p); p += f16 ? 2 : 1; }
        kf[kfUsed++] = (uint16_t)frames;
    }
    p = (p + 3) & ~3u;
    // valores: contar cuántos floats hacen falta
    uint32_t nvals = 0;
    {
        int li = 0;
        for (uint32_t oi = 2; oi < octCount; oi++) {
            if (oct[oi] == 6) nvals += kfCount[li++];
            else if (oct[oi] == 7) nvals += 2 * kfCount[li++];
        }
    }
    float *store = (float *)malloc(sizeof(float) * (nvals + 1));
    if (!store || r.bad) { free(oct); free(kf); free(kfStart); free(kfCount); free(store); return false; }
    uint32_t used = 0;
    int li = 0, elem = 0;
    static const float kPi = 3.14159265358979f;
    for (uint32_t oi = 2; oi < octCount && !r.bad; oi++) {
        int code = oct[oi];
        if (code == 1) { elem += 3; continue; }
        int bone = elem / 9, e = elem % 9;
        Curve tmp; memset(&tmp, 0, sizeof(tmp));
        switch (code) {
            case 0: tmp.kind = CV_CONST; tmp.cval = 0; break;
            case 2: tmp.kind = CV_CONST; tmp.cval = kPi * 0.5f; break;
            case 3: tmp.kind = CV_CONST; tmp.cval = kPi; break;
            case 4: tmp.kind = CV_CONST; tmp.cval = -kPi * 0.5f; break;
            case 5: tmp.kind = CV_CONST; tmp.cval = r.F32(p); p += 4; break;
            case 6: {
                tmp.kind = CV_LINEAR; tmp.nkeys = kfCount[li]; tmp.frames = kf + kfStart[li];
                tmp.vals = store + used;
                for (int k = 0; k < tmp.nkeys; k++) { store[used++] = r.F32(p); p += 4; }
                li++;
                break;
            }
            case 7: {
                tmp.kind = CV_HERMITE; tmp.nkeys = kfCount[li]; tmp.frames = kf + kfStart[li];
                tmp.vals = store + used; used += tmp.nkeys;
                tmp.slopes = store + used; used += tmp.nkeys;
                for (int k = 0; k < tmp.nkeys; k++) {
                    tmp.vals[k] = r.F32(p);
                    tmp.slopes[k] = r.F32(p + 4) * (1.0f / 30.0f);
                    p += 8;
                }
                li++;
                break;
            }
        }
        if (bone < gfBones && bone < 64) md.curve[bone][e] = tmp;
        elem++;
    }
    free(oct); free(kfStart); free(kfCount);
    if (r.bad) { free(kf); free(store); return false; }
    md.keyFrames = kf; md.store = store;
    return true;
}

static void FreeMotion(MotionData &md) { free(md.keyFrames); free(md.store); md.keyFrames = nullptr; md.store = nullptr; }

// rotación del arco más corto de a hacia b (3x3)
static void RotBetween(float R[9], Vec3 a, Vec3 b) {
    a = Normalize(a); b = Normalize(b);
    Vec3 v = Cross(a, b);
    float c = Dot(a, b), s = Length(v);
    float I[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
    if (s < 1e-9f) { memcpy(R, I, sizeof(I)); return; }
    Vec3 k = Mul(v, 1.0f / s);
    float K[9] = { 0, -k.z, k.y, k.z, 0, -k.x, -k.y, k.x, 0 };
    float K2[9];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
        K2[i * 3 + j] = K[i * 3 + 0] * K[0 + j] + K[i * 3 + 1] * K[3 + j] + K[i * 3 + 2] * K[6 + j];
    float ang = atan2f(s, c), sa = sinf(ang), ca = 1 - cosf(ang);
    for (int i = 0; i < 9; i++) R[i] = I[i] + sa * K[i] + ca * K2[i];
}

static void Mat3MulInto34(Mat34 &M, const float A[9]) {   // M.rot = A * M.rot
    float r[9];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++)
        r[i * 3 + j] = A[i * 3 + 0] * M.m[0 * 4 + j] + A[i * 3 + 1] * M.m[1 * 4 + j] + A[i * 3 + 2] * M.m[2 * 4 + j];
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) M.m[i * 4 + j] = r[i * 3 + j];
}

static void WorldFromLocals(const Model &M, const Mat34 *L, Mat34 *W) {
    for (int i = 0; i < M.boneCount; i++) {
        if (M.parent[i] < 0) W[i] = L[i]; else M34Mul(W[i], W[M.parent[i]], L[i]);
    }
}

// Pose completa (FK + IK) en el cuadro f -> matrices de mundo (espacio del modelo)
static void SamplePose(const Model &M, const BindBone *bind, const MotionData &md, const int *gfToBch,
                       const uint8_t *gfFlags, int gfBones, float f, Mat34 *W) {
    Mat34 L[MAX_BONES];
    float s[MAX_BONES][3], rr[MAX_BONES][3], t[MAX_BONES][3];
    for (int i = 0; i < M.boneCount; i++) {
        memcpy(s[i], bind[i].s, 12); memcpy(rr[i], bind[i].r, 12); memcpy(t[i], bind[i].t, 12);
    }
    for (int g = 0; g < gfBones; g++) {
        int b = gfToBch[g];
        if (b < 0) continue;
        for (int e = 0; e < 9; e++) {
            const Curve &c = md.curve[g][e];
            if (c.kind == CV_NONE) continue;
            float v = EvalCurve(c, f);
            if (e < 3) t[b][e] = v; else if (e < 6) rr[b][e - 3] = v; else s[b][e - 6] = v;
        }
    }
    for (int i = 0; i < M.boneCount; i++) M34FromSRT(L[i], s[i], rr[i], t[i]);

    // IK de pies (huesos GF con bit 0x08)
    for (int g = 0; g < gfBones; g++) {
        if (!(gfFlags[g] & 8)) continue;
        int eff = gfToBch[g];
        if (eff < 0) continue;
        int mid = M.parent[eff];
        int root = mid >= 0 ? M.parent[mid] : -1;
        if (mid < 0 || root < 0) continue;
        Vec3 T = V3(t[eff][0], t[eff][1], t[eff][2]);              // objetivo (espacio del modelo)
        // el efector conserva su longitud de enlace y su rotación local de la curva
        float bt[3] = { bind[eff].t[0], bind[eff].t[1], bind[eff].t[2] };
        M34FromSRT(L[eff], s[eff], rr[eff], bt);
        WorldFromLocals(M, L, W);
        Vec3 H = V3(W[root].m[3], W[root].m[7], W[root].m[11]);
        float a = Length(V3(L[mid].m[3], L[mid].m[7], L[mid].m[11]));
        float b = Length(V3(L[eff].m[3], L[eff].m[7], L[eff].m[11]));
        float d = Length(Sub(T, H));
        float dmin = fabsf(a - b) + 1e-4f, dmax = a + b - 1e-4f;
        if (d < dmin) d = dmin;
        if (d > dmax) d = dmax;
        float cosv = (a * a + b * b - d * d) / (2 * a * b);
        if (cosv < -1) cosv = -1;
        if (cosv > 1) cosv = 1;
        float bend = 3.14159265f - acosf(cosv);
        // rodilla: rotación local de enlace del hueso medio * RotY(bend)
        Mat34 midBind, rotY;
        M34FromSRT(midBind, bind[mid].s, bind[mid].r, bind[mid].t);
        float zero3[3] = { 0, 0, 0 }, one3[3] = { 1, 1, 1 }, ry[3] = { 0, bend, 0 };
        M34FromSRT(rotY, one3, ry, zero3);
        Mat34 midNew;
        M34Mul(midNew, midBind, rotY);
        // conservar la traslación local actual del medio
        midNew.m[3] = L[mid].m[3]; midNew.m[7] = L[mid].m[7]; midNew.m[11] = L[mid].m[11];
        L[mid] = midNew;
        WorldFromLocals(M, L, W);
        Vec3 A = V3(W[eff].m[3], W[eff].m[7], W[eff].m[11]);
        float Q[9];
        RotBetween(Q, Sub(A, H), Sub(T, H));
        Mat34 rootW = W[root];
        Mat3MulInto34(rootW, Q);
        int rp = M.parent[root];
        if (rp >= 0) {
            Mat34 inv;
            M34Inverse(inv, W[rp]);
            M34Mul(L[root], inv, rootW);
        } else {
            L[root] = rootW;
        }
    }
    WorldFromLocals(M, L, W);
}

// Hueco del pack de movimientos de cada AnimId, por modelo (las bicis solo traen los suyos: el resto repite)
static const int kSlotsHero[ANIM_COUNT] = { 0, 2, 4, 7, 9, 11, 13 };
static const int kSlotsMach[ANIM_COUNT] = { 7, 7, 9, 7, 9, 7, 9 };
static const int kSlotsAcro[ANIM_COUNT] = { 11, 11, 13, 11, 13, 11, 13 };
// unidades de mundo por ciclo (avance por distancia); 0 = por tiempo (quieto y pedaleo)
static const float kStrideHero[ANIM_COUNT] = { 0.0f, 24.5f, 56.0f, 0.f, 0.f, 0.f, 0.f };   // medido: caminar 52.5 u/s, correr ~105 u/s

static bool LoadAnimations(Model &M, const BindBone *bind, Reader &mp, const int *kSlots, char *err, size_t cap) {
    uint32_t count = mp.U32(0);
    if (mp.bad || count < 6 || count > 1024) { SetErr(err, cap, "paquete de movimientos invalido"); return false; }
    uint32_t skelOff = mp.U32(4);
    // esqueleto GF
    int gfN = mp.U8(skelOff);
    if (gfN < 2 || gfN > 64) { SetErr(err, cap, "esqueleto GF invalido"); return false; }
    GfBone gb[64];
    memset(gb, 0, sizeof(gb));
    strcpy(gb[0].name, "Origin");
    uint32_t p = skelOff + 2;
    for (int i = 1; i < gfN; i++) { gb[i].flags = mp.U8(p + 1); p += 3; }
    for (int i = 1; i < gfN; i++) {
        int k = 0;
        while (p < mp.n && mp.d[p] && k < 23) gb[i].name[k++] = (char)mp.d[p++];
        while (p < mp.n && mp.d[p]) p++;
        p++;
    }
    int gfToBch[64]; uint8_t gfFlags[64];
    for (int i = 0; i < gfN; i++) { gfToBch[i] = FindBone(M, gb[i].name); gfFlags[i] = gb[i].flags; }

    const bool hero = kSlots == kSlotsHero;
    Mat34 W[MAX_BONES];
    for (int a = 0; a < ANIM_COUNT; a++) {
        // v16: los movimientos de bici (AnimId >= 3) son opcionales: si faltan, ese modelo sigue sin bici
        const bool optional = a >= ANIM_MACH_IDLE;
        uint32_t mo = ((uint32_t)kSlots[a] + 1 < count) ? mp.U32(4 + 4 * (kSlots[a] + 1)) : 0;
        MotionData *md = (MotionData *)malloc(sizeof(MotionData));
        if (!md) return false;
        if (!mo || !ParseMotion(mp, mo, gfN, *md)) {
            free(md);
            if (optional) { M.animFrames[a] = 0; M.animSkin[a] = nullptr; M.animHead[a] = nullptr; M.animStride[a] = 0.f; continue; }
            SetErr(err, cap, "movimiento corrupto");
            return false;
        }
        int N = md->frames;
        int nf = N + 1;                       // cuadros 0..N (N == 0 en los ciclos)
        M.animFrames[a] = N;
        M.animStride[a] = hero ? kStrideHero[a] : 0.f;
        M.animSkin[a] = (Mat34 *)malloc(sizeof(Mat34) * nf * M.boneCount);
        M.animHead[a] = (Vec3 *)malloc(sizeof(Vec3) * nf);
        if (!M.animSkin[a] || !M.animHead[a]) { FreeMotion(*md); free(md); SetErr(err, cap, "sin memoria (animacion)"); return false; }
        int head = FindBone(M, "loc_head");
        if (head < 0) head = FindBone(M, "Head");
        for (int f = 0; f < nf; f++) {
            SamplePose(M, bind, *md, gfToBch, gfFlags, gfN, (float)f, W);
            for (int b = 0; b < M.boneCount; b++) M34Mul(M.animSkin[a][f * M.boneCount + b], W[b], M.invBind[b]);
            Vec3 hp = head >= 0 ? V3(W[head].m[3], W[head].m[7], W[head].m[11]) : V3(0, M.height, 0);
            M.animHead[a][f] = hp;
        }
        FreeMotion(*md);
        free(md);
    }
    return true;
}

// ============================================================================================
// Carga completa
// ============================================================================================
static void FreeModel(Model &M) {
    free(M.verts); free(M.indices);
    for (int i = 0; i < MAX_TEX; i++) { free(M.tex[i].px); free(M.tex[i].raw); }
    for (int a = 0; a < ANIM_COUNT; a++) { free(M.animSkin[a]); free(M.animHead[a]); }
    memset(&M, 0, sizeof(M));
}

void FreeAll() { for (auto &m : g_models) FreeModel(m); }

const Model *Get(int which) {
    if (which < 0 || which >= MODEL_COUNT || !g_models[which].ok) return nullptr;
    return &g_models[which];
}

static bool LoadOne(ByteSource &src, uint64_t gOff, uint64_t gSize, int which, char *err, size_t cap) {
    Model &M = g_models[which];
    FreeModel(M);
    static const uint32_t kEntry[MODEL_COUNT] = { 171, 172, 537, 536 };
    static const int *const kSlotSet[MODEL_COUNT] = { kSlotsHero, kSlotsHero, kSlotsMach, kSlotsAcro };
    uint32_t entry = kEntry[which];
    // 1) leer la cabecera MM para saber hasta dónde descomprimir (modelo + movimientos hasta "correr")
    uint32_t sz = 0;
    uint8_t *mm = GarcEntry(src, gOff, gSize, entry, 0x80, sz);
    if (!mm || sz < 0x14 || mm[0] != 'M' || mm[1] != 'M') { free(mm); SetErr(err, cap, "pack MM no encontrado"); return false; }
    uint16_t cnt; uint32_t off0, off1, off2;
    memcpy(&cnt, mm + 2, 2); memcpy(&off0, mm + 4, 4); memcpy(&off1, mm + 8, 4); memcpy(&off2, mm + 12, 4);
    free(mm);
    if (cnt < 2 || off0 >= off1 || off1 >= off2 || off2 > 0x200000) { SetErr(err, cap, "pack MM invalido"); return false; }
    // el pack de movimientos completo hasta "correr" + margen (ver ANIM_FORMAT §1); se descomprime todo sub1
    uint32_t need = off1 + 0x20000;            // los movimientos 0..5 están al principio del pack
    if (need > off2) need = off2;
    mm = GarcEntry(src, gOff, gSize, entry, need, sz);
    if (!mm) { SetErr(err, cap, "no se pudo descomprimir el modelo"); return false; }
    if (sz <= off1 || off1 - off0 < 0x44) { free(mm); SetErr(err, cap, "pack MM incompleto"); return false; }

    // 2) BCH (subarchivo 0): copia propia porque la relocalización escribe
    BchCtx c;
    memset(&c, 0, sizeof(c));
    c.r.d = mm + off0; c.r.n = off1 - off0; c.r.bad = false;
    static BindBone bind[MAX_BONES];
    bool ok = Relocate(c) && ParseModel(c, M, bind, err, cap);
    if (ok) {
        Reader mp = { mm + off1, sz - off1, false };          // tamaño REAL descomprimido
        ok = LoadAnimations(M, bind, mp, kSlotSet[which], err, cap);
    }
    free(mm);
    if (!ok) { FreeModel(M); if (err && !err[0]) SetErr(err, cap, "modelo corrupto"); return false; }
    M.male = which == MODEL_BRENDAN;
    M.ok = true;
    return true;
}

bool LoadAll(ByteSource &src, char *err, size_t cap) {
    SetErr(err, cap, "");
    uint64_t gOff = 0, gSize = 0;
    if (!src.Find("/a/0/2/1", gOff, gSize)) { SetErr(err, cap, "GARC de modelos no encontrado"); return false; }
    bool a = LoadOne(src, gOff, gSize, MODEL_MAY, err, cap);
    bool b = LoadOne(src, gOff, gSize, MODEL_BRENDAN, err, cap);
    // v16: bicis (opcionales: si fallan, el amigo en bici se ve como antes, sin bici)
    char e2[64] = "";
    if (!LoadOne(src, gOff, gSize, MODEL_MACH, e2, sizeof(e2)) || !LoadOne(src, gOff, gSize, MODEL_ACRO, e2, sizeof(e2)))
        g_bikeErr = true;
    return a && b;
}

} // namespace Hero
