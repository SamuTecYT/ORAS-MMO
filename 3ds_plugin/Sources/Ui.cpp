/*
 * Ui.cpp — ORAS MMO v16.1 (rediseño "estilo Apple")
 * Menú propio (SELECT): juego en pausa, interfaz completa en ambas pantallas, control por botones
 * (cruceta/stick, A, B, L, R) o táctil/ratón en la pantalla inferior.
 *
 * Diseño (principios de las charlas de diseño de Apple, llevados a las dos pantallas de la 3DS):
 *  - Materiales: paneles de "cristal" translúcido con sombra difusa sobre el juego desenfocado y oscurecido
 *    (UiGfx). Atenuar el fondo centra la atención en el menú (tarea modal).
 *  - Movimiento con muelles críticos (sin rebote): la selección se desliza a su sitio, el contenido entra desde
 *    el lado hacia el que navegas y vuelve por el mismo camino; todo se puede interrumpir a mitad.
 *  - Respuesta inmediata: lo que tocas se ilumina al apoyar el dedo y la acción ocurre al soltarlo (si lo
 *    sacas del botón, se cancela). Controles directos: interruptores y selectores de 3 opciones en la táctil.
 *  - Nada se pisa: cada zona tiene su sitio fijo (p. ej. el nombre va encima del personaje 3D, no sobre él).
 */
#include "GameHook.hpp"
#include "Ui.hpp"
#include "UiGfx.hpp"
#include "App.hpp"
#include "Config.hpp"
#include "GameState.hpp"
#include "Players.hpp"
#include "TownMap.hpp"
#include "Render3D.hpp"
#include "SkinColors.hpp"
#include "Lang.hpp"
#include "GameLang.hpp"
#include "HeroAssets.hpp"
#include "Overlay.hpp"
#include "Follower3D.hpp"
#include "Protocol.hpp"
#include <strings.h>
#include "Log.hpp"
#include "Emotes.hpp"
#include "GameStats.hpp"
#include "Species.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <malloc.h>
#include <unistd.h>

extern "C" char *fake_heap_end;                                  // libsysbase: fin del montón del plugin

// Lo que aún queda del montón sin pedir nunca (el plugin de 10 MB deja ~5 MB)
static unsigned long HeapRest() {
    char *brk = (char *)sbrk(0);
    return (fake_heap_end && brk && fake_heap_end > brk) ? (unsigned long)(fake_heap_end - brk) : 0;
}

// v16.2: memoria libre del montón (lo libre dentro de lo ya pedido + lo que aún queda por pedir)
static void MemReport(const char *tag) {
    struct mallinfo mi = mallinfo();
    char *brk = (char *)sbrk(0);
    unsigned long rest = (fake_heap_end && brk && fake_heap_end > brk) ? (unsigned long)(fake_heap_end - brk) : 0;
    Log::Write("memoria (%s): libre %lu KB = %lu KB en huecos + %lu KB sin usar; en uso %lu KB", tag,
               (unsigned long)((mi.fordblks + rest) / 1024), (unsigned long)(mi.fordblks / 1024), rest / 1024,
               (unsigned long)(mi.uordblks / 1024));
}

using namespace CTRPluginFramework;
using UiGfx::Rgb;
using UiGfx::RGB;

namespace Ui {

// ---------------------------------------------------------------------------------------------
// Paleta (la de siempre: azul marino con acentos dorados; azul para lo elegido)
static const Rgb C_TEXT   = { 242, 246, 255 };
static const Rgb C_SUB    = { 156, 174, 216 };
static const Rgb C_DIM    = { 100, 114, 156 };
static const Rgb C_GOLD   = { 255, 204, 82 };
static const Rgb C_GREEN  = { 84, 224, 136 };
static const Rgb C_ORANGE = { 255, 152, 64 };
static const Rgb C_RED    = { 240, 84, 96 };
static const Rgb C_NAVY   = { 10, 16, 36 };
static const Rgb C_GLASS0 = { 24, 36, 74 };      // cristal: arriba
static const Rgb C_GLASS1 = { 14, 22, 50 };      // cristal: abajo
static const Rgb C_GLASSM = { 18, 28, 60 };      // color medio (para "desvanecer" el texto)
static const Rgb C_EDGE   = { 96, 130, 210 };
static const Rgb C_ACC0   = { 70, 132, 244 };    // selección: arriba
static const Rgb C_ACC1   = { 36, 84, 206 };     // selección: abajo
static const Rgb C_ACCE   = { 160, 204, 255 };
static const Rgb C_WHITE  = { 255, 255, 255 };

static const u8 kPlayerColors[12][3] = {
    {255, 214, 64}, {90, 200, 255}, {255, 120, 120}, {140, 255, 140}, {255, 150, 255}, {255, 170, 70},
    {160, 160, 255}, {120, 255, 230}, {255, 255, 150}, {200, 140, 255}, {255, 200, 200}, {180, 230, 120}
};

enum Section { SEC_INICIO = 0, SEC_SALA, SEC_RANKING, SEC_CHAT, SEC_COMBATE, SEC_PERSONAJE, SEC_PANTALLA, SEC_CONEXION, SEC_AYUDA, SEC_COUNT };
static const char *const kSecTitle[SEC_COUNT] = { "Inicio", "Jugadores", "Ranking", "Chat y emotes", "Combates", "Mi personaje", "Pantalla", "Conexión", "Ayuda" };
// Qué es cada sección (se muestra abajo del panel mientras se elige la sección)
static const char *const kSecHelp[SEC_COUNT] = {
    "Lo más usado, a un toque. Empieza por aquí.",
    "Quién está conectado y dónde. Toca a alguien para invitarlo.",
    "Quién va mejor: medallas, Pokédex, niveles y tiempo de juego.",
    "Saluda o escribe a toda la sala; también emotes.",
    "Combates e intercambios con tus amigos (por el PSS).",
    "Elige el color de tu ropa y de tu etiqueta.",
    "Qué muestra el MMO sobre el juego (HUD, nombres, mapa).",
    "Tu nombre, el código de sala y el servidor.",
    "Guías rápidas de cada función del MMO.",
};

struct Option { char label[64]; char value[48]; char help[112]; int id; bool enabled; u8 color; };
static Option s_opt[32];
static int    s_optCount = 0;

static int  s_sec = SEC_INICIO, s_item = 0, s_scroll = 0;
static bool s_inContent = false;
static int  s_subPlayer = 0;          // Sala: jugador elegido (acciones)
static bool s_showPss = false;
static bool s_open = false, s_wantCtrpf = false;
static char s_toast[96] = "";
static u64  s_toastUntil = 0;
static MMO::RemoteView s_views[MAX_MMO_PLAYERS];

static void Toast(const char *t) { snprintf(s_toast, sizeof(s_toast), "%s", TR(t)); s_toastUntil = osGetTime() + 2500; }

// ---------------------------------------------------------------------------------------------
// Opciones de cada sección
static void AddOpt(const char *label, const char *value, int id, bool enabled = true, u8 color = 0, const char *help = nullptr) {
    if (s_optCount >= 32) return;
    Option &o = s_opt[s_optCount++];
    snprintf(o.label, sizeof(o.label), "%s", TR(label));                // v17: textos en el idioma del MMO
    snprintf(o.value, sizeof(o.value), "%s", value ? TR(value) : "");
    snprintf(o.help, sizeof(o.help), "%s", help ? TR(help) : "");
    o.id = id; o.enabled = enabled; o.color = color;
}

static const char *ColorName(int v) { return TR(SkinColors::Name(v)); }
// v16.5: gorro (Bruno) / lazo (Aura): 0 como la ropa, 1 original, 2..16 un color propio
static const char *AccName(int v) { return (v <= 0 || v >= Cfg::NUM_ACC) ? TR("Como la ropa") : TR(SkinColors::Name(v - 1)); }
static bool IsMay() { return App::LocalModelId() == MMO_MODEL_MAY; }
static const char *TagName(int v) { return TR(SkinColors::TagName(v)); }
static bool IsCycle(int id) { return (id >= 5001 && id <= 5005) || (id >= 6001 && id <= 6005); }

// v17: idioma del juego y del MMO. Orden del selector: automático, inglés, español, francés, italiano, alemán,
// japonés (kana), japonés (kanji), coreano. Códigos de GameLang.hpp.
static const int kLangCodes[9] = { 0, 2, 7, 3, 4, 5, 1, GameLang::JP_KANJI, 8 };
static const char *LangValue() {
    static char v[48];
    if (Cfg::gameLang) return Lang::Name(Cfg::gameLang);
    int save = GameLang::SaveLang();
    if (save) { snprintf(v, sizeof(v), "%s (%s)", Lang::Name(0), Lang::Name(save == 1 && GameLang::CurrentKanji() ? GameLang::JP_KANJI : save)); return v; }
    return Lang::Name(0);
}
static void CycleLang(int dir) {
    int k = 0;
    for (int i = 0; i < 9; i++) if (kLangCodes[i] == Cfg::gameLang) k = i;
    k = (k + (dir < 0 ? 8 : 1)) % 9;
    Cfg::gameLang = kLangCodes[k];
    Cfg::Save();
}

// Ayuda por temas (se lee en la pantalla de abajo)
struct HelpTopic { const char *title; const char *lines[6]; };
static const HelpTopic kHelp[6] = {
    { "Cómo jugar con amigos", { "1. Todos instalan el MMO (el ZIP).", "2. Abran el juego: se conecta solo", "   al servidor del MMO.",
                                  "3. Mismo código de sala = se ven", "   (por defecto 1234).", "" } },
    { "Ver a tus amigos", { "Se ven en 3D con su personaje real", "cuando están en tu mismo lugar.", "Si están lejos, una flecha en el",
                             "borde con su nombre y distancia.", "Abre el AreaNav para verlos a", "todos en el mapa de Hoenn." } },
    { "Chat, emotes y susurros", { "Mensaje para todos: lo ve la sala", "y sale en una nube sobre ti.", "Susurro: solo lo recibe esa",
                                    "persona (aparece en rosa).", "Emotes: dibujitos en una nube", "sobre tu personaje." } },
    { "Combates e intercambios", { "1. Invita desde Jugadores.", "2. Si acepta, uno crea sala en", "   Azahar: Multijugador > Crear.",
                                    "3. El otro: Multijugador > Unirse.", "4. Abran el PSS y toquen Wi-Fi;", "   elijan Combate o Intercambio." } },
    { "Personalización", { "Eres el personaje de tu partida.", "Elige el color de tu ropa y el", "de tu etiqueta.", "Los demás te ven así. Tú también",
                            "te ves con tu color en el juego", "(se puede desactivar)." } },
    { "Si algo falla", { "- Conexión > Reconectar.", "- Revisa tu Internet.", "- Mismo código de sala que tus",
                          "  amigos (por defecto 1234).", "- Si sigue igual, reinicia el", "  juego." } },
};
static int s_helpTopic = 0;

// Ranking: yo + cada jugador con estadísticas, ordenados por puntos
struct RankEntry { char name[16]; u8 id; bool me, offline; u16 days; Stats::Mine m; int score; };
static RankEntry s_rank[MMO::RANKBOOK_MAX + 1];
static MMO::RankBookEntry s_book[MMO::RANKBOOK_MAX];
static int s_rankN = 0;
// Orden: puntos, medallas, capturados, nivel máximo y, al final, el nombre
static bool RankBefore(const RankEntry &a, const RankEntry &b) {
    int c = Rank::Compare(Stats::ToRank(a.m), Stats::ToRank(b.m));
    if (c) return c < 0;
    return strcmp(a.name, b.name) < 0;
}
static void BuildRanking() {
    s_rankN = 0;
    RankEntry &me = s_rank[s_rankN++];
    std::string nm = Cfg::Name();
    App::AsciiFold(nm.c_str(), me.name, sizeof(me.name));
    me.id = App::MyId(); me.me = true; me.offline = false; me.days = 0; me.m = Stats::GetMine(); me.score = Stats::Score(me.m);
    // v16: ranking guardado por el servidor + STATS en vivo (incluye a los desconectados)
    char myRaw[16];
    Proto::CopyText(myRaw, sizeof(myRaw), nm.c_str());
    int nb = MMO::g_players.CopyRankBook(s_book, MMO::RANKBOOK_MAX);
    for (int i = 0; i < nb; i++) {
        const MMO::RankBookEntry &b = s_book[i];
        if (MMO::SameRankName(b.name, myRaw)) continue;   // ese soy yo (v16.1: por nombre, como el servidor)
        RankEntry &e = s_rank[s_rankN++];
        App::AsciiFold(b.name, e.name, sizeof(e.name));
        e.id = 0; e.me = false; e.offline = b.offline; e.days = b.daysAgo;
        for (int v = 0; v < MAX_MMO_PLAYERS && !b.offline; v++)
            if (s_views[v].valid && MMO::SameRankName(s_views[v].name, b.name)) e.id = s_views[v].id;
        memset(&e.m, 0, sizeof(e.m));
        const MMO::RemoteStats &st = b.stats;
        e.m.hours = st.hours; e.m.minutes = st.minutes; e.m.badges = st.badges; e.m.caught = st.caught; e.m.seen = st.seen;
        e.m.money = st.money; e.m.partyCount = st.party; e.m.avgLevel = st.avgLv; e.m.maxLevel = st.maxLv; e.m.leadSpecies = st.lead;
        e.m.okTime = st.okMask & 1; e.m.okDex = (st.okMask & 2) != 0; e.m.okParty = (st.okMask & 4) != 0; e.m.okMisc = (st.okMask & 8) != 0; e.m.league = (st.okMask & 0x10) != 0;
        e.score = Stats::Score(e.m);
    }
    for (int a = 1; a < s_rankN; a++) {
        RankEntry t = s_rank[a]; int j = a - 1;
        while (j >= 0 && RankBefore(t, s_rank[j])) { s_rank[j + 1] = s_rank[j]; j--; }
        s_rank[j + 1] = t;
    }
}
static int Badges(u8 b) { int n = 0; for (int i = 0; i < 8; i++) n += (b >> i) & 1; return n; }
static bool s_whisperPick = false;     // Chat: eligiendo a quién susurrar

static const MMO::RemoteView *FindView(int id) {
    for (int i = 0; i < MAX_MMO_PLAYERS; i++) if (s_views[i].valid && s_views[i].id == id) return &s_views[i];
    return nullptr;
}

static void BuildOptions() {
    s_optCount = 0;
    char v[48];
    switch (s_sec) {
        case SEC_INICIO: {
            App::Invite inv = App::GetInvite();
            if (inv.pending) {
                char nm[24]; App::AsciiFold(inv.name, nm, sizeof(nm));
                snprintf(v, sizeof(v), TR(inv.type == 1 ? "%s te invitó a combatir" : "%s te invitó a intercambiar"), nm);
                AddOpt(v, "Responder", 9005, true, 0, "Acepta o rechaza la invitación que te enviaron.");
            }
            snprintf(v, sizeof(v), "%d", MMO::g_players.ActiveCount());
            AddOpt("Ver quién está jugando", v, 9001, true, 0, "Lista de jugadores de tu sala y dónde está cada uno.");
            AddOpt("Saludar a todos", "", 9002, App::IsConnected(), 0, "Envía Hola a todos! a la sala con un toque.");
            AddOpt("Enviar un mensaje", "", 9003, App::IsConnected(), 0, "Mensajes rápidos, escribir el tuyo o un emote.");
            AddOpt("Invitar a combatir o intercambiar", "", 9004, App::IsConnected(), 0, "Elige a un amigo de la lista y el tipo de invitación.");
            AddOpt("Cambiar mis colores", "", 9006, true, 0, "El color de tu ropa y de tu etiqueta para distinguirte.");
            break;
        }
        case SEC_SALA:
            if (s_subPlayer) {
                const MMO::RemoteView *rv = FindView(s_subPlayer);
                if (!rv) { s_subPlayer = 0; BuildOptions(); return; }
                AddOpt("Invitar a un combate", "", 2001, true, 0, "Le llega un aviso; si acepta, conecten por el PSS.");
                AddOpt("Invitar a un intercambio", "", 2002, true, 0, "Le llega un aviso; si acepta, conecten por el PSS.");
                AddOpt("Volver a la lista", "", 2099, true, 0, "Regresa a la lista de jugadores.");
                break;
            }
            for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
                const MMO::RemoteView &rv = s_views[i];
                if (!rv.valid) continue;
                char nm[24]; App::AsciiFold(rv.name, nm, sizeof(nm));
                snprintf(v, sizeof(v), "%s%s", TownMap::ZoneName(rv.zone), (rv.flags & MMO_FLAG_IN_BATTLE) ? TR(" (combate)") : (rv.flags & MMO_FLAG_MENU) ? TR(" (menú)") : "");
                AddOpt(nm, v, 1000 + rv.id, true, (u8)rv.id, "Pulsa A (o tócalo) para invitarlo a combatir o intercambiar.");
            }
            if (!s_optCount) AddOpt("Nadie más en la sala por ahora", "", 0, false, 0, "Cuando tus amigos se conecten aparecerán aquí.");
            break;
        case SEC_RANKING:
            BuildRanking();
            for (int i = 0; i < s_rankN; i++) {
                char lab[32], val[24];
                snprintf(lab, sizeof(lab), "%d. %.13s%s", i + 1, s_rank[i].name, s_rank[i].me ? TR(" (tú)") : s_rank[i].offline ? TR(" (desc.)") : "");
                snprintf(val, sizeof(val), TR("%d pts"), s_rank[i].score);
                AddOpt(lab, val, 1500 + i, true, s_rank[i].me ? 0 : s_rank[i].id, "Pulsa A para ver su ficha completa abajo.");
            }
            break;
        case SEC_CHAT:
            if (s_whisperPick) {
                for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
                    const MMO::RemoteView &rv = s_views[i];
                    if (!rv.valid) continue;
                    char nm[24]; App::AsciiFold(rv.name, nm, sizeof(nm));
                    AddOpt(nm, "", 3200 + rv.id, true, (u8)rv.id, "Escríbele un mensaje que solo esa persona verá.");
                }
                if (!s_optCount) AddOpt("No hay nadie a quien susurrar", "", 0, false, 0, "Cuando haya amigos conectados podrás susurrarles.");
                AddOpt("Volver", "", 3299, true, 0, "Regresa al chat.");
                break;
            }
            AddOpt("Mensaje para todos...", "", 3000, App::IsConnected(), 0, "Escribe algo para toda la sala (máx. 19 letras).");
            AddOpt("Susurrar a alguien...", "", 3010, App::IsConnected(), 0, "Mensaje privado: solo lo verá esa persona.");
            for (int i = 0; i < 8; i++) AddOpt(App::kQuickChats[i], "", 3001 + i, App::IsConnected(), 0, "Mensaje rápido: lo verá toda la sala.");
            for (int i = 0; i < Emotes::COUNT; i++) { snprintf(v, sizeof(v), TR("Emote: %s"), TR(Emotes::kNames[i])); AddOpt(v, "", 3101 + i, App::IsConnected(), 0, "Aparece en una nube sobre tu personaje."); }
            break;
        case SEC_COMBATE: {
            App::Invite inv = App::GetInvite();
            if (inv.pending) {
                char nm[24]; App::AsciiFold(inv.name, nm, sizeof(nm));
                snprintf(v, sizeof(v), TR("Aceptar a %s"), nm);
                AddOpt(v, inv.type == 1 ? "combate" : "intercambio", 4001, true, 0, "Acepta y sigan los pasos del PSS de abajo.");
                AddOpt("Rechazar invitación", "", 4002, true, 0, "Le avisa que no puedes ahora.");
            }
            AddOpt("Invitar a un amigo", "", 4010, App::IsConnected(), 0, "Elige a quién en la lista de jugadores.");
            App::Outgoing og = App::GetOutgoing();
            if (og.pending) AddOpt("Cancelar mi invitación", "", 4020, true, 0, "Retira la invitación que enviaste.");
            AddOpt("Cómo conectar (PSS)", s_showPss ? "ocultar" : "ver", 4030, true, 0, "Los 4 pasos para combatir o intercambiar de verdad.");
            break;
        }
        case SEC_PERSONAJE:
            AddOpt("Ropa", ColorName(Cfg::colorVariant), 5002, true, 0, "El color de tu ropa: así te distinguen.");
            AddOpt(IsMay() ? "Lazo" : "Gorro", AccName(Cfg::accColor), 5005, true, 0,
                   IsMay() ? "Tu lazo: como la ropa, el original u otro color." : "Tu gorro: como la ropa, el original u otro color.");
            AddOpt("Nombre", TagName(Cfg::tagColor), 5003, true, 0, "El color de la etiqueta con tu nombre.");
            AddOpt("Verme así", Cfg::showMySkin ? "Sí" : "No", 5004, true, 0, "Sí: tú también te ves con tu color en el juego.");
            break;
        case SEC_PANTALLA:
            AddOpt("Recuadro de estado", Cfg::hudMode == 1 ? "Siempre" : Cfg::hudMode == 2 ? "Nunca" : "Auto", 6001, true, 0, "Auto: solo unos segundos cuando algo cambia (más HD).");
            AddOpt("Nombres", Cfg::showNames ? "Visibles" : "Ocultos", 6002, true, 0, "Las etiquetas con el nombre sobre cada amigo.");
            AddOpt("Pokémon acompañante", Cfg::followers == 1 ? "Solo el mío" : Cfg::followers == 2 ? "Ninguno" : "Todos", 6003, true, 0, "El primer Pokémon de tu equipo te sigue por el mapa.");
            AddOpt("Rendimiento", Cfg::perfMode == 1 ? "Calidad" : Cfg::perfMode == 2 ? "Ahorro" : "Auto", 6004, true, 0, "Auto: baja detalles solo si tu móvil o PC va justo de FPS.");
            AddOpt("Idioma / Language", LangValue(), 6005, true, 0, "El del juego y el del MMO. Los textos del juego cambian al reiniciarlo.");
            break;
        case SEC_CONEXION: {
            std::string nm = Cfg::Name(), room = Cfg::Room();
            AddOpt("Mi nombre", nm.c_str(), 7001, true, 0, "El nombre que ven los demás sobre tu personaje.");
            AddOpt("Código de sala", room.c_str(), 7002, true, 0, "Solo ves a quienes usan el mismo código.");
            AddOpt("Servidor", Cfg::HasManualIp() ? Cfg::ServerIp().c_str() : "Automático", 7003, true, 0, "Déjalo en Automático salvo que el anfitrión diga otra IP.");
            AddOpt("Reconectar", "", 7004, true, 0, "Vuelve a conectar si algo falló.");
            break;
        }
        default:
            for (int i = 0; i < 6; i++) AddOpt(kHelp[i].title, "", 8101 + i, true, 0, "Pulsa A para leerlo en la pantalla de abajo.");
            break;
    }
    if (s_item >= s_optCount) s_item = s_optCount ? s_optCount - 1 : 0;
    if (s_item < 0) s_item = 0;
}

