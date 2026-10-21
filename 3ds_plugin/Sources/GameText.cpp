#include "GameText.hpp"
#include "Lang.hpp"
#include "Log.hpp"
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace GameText {

static const u32 FILE_LOCATIONS = 90, FILE_SPECIES = 98;

// Una lista de textos: todas las cadenas UTF-8 seguidas en 'buf' y el principio de cada una en 'off'
struct List { char *buf; u32 *off; int n; };
// Los idiomas ya cargados no se liberan nunca (otro hilo puede estar usando sus cadenas). Desde la v17.0 r2 se cargan
// los 8 al arrancar (Preload, ~150 KB): antes se cargaban al elegirlos y en japonés/coreano se veían "?" unos segundos.
struct Set { bool ok; List species, locs; };
static Set s_sets[9];                       // por índice de textos del juego (1..8)
static volatile int s_cur = 0, s_en = 0;    // índices listos para usar (0 = ninguno)
static u32 s_bytes = 0;                     // memoria de todos los textos cargados (para el registro)

static inline u16 Rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static inline u32 Rd32(const u8 *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }

static int PutUtf8(char *o, u32 c) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 0x3F)); o[2] = (char)(0x80 | (c & 0x3F)); return 3;
}

// Texto de 6.ª generación: cabecera (secciones, líneas, ..., inicio de la sección) y por línea {offset, longitud};
// cada carácter va con XOR de una clave que rota 3 bits por carácter y avanza 0x2983 por línea.
static bool Decode(const u8 *b, u32 size, List &out) {
    if (size < 0x14) return false;
    u32 nlines = Rd16(b + 2), soff = Rd32(b + 12);
    if (!nlines || nlines > 4000 || soff + 4 + nlines * 8 > size) return false;
    u32 cap = 0;
    for (u32 i = 0; i < nlines; i++) cap += Rd16(b + soff + 4 + i * 8 + 4) * 3u + 1;
    out.buf = (char *)malloc(cap ? cap : 1);
    out.off = (u32 *)malloc(nlines * 4);
    if (!out.buf || !out.off) { free(out.buf); free(out.off); out.buf = nullptr; out.off = nullptr; return false; }
    u32 pos = 0, key = 0x7C89;
    for (u32 i = 0; i < nlines; i++) {
        u32 off = Rd32(b + soff + 4 + i * 8), len = Rd16(b + soff + 4 + i * 8 + 4);
        out.off[i] = pos;
        if (soff + off + len * 2 <= size) {
            u32 k = key;
            u16 s[64];
            u32 n = len < 64 ? len : 64;              // los nombres son cortos; lo demás se corta
            for (u32 j = 0; j < len; j++) {
                u16 c = Rd16(b + soff + off + j * 2) ^ (u16)k;
                k = ((k << 3) | (k >> 13)) & 0xFFFF;
                if (j < n) s[j] = c;
            }
            for (u32 j = 0; j < n; j++) {
                u32 c = s[j];
                if (c == 0 || c == 0xFFFF) break;
                if (c == 0x10) { if (j + 1 < n) j += 1 + s[j + 1]; continue; }   // variable: no hay en los nombres
                if (c == 0xE07F) c = ' ';
                else if (c == 0xE08D) c = 0x2026;          // …
                else if (c == 0xE08E) c = 0x2642;          // ♂
                else if (c == 0xE08F) c = 0x2640;          // ♀
                else if (c >= 0xE000 && c < 0xF900) continue;   // otros símbolos propios del juego
                pos += PutUtf8(out.buf + pos, c);
            }
        }
        out.buf[pos++] = 0;
        key = (key + 0x2983) & 0xFFFF;
    }
    if (pos < cap) { char *r = (char *)realloc(out.buf, pos); if (r) out.buf = r; }   // se reservó para 3 bytes por letra
    out.n = (int)nlines;
    s_bytes += pos + nlines * 4;
    return true;
}

