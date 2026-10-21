#include "GameLang.hpp"
#include "Log.hpp"
#include <CTRPluginFramework.hpp>
#include <cstring>
#include <cstdlib>

using namespace CTRPluginFramework;

namespace GameLang {

// code.bin (build verificada, KB §1): SetLanguage(u8 idioma, Heap*, bool kanji) y el puntero al byte del idioma
static const u32 FN_SET_LANG = 0x0033A440;
static const u32 kProlog[3]  = { 0xE92D4010, 0xE1A04001, 0xE59F1084 };   // push {r4,lr}; mov r4,r1; ldr r1,[pc,#0x84]
static const u32 LIT_ADDR    = 0x0033A4D4;                               // ese literal = 0x0062F7D0
static const u32 LANG_PTR    = 0x0062F7D0;
static const u32 kBuild[4]   = { 0xE59F0004, 0xE5900000, 0xE12FFF1E, 0x0062F7C4 };   // GetGameSystem (0x14E348)

static Hook s_hook;
static bool s_hooked = false;
static const char *s_why = "sin probar";
static int  s_setting = -1;                 // -1 = aún no leído
static volatile int s_saveLang = 0, s_saveKanji = 0, s_applied = 0, s_calls = 0;

static inline u32 R32(u32 a) { return *(volatile u32 *)a; }
static bool ValidCode(int c) { return c == 1 || c == 2 || c == 3 || c == 4 || c == 5 || c == 7 || c == 8 || c == JP_KANJI; }

// "glang=N" de mmo_settings.txt (se lee aquí mismo: esto ocurre antes de que main() cargue la configuración)
static int ReadSetting() {
    File f;
    if (File::Open(f, "mmo_settings.txt", File::READ) != 0) return 0;
    char buf[200] = {};
    u64 sz = f.GetSize();
    if (sz > sizeof(buf) - 1) sz = sizeof(buf) - 1;
    f.Read(buf, (u32)sz);
    f.Close();
    const char *p = strstr(buf, "glang=");
    if (!p) return 0;
    int v = atoi(p + 6);
    return ValidCode(v) ? v : 0;
}

static void SetLangHook(u32 lang, u32 heap, u32 kanji) {
    s_calls = s_calls + 1;
    s_saveLang = (int)(lang & 0xFF);
    s_saveKanji = kanji ? 1 : 0;
    if (s_setting < 0) s_setting = ReadSetting();
    int ov = s_setting;
    if (ValidCode(ov)) {
        if (ov == JP_KANJI) { lang = 1; kanji = 1; }
        else { lang = (u32)ov; if (ov == 1) kanji = 0; }
        s_applied = ov;
    } else {
        s_applied = 0;
    }
    HookContext::GetCurrent().OriginalFunction<void>(lang, heap, kanji);
}

void Early() {
    if (s_hooked) return;
    for (int i = 0; i < 4; i++) if (R32(0x0014E348 + i * 4) != kBuild[i]) { s_why = "juego distinto"; return; }
    for (int i = 0; i < 3; i++) if (R32(FN_SET_LANG + i * 4) != kProlog[i]) { s_why = "funcion distinta"; return; }
    if (R32(LIT_ADDR) != LANG_PTR) { s_why = "literal distinto"; return; }
    s_setting = ReadSetting();                       // si el sistema de archivos aún no responde, se lee al primer uso
    s_hook.InitializeForMitm(FN_SET_LANG, (u32)SetLangHook);
    if (s_hook.Enable() != HookResult::Success) { s_why = "no se pudo enganchar"; return; }
    s_hooked = true;
    s_why = "enganchado";
}

void LogStatus() {
    Log::Write("idioma del juego: %s; ajuste %d; la partida pidio %d%s; aplicado %d; ahora %d (%d llamadas)", s_why, s_setting,
               s_saveLang, s_saveKanji ? " (kanji)" : "", s_applied, Current(), s_calls);
}

bool Hooked() { return s_hooked; }
int  SaveLang() { return s_saveLang; }
int  Applied() { return s_applied; }
bool CurrentKanji() { return s_applied == JP_KANJI || (s_applied == 0 && s_saveLang == 1 && s_saveKanji); }

int Current() {
    u32 p = R32(LANG_PTR);
    if (p < 0x08000000 || p >= 0x10000000) return 0;
    if (!Process::CheckAddress(p & ~0xFFFu, MEMPERM_READ)) return 0;
    return *(volatile u8 *)p;
}

} // namespace GameLang