// ---------------------------------------------------------------------------------------------
// Acciones
static void TypeText(const char *title, int maxLen, std::string &out) {
    Keyboard kb(TR(title));
    kb.SetMaxLength((u32)maxLen);
    std::string s;
    if (kb.Open(s) == 0) out = s;
    else out.clear();
    UiGfx::Invalidate();                                       // el teclado pintó encima: hay que repintar todo
}

static void Activate(const Option &o, int dir) {
    if (!o.enabled) return;
    int id = o.id;
    if (id >= 1000 && id < 1000 + 64) { s_subPlayer = id - 1000; s_item = 0; return; }
    switch (id) {
        case 2001: case 2002: {
            if (!App::IsConnected()) { Toast("Sin conexión"); break; }
            UiSendSimple(MMO_PKT_EVENT_REQ, (u8)s_subPlayer, (u8)(id - 2000), nullptr);
            App::Outgoing og; og.pending = true; og.to = (u8)s_subPlayer; og.type = (u8)(id - 2000);
            App::SetOutgoing(og);
            Toast(id == 2001 ? "Invitación a combate enviada" : "Invitación a intercambio enviada");
            s_subPlayer = 0; s_sec = SEC_COMBATE; s_showPss = true; s_item = 0;
            break;
        }
        case 2099: s_subPlayer = 0; s_item = 0; break;
        case 9001: case 9004: s_sec = SEC_SALA; s_subPlayer = 0; s_item = 0; s_scroll = 0; break;
        case 9002:
            UiSendSimple(MMO_PKT_CHAT, 1, 0, nullptr);
            { std::string me = Cfg::Name() + ": " + Lang::TOsd(App::kQuickChats[0]); App::PushChat(me.c_str(), 0xFFFFFF); }
            App::SetMyBubble(Lang::TOsd(App::kQuickChats[0]), osGetTime());
            Toast("Saludo enviado");
            break;
        case 9003: s_sec = SEC_CHAT; s_item = 0; s_scroll = 0; break;
        case 9005: s_sec = SEC_COMBATE; s_item = 0; s_scroll = 0; s_showPss = true; break;
        case 9006: s_sec = SEC_PERSONAJE; s_item = 0; s_scroll = 0; break;
        case 3000: {
            std::string t;
            TypeText("Tu mensaje (máx. 19):", 19, t);
            if (!t.empty()) {
                char clean[20]; App::AsciiFold(t.c_str(), clean, sizeof(clean));
                UiSendSimple(MMO_PKT_CHAT, 255, 0, clean);
                std::string me = Cfg::Name() + ": " + clean;
                App::PushChat(me.c_str(), 0xFFFFFF);
                App::SetMyBubble(clean, osGetTime());
                Toast("Mensaje enviado");
            }
            break;
        }
        case 3010: s_whisperPick = true; s_item = 0; s_scroll = 0; break;
        case 3299: s_whisperPick = false; s_item = 0; s_scroll = 0; break;
        default:
            if (id >= 3001 && id <= 3008) {
                UiSendSimple(MMO_PKT_CHAT, (u8)(id - 3000), 0, nullptr);
                std::string me = Cfg::Name() + ": " + Lang::TOsd(App::kQuickChats[id - 3001]);
                App::PushChat(me.c_str(), 0xFFFFFF);
                App::SetMyBubble(Lang::TOsd(App::kQuickChats[id - 3001]), osGetTime());
                Toast("Mensaje enviado");
            } else if (id >= 3101 && id < 3101 + Emotes::COUNT) {
                UiSendSimple(MMO_PKT_EMOTE, (u8)(id - 3100), 0, nullptr);
                std::string me = Cfg::Name() + ": [" + Lang::TOsd(Emotes::kNames[id - 3101]) + "]";
                App::PushChat(me.c_str(), 0xFFFFFF);
                char em[3] = { '\x01', (char)('A' + (id - 3101)), 0 };
                App::SetMyBubble(em, osGetTime());
                Toast("Emote enviado");
            } else if (id > 3200 && id < 3299) {
                const MMO::RemoteView *rv = FindView(id - 3200);
                char nm[24] = "?"; if (rv) App::AsciiFold(rv->name, nm, sizeof(nm));
                char title[64]; snprintf(title, sizeof(title), TR("Susurro para %s:"), nm);
                std::string t;
                TypeText(title, 19, t);
                if (!t.empty()) {
                    char clean[20]; App::AsciiFold(t.c_str(), clean, sizeof(clean));
                    UiSendSimple(MMO_PKT_CHAT, 255, (u8)(id - 3200), clean);
                    char line[64]; snprintf(line, sizeof(line), "(a %s) %s", nm, clean);
                    App::PushChat(line, 0xFF90E0);
                    Toast("Susurro enviado");
                }
                s_whisperPick = false; s_item = 0; s_scroll = 0;
            } else if (id >= 1500 && id < 1500 + MAX_MMO_PLAYERS + 1) {
                s_item = id - 1500;                     // la ficha se ve abajo
            } else if (id >= 8101 && id <= 8106) {
                s_helpTopic = id - 8101;
            }
            break;
        case 4001: case 4002: {
            App::Invite inv = App::GetInvite();
            if (inv.pending) {
                UiSendSimple(MMO_PKT_EVENT_RESP, inv.from, id == 4001 ? 1 : 2, nullptr);
                App::ClearInvite();
                if (id == 4001) { s_showPss = true; Toast("Aceptada: sigan los pasos del PSS"); }
                else Toast("Invitación rechazada");
            }
            break;
        }
        case 4010: s_sec = SEC_SALA; s_subPlayer = 0; s_item = 0; break;
        case 4020: {
            App::Outgoing og = App::GetOutgoing();
            if (og.pending) { UiSendSimple(MMO_PKT_EVENT_END, og.to, 0, nullptr); App::ClearOutgoing(); Toast("Invitación cancelada"); }
            break;
        }
        case 4030: s_showPss = !s_showPss; break;
        case 5002: Cfg::colorVariant = (Cfg::colorVariant + (dir < 0 ? Cfg::NUM_COLORS - 1 : 1)) % Cfg::NUM_COLORS; Cfg::Save(); break;
        case 5003: Cfg::tagColor = (Cfg::tagColor + (dir < 0 ? Cfg::NUM_TAGS - 1 : 1)) % Cfg::NUM_TAGS; Cfg::Save(); break;
        case 5005: Cfg::accColor = (Cfg::accColor + (dir < 0 ? Cfg::NUM_ACC - 1 : 1)) % Cfg::NUM_ACC; Cfg::Save(); break;
        case 5004: Cfg::showMySkin = !Cfg::showMySkin; Cfg::Save(); break;
        case 6001: Cfg::hudMode = (Cfg::hudMode + (dir < 0 ? 2 : 1)) % 3; Cfg::Save(); break;
        case 6003: Cfg::followers = (Cfg::followers + (dir < 0 ? 2 : 1)) % 3; Cfg::Save(); break;
        case 6004: Cfg::perfMode = (Cfg::perfMode + (dir < 0 ? 2 : 1)) % 3; Cfg::Save(); break;
        case 6002: Cfg::showNames = !Cfg::showNames; Cfg::Save(); break;
        case 6005: CycleLang(dir); Toast("Reinicia el juego para cambiar también sus textos"); break;

        case 7001: {
            std::string t;
            TypeText("Tu nombre (máx. 13):", 13, t);
            if (!t.empty()) {
                char clean[14], old[16];
                Proto::CopyText(old, sizeof(old), Cfg::Name().c_str());
                App::AsciiFold(t.c_str(), clean, sizeof(clean));
                Cfg::SetName(clean);
                if (!MMO::SameRankName(old, clean)) MMO::g_players.BookForget(old);   // v16.3: sin tu nombre viejo repetido
                Toast("Nombre actualizado");
            }
            break;
        }
        case 7002: {
            std::string t;
            TypeText("Código de sala (letras/números):", 12, t);
            char clean[16]; size_t n = 0;
            for (char c : t) if (((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) && n < 12) clean[n++] = c;
            clean[n] = 0;
            if (n) { Cfg::SetRoom(clean); App::RequestReconnect(); Toast("Cambiando de sala..."); }
            else if (!t.empty()) Toast("Usa solo letras y números");
            break;
        }
        case 7003: {
            if (Cfg::HasManualIp()) { Cfg::SetManualIp(""); App::RequestReconnect(); Toast("Servidor automático"); break; }
            std::string t;
            TypeText("IP del servidor:", 15, t);
            if (!t.empty()) { Cfg::SetManualIp(t); App::RequestReconnect(); Toast("Conectando al nuevo servidor..."); }
            break;
        }
        case 7004: App::RequestReconnect(); Toast("Reconectando..."); break;

    }
}

// ---------------------------------------------------------------------------------------------
// Vista previa 3D del personaje (motor de avatares, cámara frontal fija y giro lento)
static u32 s_openCount = 0;                                 // veces que se abrió el menú (para el registro)
static void DrawPreview(const Screen &s, int cx, int feetY, int heightPx, u64 now) {
    if (App::GetAssetState() != App::ASSETS_OK) return;
    R3D::Target t;
    if (!Overlay::MakeTarget(s, t)) return;
    u16 mid = App::LocalModelId();
    const Hero::Model *M = Hero::Get(mid == MMO_MODEL_MAY ? Hero::MODEL_MAY : Hero::MODEL_BRENDAN);
    if (!M) return;
    // clip = vp * (X, altura, Y, 1); el modelo en el origen mira al sur (+Y); la cámara está en Y = 200
    const float W0 = 200.f, H = M->height > 1.f ? M->height : 33.5f, hc = H * 0.5f;
    float c = (float)heightPx * W0 / (120.f * H);
    float a = c * 120.f / 200.f;                       // misma escala en X que en Y (en píxeles)
    float midY = (float)feetY - heightPx * 0.5f;
    float by = (120.f - midY) / 120.f, bx = ((float)cx - 200.f) / 200.f;   // el motor proyecta con centro x=200
    Mat44 vp = {{ a, 0, -bx, W0 * bx,
                  0, c, -by, -c * hc + W0 * by,
                  0, 0, 0, 0,
                  0, 0, -1, W0 }};
    R3D::Instance in;
    memset(&in, 0, sizeof(in));
    in.model = M;
    in.pos = V3(0, 0, 0);
    in.yaw = fmodf((float)(now % 100000) * 0.0011f, 6.2831853f);
    in.anim = Hero::ANIM_WALK;
    in.frame = (float)((now / 33) % (u64)(M->animFrames[Hero::ANIM_WALK] > 0 ? M->animFrames[Hero::ANIM_WALK] : 1));
    in.prevAnim = -1; in.blend = 1.f;
    in.blink = (now % 4000) < 120;
    in.id = R3D::MAX_SLOTS;                            // ranura propia (los jugadores usan 1..12)
    in.variant = (u8)(Cfg::colorVariant < SkinColors::COUNT ? Cfg::colorVariant : 0);
    in.acc = (u8)Cfg::accColor;
    R3D::DrawInfo info;
    int noMem = R3D::g_stats.noMemory + R3D::g_stats.noCache;
    R3D::DrawAll(t, &in, 1, vp, &info);
    static u32 loggedOpen = 0;                          // una línea por cada vez que se abre el menú
    if (loggedOpen != s_openCount) {
        loggedOpen = s_openCount;
        Log::Write("menu: vista previa del personaje %s%s", info.drawn ? "dibujada" : "NO dibujada",
                   R3D::g_stats.noMemory + R3D::g_stats.noCache != noMem ? " (sin memoria)" : "");
    }
}

// ---------------------------------------------------------------------------------------------
// Animación y estado visual
static UiGfx::Spring s_openT;            // aparición del menú (0 -> 1)
static UiGfx::Spring s_pageT;            // cambio de sección o subpágina (0 -> 1)
static UiGfx::Spring s_sideY, s_listY, s_tabX, s_thumbY, s_toastT;
static UiGfx::Spring s_knob[2];          // interruptores: 0 Verme así, 1 Nombres
static UiGfx::Spring s_segX[3];          // selectores: 0 Recuadro, 1 Acompañante, 2 Rendimiento
static int   s_pageDir = 1;
static float s_fade = 1.f;               // cuánto se ve lo que se está dibujando (0..1)
static int   s_ox = 0, s_oy = 0;         // desplazamiento del contenido mientras entra
static u64   s_now = 0;
static bool  s_prevOn = false;           // vista previa 3D en la pantalla de abajo
static int   s_prevCx = 0, s_prevFeet = 0, s_prevH = 0;

static inline Rgb Tc(Rgb c) { return UiGfx::Mix(C_GLASSM, c, s_fade); }   // el texto "aparece" con su panel
static inline int Al(int a) { return (int)(a * s_fade + 0.5f); }          // opacidad de las formas
static inline int X(int x) { return x + s_ox; }
static inline int Y(int y) { return y + s_oy; }

// Zonas táctiles: se registran AL DIBUJAR, así lo que se toca es exactamente lo que se ve
struct Hit { s16 x, y, w, h; int code; };
static Hit  s_hit[64];
static int  s_nh = 0;
static int  s_pressCode = -1;            // lo que el dedo pulsa ahora (-1 = nada)
static bool s_pressInside = false;
static Hit  s_pressRect;
static void AddHit(int x, int y, int w, int h, int code) {
    if (s_nh >= 64) return;
    Hit &e = s_hit[s_nh++];
    e.x = (s16)x; e.y = (s16)y; e.w = (s16)w; e.h = (s16)h; e.code = code;
}
static bool IsPressed(int code) { return s_pressCode == code && s_pressInside; }
static bool InHit(const Hit &h, int x, int y, int slop) { return x >= h.x - slop && x < h.x + h.w + slop && y >= h.y - slop && y < h.y + h.h + slop; }

// ---------------------------------------------------------------------------------------------
// Piezas de la interfaz
// Cristal: sombra difusa, cuerpo translúcido con degradado, borde fino y un brillo arriba (la luz en el material)
static void Glass(int x, int y, int w, int h, int r) {
    UiGfx::StaticBegin();                                         // fijo: se guarda ya compuesto (UiGfx)
    UiGfx::Shadow(x, y, w, h, r, 9, Al(120));
    UiGfx::RRectV(x, y, w, h, r, C_GLASS0, C_GLASS1, Al(214));
    UiGfx::Stroke(x, y, w, h, r, C_EDGE, Al(70));
    UiGfx::FadeLine(x + r, y + 1, w - 2 * r, C_WHITE, Al(64));
    UiGfx::StaticEnd();
}
// Tarjeta interior (más oscura); pulsada = azul
static void Card(int x, int y, int w, int h, int r, bool pressed = false) {
    if (pressed) {
        UiGfx::RRectV(x, y, w, h, r, C_ACC0, C_ACC1, Al(240));
        UiGfx::Stroke(x, y, w, h, r, C_ACCE, Al(110));
        return;
    }
    UiGfx::RRect(x, y, w, h, r, C_NAVY, Al(112));
    UiGfx::Stroke(x, y, w, h, r, C_EDGE, Al(38));
}
// Lo elegido: azul con brillo si tiene el foco; si el foco está en otra parte, una marca discreta
static void Pill(int x, int y, int w, int h, int r, bool focused) {
    if (focused) {
        UiGfx::Shadow(x, y, w, h, r, 4, Al(90));
        UiGfx::RRectV(x, y, w, h, r, C_ACC0, C_ACC1, Al(255));
        UiGfx::Stroke(x, y, w, h, r, C_ACCE, Al(120));
        UiGfx::FadeLine(x + r, y + 1, w - 2 * r, C_WHITE, Al(96));
    } else {
        UiGfx::RRect(x, y, w, h, r, RGB(74, 100, 166), Al(118));
        UiGfx::Stroke(x, y, w, h, r, C_EDGE, Al(60));
    }
}
// Botón en cápsula. style 0 normal, 1 rojo (cerrar), 2 principal (azul)
static void Button(int x, int y, int w, int h, const char *label, int code, bool enabled = true, int style = 0) {
    bool pr = enabled && IsPressed(code);
    int r = h / 2;
    Rgb t0 = RGB(56, 76, 132), t1 = RGB(36, 52, 100), edge = C_EDGE;
    if (style == 1) { t0 = RGB(150, 46, 62); t1 = RGB(104, 28, 44); edge = RGB(255, 140, 150); }
    if (style == 2) { t0 = RGB(58, 116, 228); t1 = RGB(34, 80, 196); edge = C_ACCE; }
    if (pr) { t0 = style == 1 ? RGB(210, 76, 92) : RGB(96, 156, 255); t1 = style == 1 ? RGB(160, 44, 60) : RGB(56, 110, 232); }
    UiGfx::Shadow(x, y, w, h, r, 4, Al(enabled ? 80 : 30));
    UiGfx::RRectV(x, y, w, h, r, t0, t1, Al(enabled ? 240 : 110));
    UiGfx::Stroke(x, y, w, h, r, edge, Al(enabled ? 110 : 40));
    UiGfx::FadeLine(x + r, y + 1, w - 2 * r, C_WHITE, Al(pr ? 110 : 60));
    UiGfx::TextCenter(TR(label), x + w / 2, y + (h - 16) / 2, Tc(enabled ? C_WHITE : C_DIM));
    if (enabled) AddHit(x, y, w, h, code);
}
// Interruptor (encendido = verde), el botón se desliza con un muelle
static void Switch(int x, int y, const UiGfx::Spring &knob, bool pressed) {
    float t = knob.x < 0.f ? 0.f : knob.x > 1.f ? 1.f : knob.x;
    UiGfx::RRect(x, y, 48, 26, 13, UiGfx::Mix(RGB(58, 66, 98), RGB(52, 196, 120), t), Al(255));
    UiGfx::Stroke(x, y, 48, 26, 13, C_WHITE, Al(36));
    float kx = x + 13.f + t * 22.f;
    UiGfx::Shadow((int)kx - 10, y + 3, 20, 20, 10, 3, Al(100));
    UiGfx::Disc(kx, y + 13.f, pressed ? 11.5f : 10.5f, C_WHITE, Al(255));
}
// Selector de 3 opciones (como el de iOS): la marca azul se desliza a la opción elegida
static void Segmented(int x, int y, int w, int h, const char *const *labels, int sel, const UiGfx::Spring &sp, int codeBase) {
    UiGfx::RRect(x, y, w, h, h / 2, C_NAVY, Al(130));
    UiGfx::Stroke(x, y, w, h, h / 2, C_EDGE, Al(46));
    // v17: cada opción tan ancha como su texto (con el sobrante repartido): en otros idiomas los textos miden distinto
    const char *lb[3]; int tw[3], sum = 0;
    for (int k = 0; k < 3; k++) { lb[k] = TR(labels[k]); tw[k] = UiGfx::TextW(lb[k]); sum += tw[k]; }
    int seg[4]; seg[0] = x;
    int extra = w - sum;
    for (int k = 0; k < 3; k++) {
        int sw = extra >= 6 ? tw[k] + extra / 3 : w / 3;           // si no caben, a partes iguales (y se recortan)
        seg[k + 1] = k == 2 ? x + w : seg[k] + sw;
    }
    float f = sp.x < 0.f ? 0.f : sp.x > 2.f ? 2.f : sp.x;
    int k0 = (int)f; if (k0 > 1) k0 = 1;
    float t = f - k0;
    float pl = seg[k0] + (seg[k0 + 1] - seg[k0]) * t, pr = seg[k0 + 1] + (seg[k0 + 2] - seg[k0 + 1]) * t;
    int px = (int)(pl + 2.5f), pw = (int)(pr - pl + 0.5f) - 4;
    UiGfx::Shadow(px, y + 2, pw, h - 4, (h - 4) / 2, 3, Al(90));
    UiGfx::RRectV(px, y + 2, pw, h - 4, (h - 4) / 2, C_ACC0, C_ACC1, Al(255));
    UiGfx::Stroke(px, y + 2, pw, h - 4, (h - 4) / 2, C_ACCE, Al(100));
    for (int k = 0; k < 3; k++) {
        int sx = seg[k], sw = seg[k + 1] - seg[k];
        if (k != sel && IsPressed(codeBase + k)) UiGfx::RRect(sx + 2, y + 2, sw - 4, h - 4, (h - 4) / 2, C_WHITE, Al(40));
        char fit[64];
        UiGfx::Fit(lb[k], sw - 2, fit, sizeof(fit));
        UiGfx::TextCenter(fit, sx + sw / 2, y + (h - 16) / 2, Tc(k == sel ? C_WHITE : C_SUB));
        AddHit(sx, y - 3, sw, h + 6, codeBase + k);
    }
}
// Fila "etiqueta ........ valor" con una línea suave debajo
static void InfoRow(int y, const char *label, const char *value, Rgb vc, int rowH = 20) {
    label = TR(label); value = TR(value);
    UiGfx::Text(label, X(18), y, Tc(C_SUB));
    char v[64];
    UiGfx::Fit(value, 284 - UiGfx::TextW(label) - 12, v, sizeof(v));
    UiGfx::TextRight(v, X(302), y, Tc(vc));
    UiGfx::FadeLine(X(16), y + rowH - 2, 288, C_EDGE, Al(70));
}

static void Rect(int x, int y, int w, int h, Rgb c) { UiGfx::RRect(x, y, w, h, 0, c, Al(255)); }
// Flecha triangular (la fuente de la consola no tiene ◄ ►): punta en (cx ± 3, cy)
static void Arrow(int cx, int cy, bool right, Rgb c) {
    for (int k = 0; k < 5; k++) {
        int x = right ? cx + 2 - k : cx - 2 + k;
        Rect(x, cy - k, 1, 2 * k + 1, c);
    }
}

// Iconos de sección (12x12)
static void Icon(int sec, int x, int y, Rgb c) {
    switch (sec) {
        case SEC_INICIO:
            for (int i = 0; i < 6; i++) Rect(x + 5 - i, y + i, 2 + 2 * i, 1, c);
            Rect(x + 1, y + 6, 10, 6, c);
            UiGfx::RRect(x + 5, y + 8, 2, 4, 0, C_NAVY, Al(210));
            break;
        case SEC_RANKING:
            Rect(x + 2, y, 8, 5, c); Rect(x, y + 1, 2, 3, c); Rect(x + 10, y + 1, 2, 3, c);
            Rect(x + 3, y + 5, 6, 2, c); Rect(x + 5, y + 7, 2, 3, c); Rect(x + 2, y + 10, 8, 2, c);
            break;
        case SEC_SALA:
            UiGfx::Disc(x + 3.5f, y + 3.f, 2.5f, c, Al(255)); UiGfx::Disc(x + 8.5f, y + 3.f, 2.5f, c, Al(255));
            UiGfx::RRect(x, y + 7, 6, 5, 2, c, Al(255)); UiGfx::RRect(x + 6, y + 7, 6, 5, 2, c, Al(255));
            break;
        case SEC_CHAT:
            UiGfx::RRect(x, y + 1, 12, 8, 3, c, Al(255)); Rect(x + 2, y + 8, 3, 2, c); Rect(x + 2, y + 10, 1, 1, c);
            break;
        case SEC_COMBATE:
            for (int i = 0; i < 11; i++) { Rect(x + i, y + i, 2, 1, c); Rect(x + 10 - i, y + i, 2, 1, c); }
            break;
        case SEC_PERSONAJE:
            UiGfx::Disc(x + 6.f, y + 3.5f, 3.f, c, Al(255)); UiGfx::RRect(x + 2, y + 7, 8, 5, 3, c, Al(255));
            break;
        case SEC_PANTALLA:
            UiGfx::Stroke(x, y + 1, 12, 8, 2, c, Al(255)); UiGfx::Stroke(x + 1, y + 2, 10, 6, 1, c, Al(255));
            Rect(x + 5, y + 9, 2, 2, c); Rect(x + 3, y + 11, 6, 1, c);
            break;
        case SEC_CONEXION:
            Rect(x + 1, y + 9, 2, 3, c); Rect(x + 4, y + 6, 2, 6, c); Rect(x + 7, y + 3, 2, 9, c); Rect(x + 10, y, 2, 12, c);
            break;
        default:
            UiGfx::Disc(x + 6.f, y + 6.f, 6.f, c, Al(255));
            UiGfx::Small("?", x + 3, y + 1, C_NAVY);
            break;
    }
}

// Poké Ball pequeña (logo)
static void Pokeball(float cx, float cy, float r) {
    UiGfx::Disc(cx, cy, r + 1.f, RGB(20, 20, 30), Al(255));
    UiGfx::Disc(cx, cy, r, RGB(245, 245, 250), Al(255));
    UiGfx::DiscTop(cx, cy, r, RGB(232, 60, 64), Al(255));
    UiGfx::RRect((int)(cx - r), (int)cy, (int)(2 * r + 1), 1, 0, RGB(20, 20, 30), Al(255));
    UiGfx::Disc(cx, cy, 2.5f, RGB(20, 20, 30), Al(255));
    UiGfx::Disc(cx, cy, 1.3f, RGB(245, 245, 250), Al(255));
}

// Emotes (pixel art 12x12): los cuadros seguidos del mismo color se juntan en una sola forma
static int  s_runX, s_runY, s_runW, s_runSz;
static Rgb  s_runC;
static bool s_run = false;
static void FlushRun() { if (s_run) UiGfx::RRect(s_runX, s_runY, s_runW, s_runSz, 0, s_runC, Al(255)); s_run = false; }
static void EmoteRect(void *, int x, int y, int size, unsigned char r, unsigned char g, unsigned char b) {
    if (s_run && y == s_runY && x == s_runX + s_runW && size == s_runSz && s_runC.r == r && s_runC.g == g && s_runC.b == b) { s_runW += size; return; }
    FlushRun();
    s_run = true; s_runX = x; s_runY = y; s_runW = size; s_runSz = size; s_runC = RGB(r, g, b);
}
static void DrawEmote(int idx, int x, int y, int scale) { Emotes::Draw(idx, x, y, scale, EmoteRect, nullptr); FlushRun(); }


// ---------------------------------------------------------------------------------------------
// Pantalla superior: secciones a la izquierda, opciones a la derecha
static const int SIDE_X = 8, SIDE_Y = 30, SIDE_W = 140, SIDE_H = 184, SIDE_ROW = 20;
static const int CONT_X = 154, CONT_Y = 30, CONT_W = 238, CONT_H = 184;
static const int LIST_Y = 60, LIST_ROWS = 6, ROW_H = 20, HELP_Y = 187, HELP_H = 23;

static void ClampScroll() {
    if (s_item < s_scroll) s_scroll = s_item;
    if (s_item >= s_scroll + LIST_ROWS) s_scroll = s_item - LIST_ROWS + 1;
    if (s_scroll > s_optCount - LIST_ROWS) s_scroll = s_optCount - LIST_ROWS;
    if (s_scroll < 0) s_scroll = 0;
}

static void DrawList(int oy, int dy) {
    bool bar = s_optCount > LIST_ROWS;
    int right = CONT_X + CONT_W - (bar ? 16 : 11);
    UiGfx::PageBegin(false);                                      // v16.2: se desvanece con la página (UiGfx)
    if (s_inContent && s_optCount)
        Pill(CONT_X + 5, (int)(s_listY.x + 0.5f) + oy, CONT_W - (bar ? 17 : 10), ROW_H - 2, 8, true);
    UiGfx::PageBegin(true);                                       // las filas además entran desplazadas
    for (int r = 0; r < LIST_ROWS && s_scroll + r < s_optCount; r++) {
        int i = s_scroll + r;
        const Option &o = s_opt[i];
        int ry = LIST_Y + r * ROW_H + dy;
        bool sel = s_inContent && i == s_item;
        int tx = CONT_X + 13;
        if (o.color) {
            const u8 *pc = kPlayerColors[(o.color - 1) % 12];
            UiGfx::Disc(CONT_X + 17.5f, ry + 9.f, 4.f, RGB(pc[0], pc[1], pc[2]), Al(255));
            tx = CONT_X + 27;
        }
        int valW = 0;
        if (o.value[0]) {
            char val[56];
            if (IsCycle(o.id) && sel) snprintf(val, sizeof(val), "<  %s  >", o.value);   // (la fuente de la consola no tiene flechas)
            else snprintf(val, sizeof(val), "%s", o.value);
            valW = UiGfx::TextW(val) + 10;
            UiGfx::TextRight(val, right, ry + 1, Tc(sel ? C_GOLD : C_SUB));
        }
        bool emote = o.id >= 3101 && o.id < 3101 + Emotes::COUNT;
        if (emote) { DrawEmote(o.id - 3101, right - 13, ry + 3, 1); valW = 20; }
        char lab[56];
        UiGfx::Fit(o.label, right - valW - tx, lab, sizeof(lab));
        UiGfx::Text(lab, tx, ry + 1, Tc(!o.enabled ? (sel ? RGB(176, 196, 236) : C_DIM) : sel ? C_WHITE : RGB(214, 222, 245)));
    }
    UiGfx::PageBegin(false);
    if (bar) {                                                    // barra de desplazamiento fina (en vez de flechas)
        int tx = CONT_X + CONT_W - 10, ty = LIST_Y + 2 + oy, th = LIST_ROWS * ROW_H - 4;
        UiGfx::RRect(tx, ty, 3, th, 1, C_WHITE, Al(26));
        int hh = th * LIST_ROWS / s_optCount;
        if (hh < 12) hh = 12;
        UiGfx::RRect(tx, (int)(s_thumbY.x + 0.5f) + oy, 3, hh, 1, C_WHITE, Al(150));
    }
    UiGfx::PageEnd();
}

static float ThumbTarget() {
    int th = LIST_ROWS * ROW_H - 4, hh = s_optCount > 0 ? th * LIST_ROWS / s_optCount : th;
    if (hh < 12) hh = 12;
    int span = s_optCount - LIST_ROWS;
    return (float)(LIST_Y + 2) + (span > 0 ? (float)(th - hh) * s_scroll / span : 0.f);
}

static void DrawTop(u64 now) {
    UiGfx::Begin(true);
    // v16.2: al aparecer, todo se emite en su estado final y UiGfx hace la aparición (opacidad + subir 10 px) con la
    // pantalla compuesta una sola vez (antes: las dos pantallas enteras en cada cuadro de la aparición)
    float openA = s_openT.x < 0.f ? 0.f : s_openT.x > 1.f ? 1.f : s_openT.x;
    UiGfx::OpenParams(openA, (int)((1.f - openA) * 10.f + 0.5f), C_GLASSM);
    float open = 1.f;
    s_fade = open;
    int oy = 0;                                                   // (los paneles suben un poco al aparecer: UiGfx)
    UiGfx::ChromeRect(CONT_X, CONT_Y + oy, CONT_W, CONT_H);      // el panel de opciones se guarda ya compuesto

    // cabecera flotante
    Pokeball(17.f, 15.f, 7.f);
    UiGfx::Text("ORAS MMO", 29, 6, Tc(C_GOLD));
    UiGfx::Small("v" MMO_VERSION_STR, 29 + UiGfx::TextW("ORAS MMO") + 6, 12, Tc(C_DIM));
    bool conn = App::IsConnected();
    char pill[40];
    if (conn) snprintf(pill, sizeof(pill), TR("En línea: %d"), MMO::g_players.ActiveCount() + 1);
    else snprintf(pill, sizeof(pill), "%s", TR("Sin conexión"));
    int pw = UiGfx::TextW(pill) + 32, px = 392 - pw;
    Glass(px, 4, pw, 21, 10);
    UiGfx::Disc(px + 13.f, 14.5f, 7.f, conn ? C_GREEN : C_ORANGE, Al(60));
    UiGfx::Disc(px + 13.f, 14.5f, 4.f, conn ? C_GREEN : C_ORANGE, Al(255));
    UiGfx::Text(pill, px + 23, 6, Tc(conn ? C_TEXT : C_ORANGE));

    // barra lateral: la selección se desliza (muelle) de una sección a otra
    Glass(SIDE_X, SIDE_Y + oy, SIDE_W, SIDE_H, 12);
    Pill(SIDE_X + 4, (int)(s_sideY.x + 0.5f) + oy, SIDE_W - 8, SIDE_ROW - 2, 8, !s_inContent);
    bool invPending = App::GetInvite().pending;
    for (int i = 0; i < SEC_COUNT; i++) {
        int y = SIDE_Y + 2 + i * SIDE_ROW + oy;
        bool cur = i == s_sec;
        Icon(i, SIDE_X + 11, y + 4, cur ? C_GOLD : C_SUB);
        UiGfx::Text(TR(kSecTitle[i]), SIDE_X + 29, y + 1, Tc(cur ? C_WHITE : RGB(198, 208, 236)));
        char badge[8] = "";
        bool alert = false;
        if (i == SEC_SALA && conn) snprintf(badge, sizeof(badge), "%d", MMO::g_players.ActiveCount());
        if ((i == SEC_COMBATE || i == SEC_INICIO) && invPending) { snprintf(badge, sizeof(badge), "!"); alert = true; }
        if (badge[0]) {
            int bw = UiGfx::TextW(badge) + 12;
            if (bw < 18) bw = 18;
            int bx = SIDE_X + SIDE_W - bw - 8;
            UiGfx::RRect(bx, y + 2, bw, 15, 7, alert ? C_RED : RGB(60, 80, 136), Al(alert ? 255 : 230));
            UiGfx::TextCenter(badge, bx + bw / 2, y + 1, Tc(C_WHITE));
        }
    }

    // panel de opciones: el contenido entra desde el lado hacia el que navegas
    Glass(CONT_X, CONT_Y + oy, CONT_W, CONT_H, 12);
    float pg = s_pageT.x < 0.f ? 0.f : s_pageT.x > 1.f ? 1.f : s_pageT.x;
    // v16.2: la página se emite en su estado final y UiGfx aplica la entrada (opacidad pg + desplazamiento)
    UiGfx::PageParams(pg, 0, (int)((1.f - pg) * 8.f * s_pageDir), C_GLASSM);
    s_fade = open;
    int dy = oy;
    UiGfx::PageBegin(true);
    char title[48];
    if (s_sec == SEC_SALA && s_subPlayer) {
        const MMO::RemoteView *rv = FindView(s_subPlayer);
        char nm[24] = "?";
        if (rv) App::AsciiFold(rv->name, nm, sizeof(nm));
        UiGfx::Fit(nm, CONT_W - 24, title, sizeof(title));
        UiGfx::Text(title, CONT_X + 12, CONT_Y + 6 + dy, Tc(C_GOLD));
    } else {
        UiGfx::Text(TR(kSecTitle[s_sec]), CONT_X + 12, CONT_Y + 6 + dy, Tc(C_WHITE));
        if (s_sec == SEC_CHAT && s_whisperPick) UiGfx::TextRight(TR("Susurro"), CONT_X + CONT_W - 12, CONT_Y + 6 + dy, Tc(C_SUB));
    }
    UiGfx::PageEnd();
    s_fade = open;
    UiGfx::FadeLine(CONT_X + 10, CONT_Y + 26 + oy, CONT_W - 20, C_EDGE, Al(150));
    DrawList(oy, dy);
    // qué hace la opción elegida (o la sección)
    s_fade = open;
    {
        int hy = HELP_Y + oy;
        UiGfx::RRect(CONT_X + 6, hy, CONT_W - 12, HELP_H, 9, C_NAVY, Al(120));
        UiGfx::Stroke(CONT_X + 6, hy, CONT_W - 12, HELP_H, 9, C_EDGE, Al(34));
        UiGfx::Disc(CONT_X + 17.f, hy + 11.5f, 5.5f, C_GOLD, Al(255));
        UiGfx::Small("i", CONT_X + 15, hy + 7, C_NAVY);
        const char *help = (s_inContent && s_item < s_optCount && s_opt[s_item].help[0]) ? s_opt[s_item].help : TR(kSecHelp[s_sec]);
        UiGfx::PageBegin(false);                                  // el texto de ayuda se desvanece con la página
        if (Lang::Cjk()) {
            // v17: japonés/coreano: una línea con la letra del sistema (la pequeña solo tiene letras latinas)
            char f[112];
            UiGfx::Fit(help, CONT_W - 40, f, sizeof(f));
            UiGfx::Text(f, CONT_X + 28, hy + 1, Tc(C_TEXT));
        } else {
            char a[112];
            App::AsciiFold(help, a, sizeof(a));
            char l1[40], l2[40];
            int n = (int)strlen(a), cut = n;
            if (n > 34) { cut = 34; while (cut > 20 && a[cut] != ' ') cut--; }
            snprintf(l1, sizeof(l1), "%.*s", cut, a);
            snprintf(l2, sizeof(l2), "%.34s", n > cut ? a + cut + (a[cut] == ' ' ? 1 : 0) : "");
            UiGfx::Small(l1, CONT_X + 28, hy + 2, Tc(C_TEXT));
            UiGfx::Small(l2, CONT_X + 28, hy + 12, Tc(C_SUB));
        }
        UiGfx::PageEnd();
    }

    // pie: controles, o un aviso que sube desde abajo
    s_fade = open;
    float tt = s_toastT.x < 0.f ? 0.f : s_toastT.x > 1.f ? 1.f : s_toastT.x;
    if (tt < 0.98f) {
        Rgb hint = UiGfx::Mix(RGB(14, 22, 46), C_SUB, (1.f - tt) * open);
        bool cyc = s_inContent && s_item < s_optCount && IsCycle(s_opt[s_item].id);
        if (!s_inContent) UiGfx::Text(TR("\xEE\x80\x86 Elegir sección   \xEE\x80\x80 Entrar   \xEE\x80\x81 Cerrar menú"), 12, 219, hint);
        else if (cyc) UiGfx::Text(TR("\xEE\x80\x86 Mover   \xEE\x80\x80 Cambiar   \xEE\x80\x81 Volver   \xEE\x80\x84\xEE\x80\x85 Sección"), 12, 219, hint);
        else UiGfx::Text(TR("\xEE\x80\x86 Mover   \xEE\x80\x80 Aceptar   \xEE\x80\x81 Volver   \xEE\x80\x84\xEE\x80\x85 Sección"), 12, 219, hint);
    }
    if (tt > 0.02f) {
        char msg[64];
        UiGfx::Fit(s_toast, 330, msg, sizeof(msg));
        int w = UiGfx::TextW(msg) + 32, x = 200 - w / 2, y = 216 + (int)((1.f - tt) * 26.f);
        UiGfx::Shadow(x, y, w, 21, 10, 6, (int)(110 * tt));
        UiGfx::RRectV(x, y, w, 21, 10, C_ACC0, C_ACC1, (int)(250 * tt));
        UiGfx::Stroke(x, y, w, 21, 10, C_ACCE, (int)(120 * tt));
        UiGfx::Text(msg, x + 16, y + 2, UiGfx::Mix(C_ACC1, C_WHITE, tt));
    }
    (void)now;
    UiGfx::End(OSD::GetTopScreen());
}

// ---------------------------------------------------------------------------------------------
// Pantalla inferior (táctil / ratón): pestañas, la INFORMACIÓN de la sección y botones de control
static const int TAB_X = 7, TAB_W = 34, CARD_Y = 38, CARD_H = 164, FOOT_Y = 207, FOOT_H = 27;
static int s_rowIds[8];                 // Jugadores: id del jugador de cada fila táctil
static int s_rowCount = 0;

struct FootBtn { int x, w, code; const char *label; int style; };
static const FootBtn kFoot[5] = {
    { 6, 38, 500, "\xE2\x96\xB2", 0 }, { 48, 38, 501, "\xE2\x96\xBC", 0 }, { 90, 82, 502, "\xEE\x80\x80 Aceptar", 2 },
    { 176, 70, 503, "\xEE\x80\x81 Volver", 0 }, { 250, 64, 504, "Cerrar", 1 },
};

static void AfterBottom(const Screen &s) { if (s_prevOn) DrawPreview(s, s_prevCx, s_prevFeet, s_prevH, s_now); }

static void DrawBottom(u64 now) {
    UiGfx::Begin(false);
    s_nh = 0;
    s_rowCount = 0;
    s_prevOn = false;
    float openA = s_openT.x < 0.f ? 0.f : s_openT.x > 1.f ? 1.f : s_openT.x;
    UiGfx::OpenParams(openA, (int)((1.f - openA) * 10.f + 0.5f), C_GLASSM);   // v16.2: ver DrawTop
    float open = 1.f;
    s_fade = open;
    int oy = 0;
    UiGfx::ChromeRect(6, CARD_Y + oy, 308, CARD_H);              // la tarjeta de información se guarda ya compuesta

    // pestañas: la marca azul se desliza a la sección elegida
    Glass(6, 4 + oy, 308, 28, 14);
    Pill((int)(s_tabX.x + 0.5f) - 16, 7 + oy, 32, 22, 11, true);
    for (int i = 0; i < SEC_COUNT; i++) {
        int cx = TAB_X + i * TAB_W + TAB_W / 2;
        if (i != s_sec && IsPressed(100 + i)) UiGfx::RRect(cx - 16, 7 + oy, 32, 22, 11, C_WHITE, Al(46));
        Icon(i, cx - 6, 12 + oy, i == s_sec ? C_GOLD : C_SUB);
        AddHit(TAB_X + i * TAB_W, 0, TAB_W, 36, 100 + i);
    }

    Glass(6, CARD_Y + oy, 308, CARD_H, 14);
    float pg = s_pageT.x < 0.f ? 0.f : s_pageT.x > 1.f ? 1.f : s_pageT.x;
    // abajo las pestañas van en horizontal: la página entra de lado. v16.2: se emite en su estado final y UiGfx
    // aplica la entrada (opacidad pg + desplazamiento) con la página guardada
    UiGfx::PageParams(pg, (int)((1.f - pg) * 8.f * s_pageDir), 0, C_GLASSM);
    UiGfx::PageBegin(true);
    s_fade = open;
    s_ox = 0;
    s_oy = oy;
    bool conn = App::IsConnected();
    std::string room = Cfg::Room(), name = Cfg::Name();
    char buf[72], fit[72];
    switch (s_sec) {
        case SEC_INICIO: {
            int hx = X(18);
            UiGfx::Text(TR("¡Hola, "), hx, Y(45), Tc(C_TEXT)); hx += UiGfx::TextW(TR("¡Hola, "));
            UiGfx::Fit(name.c_str(), 200, fit, sizeof(fit));
            UiGfx::Text(fit, hx, Y(45), Tc(C_GOLD)); hx += UiGfx::TextW(fit);
            UiGfx::Text("!", hx, Y(45), Tc(C_TEXT));
            Game::LocalState me = Game::Snapshot();
            char online[12];
            snprintf(online, sizeof(online), "%d", conn ? MMO::g_players.ActiveCount() + 1 : 0);
            const char *lab[4] = { "ESTADO", "SALA", "EN LINEA", "ESTAS EN" };
            const char *val[4] = { conn ? "Conectado" : "Conectando...", room.c_str(), online, me.valid ? TownMap::ZoneName(me.zone) : "-" };
            Rgb col[4] = { conn ? C_GREEN : C_ORANGE, C_TEXT, C_TEXT, C_TEXT };
            for (int k = 0; k < 4; k++) {
                int tx = X(14 + (k % 2) * 148), ty = Y(70 + (k / 2) * 50);
                Card(tx, ty, 144, 44, 11);
                UiGfx::Small(TR(lab[k]), tx + 11, ty + 7, Tc(C_DIM));
                UiGfx::Fit(TR(val[k]), 124, fit, sizeof(fit));
                UiGfx::Text(fit, tx + 11, ty + 21, Tc(col[k]));
            }
            App::Invite inv = App::GetInvite();
            if (inv.pending) {
                bool pr = IsPressed(310);
                char nm[24]; App::AsciiFold(inv.name, nm, sizeof(nm));
                snprintf(buf, sizeof(buf), TR(inv.type == 1 ? "%s te invitó a combatir" : "%s te invitó a intercambiar"), nm);
                UiGfx::Shadow(X(14), Y(171), 292, 26, 13, 5, Al(90));
                UiGfx::RRectV(X(14), Y(171), 292, 26, 13, pr ? RGB(214, 80, 96) : RGB(156, 44, 60), pr ? RGB(170, 52, 68) : RGB(112, 28, 44), Al(245));
                UiGfx::Stroke(X(14), Y(171), 292, 26, 13, RGB(255, 150, 160), Al(120));
                UiGfx::Fit(buf, 196, fit, sizeof(fit));
                UiGfx::Text(fit, X(28), Y(176), Tc(C_WHITE));
                UiGfx::TextRight(TR("Responder >"), X(296), Y(176), Tc(C_GOLD));
                AddHit(X(14), Y(171), 292, 26, 310);
            }
            break;
        }
        case SEC_SALA: {
            if (s_subPlayer) {
                const MMO::RemoteView *rv = FindView(s_subPlayer);
                if (!rv) break;
                char nm[24]; App::AsciiFold(rv->name, nm, sizeof(nm));
                const u8 *pc = kPlayerColors[(rv->id - 1) % 12];
                UiGfx::Disc(X(25), Y(53), 6.f, RGB(pc[0], pc[1], pc[2]), Al(255));
                UiGfx::Fit(nm, 250, fit, sizeof(fit));
                UiGfx::Text(fit, X(38), Y(45), Tc(C_GOLD));
                InfoRow(Y(70), "Lugar", TownMap::ZoneName(rv->zone), C_TEXT);
                Game::LocalState me = Game::Snapshot();
                if (me.valid && TownMap::SameSpace(rv->zone, me.zone)) {
                    float d = sqrtf((rv->x - me.x) * (rv->x - me.x) + (rv->y - me.y) * (rv->y - me.y)) / 18.f;
                    snprintf(buf, sizeof(buf), "%d m", (int)(d + 0.5f));
                } else snprintf(buf, sizeof(buf), "%s", TR("en otro lugar"));
                InfoRow(Y(90), "Distancia", buf, C_TEXT);
                InfoRow(Y(110), "Estado", (rv->flags & MMO_FLAG_IN_BATTLE) ? "En combate" : (rv->flags & MMO_FLAG_MENU) ? "En un menú" : "Explorando", C_TEXT);
                InfoRow(Y(130), "Personaje", rv->model == MMO_MODEL_MAY ? "Aura" : "Bruno", C_TEXT);
                // v16.3: sin "Ir con él" (el viaje al lugar del amigo descolocaba el juego: quitado)
                Button(X(14), Y(164), 140, 28, "Combate", 800, conn);
                Button(X(14) + 146, Y(164), 146, 28, "Intercambio", 801, conn);
                break;
            }
            UiGfx::Text(TR("Jugadores"), X(18), Y(44), Tc(C_TEXT));
            snprintf(buf, sizeof(buf), TR("Sala %s"), room.c_str());
            UiGfx::TextRight(buf, X(302), Y(44), Tc(C_SUB));
            Game::LocalState me = Game::Snapshot();
            for (int i = 0; i < MAX_MMO_PLAYERS && s_rowCount < 6; i++) {
                const MMO::RemoteView &rv = s_views[i];
                if (!rv.valid) continue;
                int r = s_rowCount;
                int ry = Y(68 + r * 22);
                Card(X(12), ry, 296, 20, 10, IsPressed(300 + r));
                const u8 *pc = kPlayerColors[(rv.id - 1) % 12];
                UiGfx::Disc(X(25), ry + 10.f, 4.5f, RGB(pc[0], pc[1], pc[2]), Al(255));
                char nm[24]; App::AsciiFold(rv.name, nm, sizeof(nm));
                UiGfx::Fit(nm, 140, fit, sizeof(fit));
                UiGfx::Text(fit, X(36), ry + 1, Tc(C_TEXT));
                if (me.valid && TownMap::SameSpace(rv.zone, me.zone)) {
                    float d = sqrtf((rv.x - me.x) * (rv.x - me.x) + (rv.y - me.y) * (rv.y - me.y)) / 18.f;
                    snprintf(buf, sizeof(buf), "%s  %dm", TR((rv.flags & MMO_FLAG_IN_BATTLE) ? "combate" : (rv.flags & MMO_FLAG_MENU) ? "en menú" : "aquí"), (int)(d + 0.5f));
                } else snprintf(buf, sizeof(buf), "%s", TownMap::ZoneName(rv.zone));
                UiGfx::Fit(buf, 110, fit, sizeof(fit));
                UiGfx::TextRight(fit, X(300), ry + 1, Tc(C_SUB));
                AddHit(X(12), ry, 296, 20, 300 + r);
                s_rowIds[s_rowCount++] = rv.id;
            }
            if (!s_rowCount) {
                UiGfx::TextCenter(TR("Nadie más por ahora"), X(160), Y(104), Tc(C_DIM));
                UiGfx::SmallCenter(TR("Cuando se conecten tus amigos"), X(160), Y(126), Tc(C_DIM));
                UiGfx::SmallCenter(TR("apareceran aqui."), X(160), Y(138), Tc(C_DIM));
            }
            break;
        }
        case SEC_RANKING: {
            BuildRanking();
            int sel = (s_inContent && s_item < s_rankN) ? s_item : -1;
            if (sel >= 0) {
                const RankEntry &e = s_rank[sel];
                snprintf(buf, sizeof(buf), "%d. %s", sel + 1, e.name);
                UiGfx::Fit(buf, 200, fit, sizeof(fit));
                UiGfx::Text(fit, X(18), Y(44), Tc(C_GOLD));
                snprintf(buf, sizeof(buf), TR("%d pts"), e.score);
                UiGfx::TextRight(buf, X(302), Y(44), Tc(C_WHITE));
                if (e.offline) {
                    if (e.days) snprintf(buf, sizeof(buf), TR("Desconectado (hace %u d)"), (unsigned)e.days);
                    else snprintf(buf, sizeof(buf), "%s", TR("Desconectado"));
                } else snprintf(buf, sizeof(buf), "%s", TR(e.me ? "Tú" : "Conectado"));
                UiGfx::Small(buf, X(18), Y(62), Tc(e.offline ? C_DIM : C_GREEN));
                {   // de dónde salen los puntos (medallas > Liga > Pokédex > equipo). v17: solo si cabe al lado del estado
                    int left = UiGfx::SmallW(buf) + 10;
                    Rank::Parts pp = Rank::Score(Stats::ToRank(e.m));
                    snprintf(buf, sizeof(buf), TR("Med %d + Liga %d + Dex %d + Eq %d"), pp.badges, pp.league, pp.dex, pp.team);
                    if (UiGfx::SmallW(buf) <= 284 - left) UiGfx::Small(buf, X(302) - UiGfx::SmallW(buf), Y(62), Tc(C_SUB));
                }
                UiGfx::Small(TR("MEDALLAS"), X(18), Y(80), Tc(C_DIM));
                for (int b = 0; b < 8; b++) {
                    bool got = e.m.okMisc && ((e.m.badges >> b) & 1);
                    UiGfx::Disc(X(112) + b * 24.f, Y(84) + 0.5f, 7.f, got ? C_GOLD : RGB(40, 52, 86), Al(255));
                    if (got) UiGfx::Disc(X(110) + b * 24.f, Y(82) + 0.5f, 2.f, C_WHITE, Al(150));
                }
                if (!e.m.okMisc) UiGfx::Small("?", X(298), Y(80), Tc(C_DIM));
                if (e.m.okDex) snprintf(buf, sizeof(buf), TR("%u capturados / %u vistos"), e.m.caught, e.m.seen); else snprintf(buf, sizeof(buf), "-");
                InfoRow(Y(100), "Pokédex", buf, C_TEXT);
                if (e.m.okParty) snprintf(buf, sizeof(buf), TR("%u Pkmn, Nv. %u-%u"), e.m.partyCount, e.m.avgLevel, e.m.maxLevel); else snprintf(buf, sizeof(buf), "-");
                InfoRow(Y(120), "Equipo", buf, C_TEXT);
                InfoRow(Y(140), "Liga Pokémon", e.m.league ? "Campeón" : "Aún no", e.m.league ? C_GOLD : C_TEXT);
                if (e.m.okTime) snprintf(buf, sizeof(buf), "%uh %02um", e.m.hours, e.m.minutes); else snprintf(buf, sizeof(buf), "-");
                InfoRow(Y(160), "Tiempo de juego", buf, C_TEXT);
                if (e.m.okMisc) snprintf(buf, sizeof(buf), "$%lu", (unsigned long)e.m.money); else snprintf(buf, sizeof(buf), "-");
                InfoRow(Y(180), "Dinero", buf, C_TEXT);
                break;
            }
            // tabla general (toca una fila para ver su ficha)
            static const int kCol[7] = { 16, 38, 116, 160, 194, 226, 252 };   // v17: más aire para títulos en japonés/coreano
            static const char *const kHead[7] = { "#", "JUGADOR", "MED", "LIGA", "DEX", "NV", "TIEMPO" };
            for (int c = 0; c < 7; c++) UiGfx::Small(TR(kHead[c]), X(kCol[c]), Y(45), Tc(C_DIM));
            for (int i = 0; i < s_rankN && i < 7; i++) {
                const RankEntry &e = s_rank[i];
                int ry = Y(58 + i * 20);
                bool pr = IsPressed(1500 + i);
                if (i == 0 && !pr) {
                    UiGfx::RRectV(X(10), ry, 300, 18, 9, RGB(118, 92, 30), RGB(84, 62, 18), Al(225));
                    UiGfx::Stroke(X(10), ry, 300, 18, 9, C_GOLD, Al(150));
                } else Card(X(10), ry, 300, 18, 9, pr);
                Rgb fg = e.me ? C_GOLD : e.offline ? C_DIM : C_TEXT;
                if (i == 0 && !e.offline) fg = e.me ? C_GOLD : C_WHITE;
                char cell[16];
                snprintf(cell, sizeof(cell), "%d", i + 1);
                UiGfx::Small(cell, X(kCol[0]), ry + 4, Tc(fg));
                if (!e.offline) {                                    // conectado: su punto de color (tú, el tuyo)
                    const u8 *pc = kPlayerColors[((e.me ? App::MyId() : e.id) > 0 ? (e.me ? App::MyId() : e.id) - 1 : 0) % 12];
                    UiGfx::Disc(X(31) + 0.5f, ry + 9.f, 3.f, RGB(pc[0], pc[1], pc[2]), Al(255));
                }
                snprintf(cell, sizeof(cell), "%.12s", e.name);
                UiGfx::Small(cell, X(kCol[1]), ry + 4, Tc(fg));
                if (e.m.okMisc) snprintf(cell, sizeof(cell), "%d", Badges(e.m.badges)); else snprintf(cell, sizeof(cell), "?");
                UiGfx::Small(cell, X(kCol[2]), ry + 4, Tc(fg));
                UiGfx::Small(e.m.league ? TR("Si") : "-", X(kCol[3]), ry + 4, Tc(fg));
                if (e.m.okDex) snprintf(cell, sizeof(cell), "%u", e.m.caught); else snprintf(cell, sizeof(cell), "?");
                UiGfx::Small(cell, X(kCol[4]), ry + 4, Tc(fg));
                if (e.m.okParty) snprintf(cell, sizeof(cell), "%u", e.m.maxLevel); else snprintf(cell, sizeof(cell), "?");
                UiGfx::Small(cell, X(kCol[5]), ry + 4, Tc(fg));
                if (e.m.okTime) snprintf(cell, sizeof(cell), "%uh%02u", e.m.hours, e.m.minutes); else snprintf(cell, sizeof(cell), "-");
                UiGfx::Small(cell, X(kCol[6]), ry + 4, Tc(fg));
                AddHit(X(10), ry, 300, 18, 1500 + i);
            }
            if (s_rankN <= 1) UiGfx::Small(TR("Cuando tus amigos se conecten apareceran aqui."), X(22), Y(110), Tc(C_DIM));
            break;
        }
        case SEC_CHAT: {
            UiGfx::Text(TR("Mensajes recientes"), X(18), Y(44), Tc(C_TEXT));
            char lines[4][48]; u32 col[4]; u64 age[4];
            int n = App::GetChat(lines, col, age, 4, now);
            if (!n) UiGfx::TextCenter(TR("Aún no hay mensajes"), X(160), Y(110), Tc(C_DIM));
            for (int i = 0; i < n; i++) {
                int by = Y(68 + i * 32);
                Card(X(12), by, 296, 28, 13);
                UiGfx::Fit(lines[i], 276, fit, sizeof(fit));
                UiGfx::Text(fit, X(24), by + 5, Tc(RGB((col[i] >> 16) & 255, (col[i] >> 8) & 255, col[i] & 255)));
            }
            break;
        }
        case SEC_COMBATE: {
            App::Invite inv = App::GetInvite();
            App::Outgoing og = App::GetOutgoing();
            bool hot = inv.pending || og.pending;
            if (inv.pending) {
                char nm[24]; App::AsciiFold(inv.name, nm, sizeof(nm));
                snprintf(buf, sizeof(buf), TR(inv.type == 1 ? "%s quiere combatir" : "%s quiere intercambiar"), nm);
            } else if (og.pending) {
                const MMO::RemoteView *rv = FindView(og.to);
                char nm[24] = "?"; if (rv) App::AsciiFold(rv->name, nm, sizeof(nm));
                snprintf(buf, sizeof(buf), TR("Esperando respuesta de %s"), nm);
            } else snprintf(buf, sizeof(buf), "%s", TR("Sin invitaciones pendientes"));
            if (hot) {
                UiGfx::RRectV(X(12), Y(42), 296, 24, 12, RGB(96, 76, 26), RGB(70, 54, 16), Al(225));
                UiGfx::Stroke(X(12), Y(42), 296, 24, 12, C_GOLD, Al(130));
            } else Card(X(12), Y(42), 296, 24, 12);
            UiGfx::Fit(buf, 272, fit, sizeof(fit));
            UiGfx::Text(fit, X(24), Y(46), Tc(hot ? C_GOLD : C_SUB));
            UiGfx::Text(TR("Cómo combatir o intercambiar"), X(18), Y(76), Tc(C_TEXT));
            static const char *const kSteps[4] = { "Uno: Multijugador > Crear sala", "El otro: Multijugador > Unirse",
                                                   "Abran el PSS y toquen Wi-Fi", "Toquen a su amigo en Transeúntes" };
            for (int i = 0; i < 4; i++) {
                int sy = Y(100 + i * 24);
                UiGfx::Disc(X(26), sy + 9.f, 8.f, C_ACC1, Al(255));
                char d[4]; snprintf(d, sizeof(d), "%d", i + 1);
                UiGfx::Small(d, X(23), sy + 4, Tc(C_WHITE));
                UiGfx::Fit(TR(kSteps[i]), 260, fit, sizeof(fit));
                UiGfx::Text(fit, X(40), sy + 1, Tc(C_SUB));
            }
            break;
        }
        case SEC_PERSONAJE: {
            // izquierda: controles (cada uno en su sitio); derecha: tu nombre ARRIBA y el personaje debajo (sin pisarse)
            // v16.5: tres filas (ropa, gorro / lazo, nombre); flechas 400/401, 404/405 y 402/403
            const char *const kLab[3] = { "ROPA", IsMay() ? "LAZO" : "GORRO", "NOMBRE" };
            const char *vals[3] = { ColorName(Cfg::colorVariant), AccName(Cfg::accColor), TagName(Cfg::tagColor) };
            static const int kCode[3] = { 400, 404, 402 };
            for (int r = 0; r < 3; r++) {
                int ly = Y(42 + r * 40), cy = ly + 13;
                UiGfx::Small(TR(kLab[r]), X(18), ly, Tc(C_DIM));
                Card(X(14), cy, 146, 28, 14);
                for (int side = 0; side < 2; side++) {
                    int code = kCode[r] + side, bx = side ? X(130) : X(16);
                    bool pr = IsPressed(code);
                    UiGfx::RRectV(bx, cy + 2, 28, 24, 12, pr ? RGB(96, 156, 255) : RGB(60, 82, 140), pr ? RGB(56, 110, 232) : RGB(40, 56, 104), Al(245));
                    Arrow(bx + 14, cy + 14, side != 0, C_WHITE);
                    AddHit(side ? X(118) : X(10), cy - 4, 44, 36, code);
                }
                UiGfx::Fit(vals[r], 80, fit, sizeof(fit));
                UiGfx::TextCenter(fit, X(87), cy + 6, Tc(r < 2 ? C_GOLD : C_TEXT));
            }
            UiGfx::Text(TR("Verme así"), X(18), Y(171), Tc(C_TEXT));
            Switch(X(112), Y(166), s_knob[0], IsPressed(410));
            AddHit(X(12), Y(163), 152, 34, 410);
            // etiqueta de nombre con su color, encima del personaje
            {
                const u8 *pc = kPlayerColors[(App::MyId() ? App::MyId() - 1 : 0) % 12];
                if (Cfg::tagColor >= 1 && Cfg::tagColor < SkinColors::TAG_COUNT) pc = SkinColors::kTagRgb[Cfg::tagColor];
                UiGfx::Fit(name.c_str(), 120, fit, sizeof(fit));
                int tw = UiGfx::TextW(fit) + 18, tx = X(236) - tw / 2;
                UiGfx::Shadow(tx, Y(46), tw, 19, 9, 4, Al(90));
                UiGfx::RRect(tx, Y(46), tw, 19, 9, RGB(pc[0] * 3 / 4, pc[1] * 3 / 4, pc[2] * 3 / 4), Al(250));
                UiGfx::Stroke(tx, Y(46), tw, 19, 9, RGB(18, 20, 30), Al(160));
                int luma = (pc[0] * 3 + pc[1] * 6 + pc[2]) / 10;
                UiGfx::Text(fit, tx + 9, Y(47), Tc(luma > 175 ? RGB(16, 18, 28) : C_WHITE));
            }
            // escenario: sombra suave a sus pies
            UiGfx::RRect(X(194), Y(184), 84, 10, 5, RGB(0, 0, 10), Al(80));
            UiGfx::RRect(X(204), Y(186), 64, 6, 3, RGB(0, 0, 10), Al(90));
            if (App::GetAssetState() == App::ASSETS_OK) {
                s_prevOn = true; s_prevCx = X(236); s_prevFeet = Y(190); s_prevH = 116;   // de y 74 a 190: el nombre acaba en 65
            } else UiGfx::TextCenter(TR("Cargando..."), X(236), Y(120), Tc(C_SUB));
            break;
        }
        case SEC_PANTALLA: {
            static const char *const kHud[3] = { "Auto", "Siempre", "Nunca" };
            static const char *const kFol[3] = { "Todos", "El mío", "Ninguno" };
            static const char *const kPerf[3] = { "Auto", "Calidad", "Ahorro" };
            const int rx = X(146), rw = 158;
            UiGfx::Text(TR("Recuadro"), X(18), Y(47), Tc(C_TEXT));
            Segmented(rx, Y(43), rw, 24, kHud, Cfg::hudMode, s_segX[0], 600);
            UiGfx::Text(TR("Nombres"), X(18), Y(78), Tc(C_TEXT));
            Switch(X(256), Y(73), s_knob[1], IsPressed(650));
            AddHit(X(200), Y(70), 108, 32, 650);
            UiGfx::Text(TR("Acompañante"), X(18), Y(109), Tc(C_TEXT));
            Segmented(rx, Y(105), rw, 24, kFol, Cfg::followers, s_segX[1], 603);
            UiGfx::Text(TR("Rendimiento"), X(18), Y(140), Tc(C_TEXT));
            Segmented(rx, Y(136), rw, 24, kPerf, Cfg::perfMode, s_segX[2], 606);
            // v17: idioma (del juego y del MMO), con flechas como en Mi personaje
            UiGfx::Text(TR("Idioma"), X(18), Y(171), Tc(C_TEXT));
            Card(rx, Y(166), rw, 26, 13);
            for (int side = 0; side < 2; side++) {
                int code = 620 + side, bx = side ? rx + rw - 26 : rx + 2;
                bool pr = IsPressed(code);
                UiGfx::RRectV(bx, Y(167), 24, 24, 12, pr ? RGB(96, 156, 255) : RGB(60, 82, 140), pr ? RGB(56, 110, 232) : RGB(40, 56, 104), Al(245));
                Arrow(bx + 12, Y(179), side != 0, C_WHITE);
                AddHit(side ? rx + rw - 36 : rx - 8, Y(162), 44, 34, code);
            }
            UiGfx::Fit(LangValue(), rw - 60, fit, sizeof(fit));
            UiGfx::TextCenter(fit, rx + rw / 2, Y(171), Tc(C_GOLD));
            for (int k = 0; k < 4; k++) UiGfx::FadeLine(X(16), Y(70 + k * 31), 288, C_EDGE, Al(60));
            break;
        }
        case SEC_CONEXION: {
            char st[48]; App::GetStatus(st, sizeof(st));
            if (conn) snprintf(st, sizeof(st), "%s", TR("Conectado"));   // el estado guardado va en el idioma del OSD
            UiGfx::Text(TR("Tu conexión"), X(18), Y(44), Tc(C_TEXT));
            InfoRow(Y(66), "Estado", st, conn ? C_GREEN : C_ORANGE, 18);
            InfoRow(Y(84), "Servidor", Cfg::ServerIp().c_str(), C_TEXT, 18);
            InfoRow(Y(102), "Sala", room.c_str(), C_TEXT, 18);
            InfoRow(Y(120), "Tu nombre", name.c_str(), C_TEXT, 18);
            snprintf(buf, sizeof(buf), "J%d", App::MyId());
            InfoRow(Y(138), "Tu número", conn ? buf : "-", C_TEXT, 18);
            snprintf(buf, sizeof(buf), "%d", conn ? MMO::g_players.ActiveCount() + 1 : 0);
            InfoRow(Y(156), "En línea", buf, C_TEXT, 18);
            Button(X(14), Y(176), 292, 22, "Reconectar", 700);
            break;
        }
        default: {
            int tpc = (s_inContent && s_item < 6) ? s_item : s_helpTopic;
            const HelpTopic &h = kHelp[tpc];
            UiGfx::Text(TR(h.title), X(18), Y(44), Tc(C_GOLD));
            UiGfx::FadeLine(X(16), Y(64), 288, C_EDGE, Al(90));
            for (int i = 0, y = 70; i < 6; i++) if (h.lines[i][0]) { UiGfx::Text(TR(h.lines[i]), X(18), Y(y), Tc(C_TEXT)); y += 21; }
            break;
        }
    }
    UiGfx::PageEnd();
    s_ox = 0; s_oy = 0;
    s_fade = open;
    // botones de control (para jugar solo con el ratón / el dedo)
    for (int i = 0; i < 5; i++) {
        const FootBtn &b = kFoot[i];
        Button(b.x, FOOT_Y + oy, b.w, FOOT_H, b.label, b.code, true, b.style);
    }
    if (s_prevOn) UiGfx::SetAfterRect(s_prevCx - 48, s_prevFeet - s_prevH - 8, 96, s_prevH + 14);
    UiGfx::End(OSD::GetBottomScreen(), AfterBottom, s_prevOn ? (u32)(s_now / 33) + 1 : 0);   // el modelo 3D gira: cada cuadro es distinto
}

// ---------------------------------------------------------------------------------------------
static void GoBack() {
    if (s_whisperPick) { s_whisperPick = false; s_item = 0; s_scroll = 0; return; }
    if (s_subPlayer) { s_subPlayer = 0; s_item = 0; return; }
    if (s_inContent) { s_inContent = false; return; }
    s_open = false;
}

static void ChangeSection(int sec) {
    s_sec = (sec + SEC_COUNT) % SEC_COUNT;
    s_item = 0; s_scroll = 0; s_subPlayer = 0; s_showPss = false; s_whisperPick = false;
}

// Ejecuta la opción con ese id de la lista actual (y la marca arriba, para que se vea qué cambió)
static void ActivateId(int id, int dir) {
    BuildOptions();
    for (int i = 0; i < s_optCount; i++)
        if (s_opt[i].id == id) { s_inContent = true; s_item = i; Activate(s_opt[i], dir); return; }
}
static void FocusId(int id) {
    BuildOptions();
    for (int i = 0; i < s_optCount; i++) if (s_opt[i].id == id) { s_inContent = true; s_item = i; return; }
}

// Lo que pasa al SOLTAR el dedo sobre algo (si se apartó del botón, no llega aquí)
static void Commit(int h) {
    if (h >= 100 && h < 100 + SEC_COUNT) { ChangeSection(h - 100); s_inContent = true; return; }
    switch (h) {
        case 500: if (!s_inContent) ChangeSection(s_sec - 1); else if (s_item > 0) s_item--; return;
        case 501: if (!s_inContent) ChangeSection(s_sec + 1); else if (s_item + 1 < s_optCount) s_item++; return;
        case 502: if (!s_inContent) s_inContent = true; else if (s_item < s_optCount) Activate(s_opt[s_item], +1); return;
        case 503: GoBack(); return;
        case 504: s_open = false; return;
        case 310: ChangeSection(SEC_COMBATE); s_inContent = true; s_showPss = true; return;
        case 400: case 401: ActivateId(5002, h == 400 ? -1 : +1); return;
        case 402: case 403: ActivateId(5003, h == 402 ? -1 : +1); return;
        case 404: case 405: ActivateId(5005, h == 404 ? -1 : +1); return;
        case 410: ActivateId(5004, +1); return;
        case 650: ActivateId(6002, +1); return;
        case 620: case 621: ActivateId(6005, h == 620 ? -1 : +1); return;
        case 700: ActivateId(7004, +1); return;
        case 800: ActivateId(2001, +1); return;
        case 801: ActivateId(2002, +1); return;
        default: break;
    }
    if (h >= 600 && h < 609) {                                    // selectores: se toca directamente la opción
        int k = (h - 600) / 3, seg = (h - 600) % 3;
        if (k == 0) { Cfg::hudMode = seg; FocusId(6001); }
        else if (k == 1) { Cfg::followers = seg; FocusId(6003); }
        else { Cfg::perfMode = seg; FocusId(6004); }
        Cfg::Save();
        return;
    }
    if (h >= 300 && h < 300 + s_rowCount && s_sec == SEC_SALA) { s_inContent = true; s_subPlayer = s_rowIds[h - 300]; s_item = 0; return; }
    if (h >= 1500 && h < 1500 + MMO::RANKBOOK_MAX + 1 && s_sec == SEC_RANKING) { s_inContent = true; s_item = h - 1500; return; }
}

// Página actual (sección + subpágina): si cambia, el contenido entra con su animación
static bool s_demo = false;
static int  s_demoMode = 0;
void StartDemo(int mode) { s_demo = true; s_demoMode = mode; }
static u64 s_demoShotAt = 0;                            // prueba: fotos de las dos pantallas en cada paso (v17)
static int s_demoShotStep = 0, s_demoShotSec = 0;

static int PageKey() { return s_sec * 1000 + (s_subPlayer ? 100 + s_subPlayer : 0) + (s_whisperPick ? 50 : 0); }

static uint64_t UiTick() { return svcGetSystemTick(); }

bool OnMenuHotkey() {
    if (s_wantCtrpf) { s_wantCtrpf = false; return true; }    // segunda llamada: abrir CTRPF
    bool wasPaused = Process::IsPaused();
    // v16.3: primero el OSD termina el cuadro que esté dibujando y deja de dibujar; DESPUÉS se pausa el juego. Antes se
    // pausaba primero: si el OSD estaba a mitad de un cuadro, ese cuadro no acababa hasta cerrar el menú, la memoria no
    // se liberaba y el menú iba sin caché (43 vueltas/s en vez de 50; prueba con 4 amigos)
    Overlay::Hold(true);
    for (int i = 0; i < 40 && Overlay::Busy(); i++) svcSleepThread(2 * 1000000LL);
    if (!wasPaused) Process::Pause();
    s_open = true;
    s_sec = SEC_INICIO;
    s_item = 0;
    s_scroll = 0;
    s_inContent = true;
    s_subPlayer = 0;
    R3D::SetClock(nullptr, 0);                                // sin presupuesto: el juego está en pausa
    // v16.2: memoria para los cachés del menú. Con amigos en pantalla, los sprites de sus avatares (hasta ~100 KB
    // cada uno), la z del motor (96 KB) y los dibujos de los acompañantes ocupaban el sitio y el menú de abajo se
    // quedaba "sin memoria" (25 vueltas/s en vez de 50). Con el juego en pausa nada de eso se ve: se libera y se
    // vuelve a crear solo al cerrar. Antes se espera a que el OSD termine el cuadro que estuviera dibujando.
    for (int i = 0; i < 20 && Overlay::Busy(); i++) svcSleepThread(5 * 1000000LL);
    if (Overlay::Busy()) Log::Write("menu: el OSD sigue ocupado: no se libera memoria");
    MemReport("antes de abrir el menú");
    s_openCount++;
    // v16.3: con el plugin de 10 MB sobra memoria (>1,5 MB sin pedir): no se suelta nada y, al cerrar, los amigos y
    // sus Pokémon siguen dibujados al instante. Con el de 5 MB se libera como antes para los cachés del menú.
    const bool tight = HeapRest() < 1536ul * 1024;
    if (tight && !Overlay::Busy()) { R3D::ReleaseMemory(); Follower3D::Release(); }
    MemReport(tight ? "menú, tras liberar" : "menú (sobra memoria: no se libera nada)");
    u64 tCap = svcGetSystemTick();
    UiGfx::CaptureBackground(OSD::GetTopScreen(), OSD::GetBottomScreen());   // el juego, desenfocado, de fondo
    tCap = svcGetSystemTick() - tCap;
    // estado visual de partida: el menú "se materializa" (sube y aparece) y todo lo demás en su sitio
    s_openT.Snap(0.f); s_openT.Set(1.f);
    s_pageT.Snap(1.f);
    s_sideY.init = s_listY.init = s_tabX.init = s_thumbY.init = false;
    s_toastT.Snap(0.f);
    s_toastUntil = 0;
    for (auto &k : s_knob) k.init = false;
    for (auto &k : s_segX) k.init = false;
    s_pressCode = -1; s_pressInside = false;
    s_nh = 0;
    u32 repeatKey = 0;
    u64 repeatAt = 0, last = osGetTime();
    bool touchWas = true, waitRelease = true;                  // ignorar el SELECT que abrió el menú
    int lastPage = -1, lastSec = s_sec;
    int demoStep = 0;
    memset(&UiGfx::g_prof, 0, sizeof(UiGfx::g_prof));
    u64 pTop = 0, pBot = 0, pSwap = 0, pLogic = 0, pSleep = 0, pIter = 0, pBg = svcGetSystemTick();
    u32 nIter = 0;
    u64 demoNext = osGetTime() + 2500;
    while (s_open) {
        u64 ti0 = svcGetSystemTick();
        Controller::Update();
        u64 now = osGetTime();
        float dt = (float)(s64)(now - last) / 1000.f;
        last = now;
        s_now = now;
        u32 down = Controller::GetKeysDown(false);
        u32 pressed = Controller::GetKeysPressed();
        if (waitRelease) { if (!(down & (Key::Select | Key::Touchpad))) waitRelease = false; pressed = 0; }
        // repetición al mantener arriba/abajo/izquierda/derecha
        const u32 nav = Key::Up | Key::Down | Key::Left | Key::Right;
        if (pressed & nav) { repeatKey = pressed & nav; repeatAt = now + 300; }
        else if ((down & repeatKey) && now >= repeatAt) { pressed |= down & repeatKey; repeatAt = now + 70; }
        else if (!(down & repeatKey)) repeatKey = 0;

        MMO::g_players.Sample(now, s_views);
        BuildOptions();

        if (pressed & (Key::Select | Key::Start)) s_open = false;
        if (pressed & Key::L) { ChangeSection(s_sec - 1); BuildOptions(); }
        if (pressed & Key::R) { ChangeSection(s_sec + 1); BuildOptions(); }
        if (!s_inContent) {
            if (pressed & Key::Up) { ChangeSection(s_sec - 1); BuildOptions(); }
            if (pressed & Key::Down) { ChangeSection(s_sec + 1); BuildOptions(); }
            if (pressed & (Key::A | Key::Right)) s_inContent = true;
            else if (pressed & Key::B) s_open = false;
        } else {
            if ((pressed & Key::Up) && s_item > 0) s_item--;
            if ((pressed & Key::Down) && s_item + 1 < s_optCount) s_item++;
            if (s_optCount && s_item < s_optCount) {
                const Option &o = s_opt[s_item];
                bool cyc = IsCycle(o.id);
                if (pressed & Key::A) Activate(o, +1);
                else if (cyc && (pressed & Key::Right)) Activate(o, +1);
                else if (cyc && (pressed & Key::Left)) Activate(o, -1);
                else if (pressed & Key::Left) GoBack();
            }
            if (pressed & Key::B) GoBack();
        }
        if (s_demo && s_demoMode == 1 && now >= demoNext) {  // prueba v17: cambiar el idioma rápido y entre secciones
            static int s_origLang = -1;
            if (s_origLang < 0) s_origLang = Cfg::gameLang;
            int k = demoStep++;
            demoNext = now + (k < 18 ? 400 : 3000);           // primero muy rápido; luego despacio (da tiempo a cargar)
            static const int kSecs[6] = { SEC_PANTALLA, SEC_SALA, SEC_RANKING, SEC_INICIO, SEC_AYUDA, SEC_CONEXION };
            if (k >= 36) {
                Cfg::gameLang = s_origLang; Cfg::Save();
                Log::Write("menu: prueba de idiomas terminada (%d cambios)", k);
                s_demo = false; s_open = false;
            } else {
                CycleLang(+1);
                int sec = (k >= 18 && k < 27) ? SEC_PANTALLA : kSecs[k % 6];   // la página con más opciones, en los 9
                ChangeSection(sec);
                s_inContent = true;
                // v17.0 r2: la foto, 0,4 s después del cambio (el idioma tiene que verse bien al momento, sin "?")
                if (k >= 18) { s_demoShotAt = now + 400; s_demoShotStep = k - 18; s_demoShotSec = sec; }
                Log::Write("menu: idioma %d (%s)", Cfg::gameLang, Lang::Name(Cfg::gameLang));
            }
        }
        if (s_demo && s_demoMode == 0 && now >= demoNext) {  // prueba: una sección cada 2,5 s y se cierra
            demoNext = now + 2500;
            static const int kSteps[12][2] = { {SEC_PERSONAJE, 1}, {SEC_PANTALLA, 1}, {SEC_SALA, 1}, {SEC_RANKING, 0}, {SEC_RANKING, 1},
                                               {SEC_CHAT, 1}, {SEC_COMBATE, 1}, {SEC_CONEXION, 1}, {SEC_AYUDA, 1},
                                               {SEC_INICIO, 1}, {SEC_INICIO, 1}, {-1, 0} };
            int k = demoStep++;
            if (k >= 12 || kSteps[k][0] < 0) { s_demo = false; s_open = false; }
            else {
                ChangeSection(kSteps[k][0]);
                s_inContent = kSteps[k][1] != 0;
                s_demoShotAt = now + 1800; s_demoShotStep = k; s_demoShotSec = kSteps[k][0];
                if (k == 10) Toast("Prueba de aviso");
            }
        }
        // táctil / ratón: se ilumina al apoyar, actúa al soltar dentro (con 10 px de margen), se cancela si te apartas
        bool touch = Touch::IsDown();
        if (touch && !waitRelease) {
            UIntVector p = Touch::GetPosition();
            int tx = (int)p.x, ty = (int)p.y;
            if (!touchWas) {
                s_pressCode = -1;
                for (int i = s_nh - 1; i >= 0; i--)
                    if (InHit(s_hit[i], tx, ty, 0)) { s_pressCode = s_hit[i].code; s_pressRect = s_hit[i]; break; }
                s_pressInside = s_pressCode >= 0;
            } else if (s_pressCode >= 0) {
                s_pressInside = InHit(s_pressRect, tx, ty, 10);
            }
        } else if (!touch && touchWas) {
            if (s_pressCode >= 0 && s_pressInside) Commit(s_pressCode);
            s_pressCode = -1; s_pressInside = false;
        }
        bool input = pressed != 0 || touch != touchWas;
        touchWas = touch;
        if (!s_open) break;

        BuildOptions();
        if (s_item >= s_optCount) s_item = s_optCount ? s_optCount - 1 : 0;
        ClampScroll();
        // cambio de página: el contenido entra desde el lado hacia el que vas (y vuelve por el mismo camino)
        int page = PageKey();
        if (page != lastPage) {
            if (lastPage >= 0) {
                if (s_sec != lastSec) s_pageDir = s_sec > lastSec ? 1 : -1;
                else s_pageDir = (s_subPlayer || s_whisperPick) ? 1 : -1;
                s_pageT.Snap(0.f); s_pageT.Set(1.f);
                s_listY.init = false;
                s_thumbY.init = false;
            }
            lastPage = page; lastSec = s_sec;
        }
        // destinos de los muelles (parten SIEMPRE de donde están: se pueden interrumpir)
        s_sideY.Set((float)(SIDE_Y + 3 + s_sec * SIDE_ROW));
        s_tabX.Set((float)(TAB_X + s_sec * TAB_W + TAB_W / 2));
        s_listY.Set((float)(LIST_Y + (s_item - s_scroll) * ROW_H + 1));
        s_thumbY.Set(ThumbTarget());
        s_toastT.Set(now < s_toastUntil ? 1.f : 0.f);
        s_knob[0].Set(Cfg::showMySkin ? 1.f : 0.f);
        s_knob[1].Set(Cfg::showNames ? 1.f : 0.f);
        s_segX[0].Set((float)(Cfg::hudMode % 3));
        s_segX[1].Set((float)(Cfg::followers % 3));
        s_segX[2].Set((float)(Cfg::perfMode % 3));
        s_openT.Step(dt, 0.38f);
        s_pageT.Step(dt, 0.32f);
        // el final de una entrada (>97 %) ya no se ve: se asienta en lugar de repintar la página varias veces más
        if (s_openT.target == 1.f && s_openT.x > 0.97f) s_openT.Snap(1.f);
        if (s_pageT.target == 1.f && s_pageT.x > 0.97f) s_pageT.Snap(1.f);
        s_sideY.Step(dt, 0.22f); s_tabX.Step(dt, 0.22f); s_listY.Step(dt, 0.18f); s_thumbY.Step(dt, 0.22f);
        s_toastT.Step(dt, 0.30f);
        for (auto &k : s_knob) k.Step(dt, 0.22f);
        for (auto &k : s_segX) k.Step(dt, 0.26f);

        // Cada cuadro se arma la lista de lo que hay que dibujar (barato) y se presenta: 60 cuadros por segundo
        // estables. UiGfx solo pinta de verdad lo que cambió respecto a lo que ya tiene ese búfer de pantalla.
        (void)input;
        u64 t1 = svcGetSystemTick();
        DrawTop(now);
        u64 t2 = svcGetSystemTick();
        DrawBottom(now);
        if (s_demoShotAt && now >= s_demoShotAt) {               // prueba: lo que se ve en cada paso (v17: todas)
            s_demoShotAt = 0;
            char top[40], bot[40];
            snprintf(top, sizeof(top), "mmo_dev_menu_%02d_top.bmp", s_demoShotStep);
            snprintf(bot, sizeof(bot), "mmo_dev_menu_%02d_bot.bmp", s_demoShotStep);
            Overlay::DevShot(OSD::GetTopScreen(), top);
            Overlay::DevShot(OSD::GetBottomScreen(), bot);
            if (s_demoShotSec == SEC_PERSONAJE) Overlay::DevShot(OSD::GetBottomScreen(), "mmo_dev_personaje.bmp");
            Log::Write("menu: fotos de prueba del paso %d", s_demoShotStep);
        }
        u64 t3 = svcGetSystemTick();
        OSD::SwapBuffers();
        u64 t4 = svcGetSystemTick();
        if (nIter == 30) MemReport("menú, ya abierto");          // tras la aparición y los cachés (medio segundo)
        // ~60 vueltas por segundo: se espera solo lo que falta para 16 ms (si dibujar costó, se espera menos)
        s64 spentMs = (s64)((t4 - ti0) / 268111u);
        svcSleepThread((spentMs < 14 ? 16 - spentMs : 2) * 1000000LL);
        u64 t5 = svcGetSystemTick();
        pLogic += t1 - ti0; pTop += t2 - t1; pBot += t3 - t2; pSwap += t4 - t3; pSleep += t5 - t4; pIter += t5 - ti0; nIter++;
    }
    MemReport("menú abierto, al cerrar");
    UiGfx::ReleaseBackground();
    // v16.3: antes de volver al juego, con la memoria del menú ya suelta: fuera lo que reservó la vista previa y se
    // recupera el espacio propio de los Pokémon acompañantes (256 KB seguidos; si se pidiera más tarde, la memoria ya
    // estaría troceada y no cabría: prueba con 4 amigos)
    if (tight) {
        R3D::ReleaseMemory();
        Follower3D::Reserve();
    }
    MemReport("menú cerrado");
    {   // perfilado (ms por vuelta, tiempo del ARM11 emulado): qué cuesta de verdad
        const double k = 1.0 / (268111.856 * (nIter ? nIter : 1));
        const UiGfx::Prof &P = UiGfx::g_prof;
        Log::Write("menu: vueltas=%lu (%.1f/s) captura=%.0fms | por vuelta: logica=%.1f arriba=%.1f abajo=%.1f swap=%.1f espera=%.1f total=%.1f ms",
                   (unsigned long)nIter, nIter * 1000.0 / ((svcGetSystemTick() - pBg) / 268111.856), tCap / 268111.856,
                   pLogic * k, pTop * k, pBot * k, pSwap * k, pSleep * k, pIter * k);
        Log::Write("menu: pintadas=%lu saltadas=%lu paneles=%lu paginas=%lu/%lu | componer=%.1f copiar=%.1f 3d=%.1f texto=%.1f ms/vuelta | anchos: %lu caché, %lu medidos (%.1f ms/vuelta)",
                   (unsigned long)P.draws, (unsigned long)P.skips, (unsigned long)P.chromeBuilds, (unsigned long)P.pageBuilds, (unsigned long)P.pageFrames,
                   P.compose * k, P.blit * k, P.after * k, P.text * k,
                   (unsigned long)P.wHit, (unsigned long)P.wMiss, P.wTicks * k);
    }
    R3D::SetClock(UiTick, SYSCLOCK_ARM11);
    Overlay::PokeHud();                                         // mostrar el HUD un momento al volver
    Overlay::Hold(false);
    if (!wasPaused) Process::Play();
    if (s_wantCtrpf) PluginMenu::ForceOpen();
    return false;
}

} // namespace Ui
