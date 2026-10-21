#include "Lang.hpp"
#include "GameLang.hpp"
#include "GameFont.hpp"
#include "Config.hpp"
#include <cstring>

namespace Lang {

struct Entry { const char *s[COUNT]; };   // es, en, fr, it, de, ja, ko (nullptr = el inglés)
#include "LangTable.inc"                  // kTable[], kTableN (generado: tools/i18n/gen_lang.py)

// Tabla hash (dirección abierta) del texto español -> índice. Se arma una vez al arrancar (Init en main).
static const int HSIZE = 2048;
static s16 s_hash[HSIZE];
static bool s_built = false;

static u32 Hash(const char *s) {
    u32 h = 2166136261u;
    for (; *s; s++) { h ^= (u8)*s; h *= 16777619u; }
    return h;
}

static void Build() {
    for (int i = 0; i < HSIZE; i++) s_hash[i] = -1;
    for (int i = 0; i < kTableN && i < HSIZE / 2; i++) {
        u32 h = Hash(kTable[i].s[ES]) & (HSIZE - 1);
        while (s_hash[h] >= 0) h = (h + 1) & (HSIZE - 1);
        s_hash[h] = (s16)i;
    }
    s_built = true;
}

static int Find(const char *es) {
    u32 h = Hash(es) & (HSIZE - 1);
    for (int n = 0; n < HSIZE; n++) {
        int i = s_hash[h];
        if (i < 0) return -1;
        if (!strcmp(kTable[i].s[ES], es)) return i;
        h = (h + 1) & (HSIZE - 1);
    }
    return -1;
}

void Init() { if (!s_built) Build(); }

Id FromGame(int c) {
    switch (c) {
        case 1: case GameLang::JP_KANJI: return JA;
        case 2: return EN;
        case 3: return FR;
        case 4: return IT;
        case 5: return DE;
        case 8: return KO;
        default: return ES;
    }
}

int GameIndex(Id id) {
    static const int k[COUNT] = { 7, 3, 4, 5, 6, 1, 8 };
    if (id == JA) {                                     // kana o kanji: lo elegido en el menú; en automático, el juego
        if (Cfg::gameLang == GameLang::JP_KANJI) return 2;
        if (Cfg::gameLang == 1) return 1;
        return GameLang::CurrentKanji() ? 2 : 1;
    }
    return k[id < COUNT ? id : 0];
}

// v17.0 r2: lo elegido en el menú se lee directo cada vez. Antes se guardaba en variables compartidas por el menú y el
// hilo de recursos; al cambiar de idioma uno podía ver un instante el idioma anterior y el menú pintaba un texto japonés
// con la fuente del sistema ("?" en la "ん", y se quedaba así). En automático, el idioma del juego se lee como mucho cada
// 100 ms (cuesta una llamada al sistema) y no cambia mientras se juega.
Id Ui() {
    int cfg = Cfg::gameLang;
    if (cfg) return FromGame(cfg);
    static volatile int s_game = 0;
    static volatile u64 s_next = 0;
    u64 now = osGetTime();
    if (now >= s_next) { s_next = now + 100; s_game = GameLang::Current(); }
    int cur = s_game;
    return cur ? FromGame(cur) : ES;
}

Id Shown() {
    Id u = Ui();
    if ((u == JA || u == KO) && !GameFont::Ready(u)) return EN;
    return u;
}

bool Cjk() { Id u = Shown(); return u == JA || u == KO; }

static const char *Lookup(const char *es, Id u) {
    if (!es || !*es || u == ES) return es;
    if (!s_built) Build();
    int i = Find(es);
    if (i < 0) return es;
    const char *t = kTable[i].s[u];
    if (!t || !*t) t = kTable[i].s[EN];
    return (t && *t) ? t : es;
}

const char *T(const char *es) { return Lookup(es, Shown()); }

const char *TOsd(const char *es) {
    Id u = Shown();
    return Lookup(es, (u == JA || u == KO) ? EN : u);
}

const char *Name(int code) {
    switch (code) {
        case 0: return T("Automático");
        case 1: return "日本語 (かな)";
        case GameLang::JP_KANJI: return "日本語 (漢字)";
        case 2: return "English";
        case 3: return "Français";
        case 4: return "Italiano";
        case 5: return "Deutsch";
        case 7: return "Español";
        case 8: return Shown() == KO ? "한국어" : T("Coreano");   // el hangul solo está en la fuente del juego coreana
        default: return "?";
    }
}

int Rows() { return kTableN; }
const char *Row(int i, Id id) {
    if (i < 0 || i >= kTableN || id >= COUNT) return nullptr;
    const char *t = kTable[i].s[id];
    return (t && *t) ? t : kTable[i].s[EN];
}

} // namespace Lang