static bool LoadFile(Hero::ByteSource &fs, u64 gOff, u64 gSize, u32 idx, List &out) {
    u32 sz = 0;
    u8 *b = Hero::GarcEntry(fs, gOff, gSize, idx, 0, sz);
    if (!b) return false;
    bool ok = Decode(b, sz, out);
    free(b);
    return ok;
}

static bool LoadSet(Hero::ByteSource &fs, int gi) {
    if (gi < 1 || gi > 8) return false;
    Set &S = s_sets[gi];
    if (S.ok) return true;
    char path[16];
    snprintf(path, sizeof(path), "/a/0/7/%d", gi);
    u64 off = 0, size = 0;
    if (!fs.Find(path, off, size)) return false;
    u64 t0 = osGetTime();
    if (!LoadFile(fs, off, size, FILE_SPECIES, S.species) || !LoadFile(fs, off, size, FILE_LOCATIONS, S.locs)) {
        Log::Write("textos del juego %d: no se pudieron leer", gi);
        return false;
    }
    __sync_synchronize();                                   // las listas, antes que 'ok' (otros hilos leen 'ok' primero)
    S.ok = true;
    Log::Write("textos del juego %d: %d especies, %d lugares (%lu ms)", gi, S.species.n, S.locs.n, (unsigned long)(osGetTime() - t0));
    return true;
}

void Preload(Hero::ByteSource &fs) {
    u64 t0 = osGetTime();
    int n = 0;
    for (int gi = 1; gi <= 8; gi++) if (LoadSet(fs, gi)) n++;
    if (s_sets[3].ok) s_en = 3;
    int cur = Lang::GameIndex(Lang::Ui());
    if (cur >= 1 && cur <= 8 && s_sets[cur].ok) s_cur = cur;
    Log::Write("textos del juego: %d idiomas cargados, %lu KB, %lu ms", n, (unsigned long)(s_bytes / 1024), (unsigned long)(osGetTime() - t0));
}

void Serve(Hero::ByteSource &fs) {
    static int wanted = 0;
    static u64 since = 0;
    Lang::Id ui = Lang::Ui();
    int gi = Lang::GameIndex(ui);
    u64 now = osGetTime();
    if (gi != wanted) { wanted = gi; since = now; }
    if (gi == s_cur) return;
    if (gi >= 1 && gi <= 8 && s_sets[gi].ok) { s_cur = gi; return; }   // ya cargado (al arrancar se cargan todos)
    if (now - since < 1000) return;                         // ~1 s estable antes de cargar
    if (LoadSet(fs, gi)) s_cur = gi;
    if (!s_en && (ui == Lang::JA || ui == Lang::KO) && LoadSet(fs, 3)) s_en = 3;
}

static const char *Get(int gi, bool species, u16 id) {
    if (gi < 1 || gi > 8) return nullptr;
    const Set &S = s_sets[gi];
    if (!S.ok) return nullptr;
    const List &L = species ? S.species : S.locs;
    if (id >= L.n) return nullptr;
    const char *s = L.buf + L.off[id];
    return *s ? s : nullptr;
}

// El idioma que se ve (si japonés/coreano aún no tiene letras, Lang::Shown da el inglés), si ya está cargado; si no, el
// último cargado: así un cambio de idioma en el menú cambia también los nombres en el mismo cuadro.
static int Pick() {
    int gi = Lang::GameIndex(Lang::Shown());
    return (gi >= 1 && gi <= 8 && s_sets[gi].ok) ? gi : s_cur;
}
const char *Species(u16 id) { return Get(Pick(), true, id); }
const char *Location(u16 id) { return Get(Pick(), false, id); }
const char *SpeciesOf(int gi, u16 id) { return Get(gi, true, id); }
const char *LocationOf(int gi, u16 id) { return Get(gi, false, id); }
const char *SpeciesEn(u16 id) { return Get(s_en ? s_en : (s_cur == 3 ? 3 : 0), true, id); }
int Ready() { return s_cur; }
const char *LocationEn(u16 id) { return Get(s_en ? s_en : (s_cur == 3 ? 3 : 0), false, id); }

} // namespace GameText
