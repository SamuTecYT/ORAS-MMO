/*
 * main.cpp — ORAS MMO (arranque; la versión está en Protocol.hpp MMO_VERSION_STR, hoy 17.0)
 *
 * Plugin multijugador para Pokémon Rubí Omega / Zafiro Alfa (Azahar PC y Android).
 *  - Se conecta solo al servidor (MMO_DEFAULT_SERVER de Protocol.hpp, o el de mmo_server.txt) y a la sala guardada.
 *  - Sin calibración: encuentra la posición del jugador y la cámara del juego por sí mismo.
 *  - Amigos con el modelo 3D REAL del juego (GameHook) o con el motor propio (bici/color); acompañantes 3D.
 *  - Pines nativos en el AreaNav y en el mapa de Vuelo del juego (el mapa propio se borró en v16.0).
 * Memoria del juego: solo lectura, SALVO lo decidido por el proyecto: SkinTex, GameHook, ocultar la cabecita, GameLang y DepthProbe (que solo lee).
 */
#include "GameHook.hpp"
#include "DepthProbe.hpp"
#include "GameLang.hpp"
#include "GameText.hpp"
#include "GameFont.hpp"
#include "Lang.hpp"
#include "Perf.hpp"
#include "Ui.hpp"
#include "GameStats.hpp"
#include "SkinTex.hpp"
#include "Follower3D.hpp"
#include "Render3D.hpp"
#include <malloc.h>
#include "Emotes.hpp"
#include "SafePatch.hpp"   // parches de código con vaciado JIT (KB §12.2); aún sin uso
#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "Protocol.hpp"
#include "NetworkClient.hpp"
#include "Players.hpp"
#include "GameState.hpp"
#include "Config.hpp"
#include "App.hpp"
#include "Log.hpp"
#include "RomFS.hpp"
#include "HeroAssets.hpp"
#include "TownMap.hpp"
#include "Overlay.hpp"

using namespace CTRPluginFramework;

static volatile bool s_running = true;

// =============================================================================================
// Carga de recursos del juego (modelos, animaciones, mapa) en segundo plano
// =============================================================================================
static void AssetThread(void *) {
    svcSleepThread(1500 * 1000000LL);      // dejar que el juego arranque primero
    GameRomFS fs;
    char err[96] = "";
    if (!fs.Open()) {
        App::SetAssetState(App::ASSETS_FAILED, "no se pudo abrir el RomFS del juego");
        Log::Write("RomFS: no se pudo abrir");
        return;
    }
    u64 t0 = osGetTime();
    bool ok = Hero::LoadAll(fs, err, sizeof(err));
    Log::Write("Modelos: %s (%s) en %lu ms", ok ? "OK" : "FALLO", err, (unsigned long)(osGetTime() - t0));
    // v16: ya no se carga la imagen del mapa propio (los amigos van sobre los mapas del juego: GameMaps)
    // Basta con que cargue uno de los dos modelos: el otro se dibuja como marcador simple
    bool any = Hero::Get(Hero::MODEL_MAY) || Hero::Get(Hero::MODEL_BRENDAN);
    // v16.3: las cachés del motor 3D de los 4 modelos, ahora que la memoria está entera (antes se rehacían al cerrar el
    // menú y con la memoria troceada fallaban: amigos y vista previa sin dibujar). Antes de ASSETS_OK: nadie dibuja aún.
    int warm = 0;
    for (int m = 0; m < Hero::MODEL_COUNT; m++) {
        const Hero::Model *M = Hero::Get(m);
        if (M && R3D::Prewarm(*M)) warm++;
    }
    {
        struct mallinfo mi = mallinfo();
        Log::Write("motor 3D: %d cachés de modelos (%lu KB); memoria en uso %lu KB, libre %lu KB en huecos", warm,
                   (unsigned long)(R3D::CacheBytes() / 1024), (unsigned long)(mi.uordblks / 1024), (unsigned long)(mi.fordblks / 1024));
    }
    App::SetAssetState(any ? App::ASSETS_OK : App::ASSETS_FAILED, ok ? "" : err);
    GameText::Preload(fs);                   // v17.0 r2: los nombres de los 8 idiomas (cambiar de idioma es al momento)
    GameFont::Serve(fs);                     // y las letras de japonés y coreano (una por llamada: aquí y en el bucle)
    GameFont::Serve(fs);
    // el hilo sigue vivo para leer de la SD los dibujos 3D de los Pokémon acompañantes cuando se piden
    while (s_running) {
        bool more = Follower3D::Serve();     // v16.2: dibujos 3D de los acompañantes (de la SD)
        GameText::Serve(fs);                 // v17: nombres oficiales de Pokémon y lugares en el idioma del MMO
        GameFont::Serve(fs);                 // v17: letras japonesas/coreanas del menú con la fuente del juego
        svcSleepThread((more ? 8 : 40) * 1000000LL);
    }
    fs.Close();
}

// =============================================================================================
// Hilo de red
// =============================================================================================
static void HandlePacket(const PlayerPacket &p, u64 now) {
    u8 id = p.player_id;
    char name[16];
    MMO::g_players.OnPacket(p, now);
    MMO::g_players.GetName(id, name, sizeof(name));
    char line[64];
    switch (p.type) {
        case MMO_PKT_CHAT: {
            char txt[24];
            if (p.anim == 255) { memcpy(txt, p.msg, 20); txt[20] = 0; }
            else snprintf(txt, sizeof(txt), "%s", Lang::TOsd(App::ChatText(p.anim)));   // v17: en tu idioma
            bool whisper = p.btype != 0;
            if (whisper && p.btype != App::MyId()) break;          // un susurro que no es para mí
            MMO::g_players.SetBubble(id, txt, now);
            if (whisper) { snprintf(line, sizeof(line), Lang::TOsd("(susurro) %s: %s"), name, txt); App::PushChat(line, 0xFF90E0); }
            else { snprintf(line, sizeof(line), "%s: %s", name, txt); App::PushChat(line, 0xFFE070); }
            break;
        }
        case MMO_PKT_EMOTE: {
            GameHook::OnEmote(id, p.anim);
            int e = (p.anim >= 1 && p.anim <= Emotes::COUNT) ? p.anim - 1 : 0;
            char em[3] = { '\x01', (char)('A' + e), 0 };      // el OSD lo dibuja como icono
            MMO::g_players.SetBubble(id, em, now);
            snprintf(line, sizeof(line), "%s: [%s]", name, Lang::TOsd(Emotes::kNames[e]));
            App::PushChat(line, 0x80E0FF);
            break;
        }
        case MMO_PKT_EVENT_REQ:
            if (p.anim == App::MyId()) {
                App::Invite inv;
                inv.pending = true; inv.from = id; inv.type = (p.btype == 2) ? 2 : 1;
                Proto::CopyText(inv.name, sizeof(inv.name), name);
                App::SetInvite(inv);
                snprintf(line, sizeof(line), Lang::TOsd(inv.type == 1 ? "%s te invita a combatir (SELECT)" : "%s te invita a intercambiar (SELECT)"), name);
                App::PushChat(line, 0xFFA040);
                { char a[64]; App::AsciiFold(line, a, sizeof(a)); OSD::Notify(a, Color::Orange); }
            }
            break;
        case MMO_PKT_EVENT_RESP:
            if (p.anim == App::MyId() && App::GetOutgoing().pending && App::GetOutgoing().to == id) {
                App::ClearOutgoing();
                snprintf(line, sizeof(line), Lang::TOsd(p.btype == 1 ? "%s acepto! Abran el PSS" : "%s rechazo la invitacion"), name);
                App::PushChat(line, p.btype == 1 ? 0x80FF80 : 0xFF8080);
                { char a[64]; App::AsciiFold(line, a, sizeof(a)); OSD::Notify(a, p.btype == 1 ? Color::Lime : Color::Red); }
            }
            break;
        case MMO_PKT_EVENT_END:
            if (p.anim == App::MyId()) {
                bool matched = false;
                App::Invite inv = App::GetInvite();
                if (inv.pending && inv.from == id) { App::ClearInvite(); matched = true; }
                App::Outgoing og = App::GetOutgoing();
                if (og.pending && og.to == id) { App::ClearOutgoing(); matched = true; }
                if (matched) {
                    snprintf(line, sizeof(line), Lang::TOsd("%s cancelo la invitacion"), name);
                    App::PushChat(line, 0xFFA040);
                }
            }
            break;
        case MMO_PKT_LEAVE: {
            MMO::g_players.OnLeave(id);
            App::Invite inv = App::GetInvite();
            if (inv.pending && inv.from == id) App::ClearInvite();
            App::Outgoing og = App::GetOutgoing();
            if (og.pending && og.to == id) App::ClearOutgoing();
            snprintf(line, sizeof(line), Lang::TOsd("%s salio de la sala"), name);
            App::PushChat(line, 0xB0B0B0);
            break;
        }
        default:
            break;
    }
}

static void FillCommon(PlayerPacket &p, u8 type) {
    memset(&p, 0, sizeof(p));
    p.type = type;
    p.player_id = App::MyId();
    p.model_id = App::LocalModelId();
    std::string nm = Cfg::Name();
    Proto::CopyText(p.nickname, sizeof(p.nickname), nm.c_str());
    Proto::SetVersion(p);
    p.lvls[3] = (u8)((Cfg::colorVariant & 0x0F) | ((Cfg::tagColor & 0x0F) << 4));
    // v16.5: color del gorro / lazo (solo en POSITION: en los demás tipos btype tiene otro uso)
    if (type == MMO_PKT_POSITION) p.btype = (u8)(Cfg::accColor >= 0 && Cfg::accColor < Cfg::NUM_ACC ? Cfg::accColor : 0);
}

// Fin de sesión (desconexión, error o cambio de sala/IP): nada de la sesión anterior sobrevive
static void OnSessionLost() {
    App::SetConnection(false, 0);
    MMO::g_players.Clear();
    App::ClearInvite();
    App::ClearOutgoing();
}

static void NetworkThread(void *) {
    NetworkClient net;
    u64 nextTry = 0, lastSend = 0, lastPing = 0;
    int backoff = 2000;
    u16 seq = 0;
    u16 lastZone = 0xFFFF; u8 lastFacing = 0xFF, lastState = 0xFF, lastFlags = 0xFF;
    float lastX = 0, lastY = 0, lastZ = 0;
    u16 lastYaw16 = 0;
    u8 lastOldFacing = 0;
    u64 prevTick = 0;
    bool joinedAnnounced = false;

    // Género del jugador (modelo chico/chica que ven los demás): v16.1: se reintenta cada 0,5 s mientras no se
    // sepa (antes se gastaban los 20 intentos en el primer segundo y podía quedarse en "chico" toda la partida).
    // Si en 60 s jugando no se lee, se queda en chico para no bloquear el ranking.
    u64 nextGender = 0, genderSince = 0;

    while (s_running) {
        u64 now = osGetTime();
        Game::Update(now);
        Game::LocalState st = Game::Snapshot();

        if (st.valid && !App::GenderKnown() && now >= nextGender) {
            nextGender = now + 500;
            if (!genderSince) genderSince = now;
            // MyStatus.gender en el mismo bloque de guardado: P + 0x12AC1 (v1.0 y v1.4), validado con el nombre
            u32 P = Game::PlayerBlockAddr();
            u32 g = P + 0x12AC1, nmA = P + 0x12B04;
            u8 gv = 0xFF; u16 c0 = 0;
            if (P && Process::CheckAddress(g, MEMPERM_READ) && Process::CheckAddress(nmA, MEMPERM_READ) &&
                Process::Read8(g, gv) && Process::Read16(nmA, c0) && gv <= 1 && c0 >= 0x20 && c0 < 0x3000) {
                // si el plugin ya lo había cambiado, el de la partida es el guardado en la configuración
                App::SetDetectedFemale(gv);
                Log::Write("Genero detectado: %s", gv ? "chica" : "chico");
            } else if (now - genderSince > 60000) {
                App::SetDetectedFemale(0);
                Log::Write("Genero no detectado en 60 s: chico por defecto");
            }
        }

        if (App::TakeReconnectRequest()) {          // cambio de sala/IP o "Reconectar": aplicar ya
            if (net.IsConnected()) net.Disconnect();
            OnSessionLost();
            nextTry = 0;
            backoff = 2000;
        }

        if (!net.IsConnected()) {
            if (App::IsConnected()) OnSessionLost();
            if (now >= nextTry) {
                std::string ip = Cfg::ServerIp(), room = Cfg::Room();
                char msg[48];
                snprintf(msg, sizeof(msg), Lang::TOsd("Conectando a %s..."), ip.c_str());
                App::SetStatus(msg);
                std::string err;
                int id = net.Join(ip, Cfg::ServerPort(), room, err);
                if (id > 0) {
                    App::SetConnection(true, (u8)id);
                    MMO::g_players.Clear();
                    MMO::g_players.localId = (u8)id;
                    App::SetStatus(Lang::TOsd("Conectado"));
                    Log::Write("Conectado a %s sala %s como J%d", ip.c_str(), room.c_str(), id);
                    backoff = 2000;
                    lastSend = lastPing = 0;
                    lastZone = 0xFFFF;
                    if (!joinedAnnounced) {
                        char a[64], b[64];
                        snprintf(a, sizeof(a), Lang::TOsd("MMO: conectado a la sala %s"), room.c_str());
                        App::AsciiFold(a, b, sizeof(b));
                        OSD::Notify(b, Color::Lime);
                        joinedAnnounced = true;
                    }
                } else {
                    snprintf(msg, sizeof(msg), Lang::TOsd("Reintentando (%s)"), Lang::TOsd(err.c_str()));
                    App::SetStatus(msg);
                    nextTry = now + backoff;
                    if (backoff < 10000) backoff += 2000;
                    if (err == "sala llena") nextTry = now + 15000;
                }
            }
        } else {
            // ---- recibir
            PlayerPacket in;
            int r, guard = 0;
            while (guard++ < 256 && (r = net.Poll(in)) == 1) HandlePacket(in, osGetTime());   // hora real de llegada
            if (r < 0) {
                Log::Write("Conexion perdida");
                App::SetStatus(Lang::TOsd("Conexion perdida"));
                OnSessionLost();
                nextTry = now + 1500;
                svcSleepThread(33 * 1000000LL);
                continue;
            }

            // ---- enviar mi estado
            u8 flags = 0;
            if (st.inBattle) flags |= MMO_FLAG_IN_BATTLE;
            // v16.5: con un menú abierto (también Mochila, Pokédex o el mapa grande, que quitan el mapa) sigues
            // visible para tus amigos en tu sitio; solo las puertas y los viajes te ocultan un momento
            if (st.valid && st.playerState == Game::STATE_LOADING && !st.inApp) flags |= MMO_FLAG_TRANSITION;
            if (st.valid && (st.inApp || st.fieldMenu || st.shortcutMenu)) flags |= MMO_FLAG_MENU;
            {
                static u8 loggedFlags = 0xFF;
                if (flags != loggedFlags) {
                    loggedFlags = flags;
                    Log::Write("envio: flags=%02X (estado %d%s%s)", flags, st.playerState, st.inApp ? ", app del menu" : "",
                               st.fieldMenu ? ", menu X" : st.shortcutMenu ? ", menu Y" : "");
                }
            }
            if (st.ride == 2) flags |= MMO_FLAG_BIKE_ALT;
            bool canPos = st.valid;
            if (canPos) {
                float moved = fabsf(st.x - lastX) + fabsf(st.y - lastY) + fabsf(st.z - lastZ);
                bool changed = st.zone != lastZone || st.facing8 != lastFacing || st.moveState != lastState || flags != lastFlags;
                bool moving = moved > 0.01f || (st.moveState != MOVE_IDLE && st.moveState != MOVE_BIKE);
                u32 interval = moving ? 28 : 500;             // cada tick (~30 Hz) moviéndose, 2 Hz quieto
                if (changed || now - lastSend >= interval) {
                    // Al arrancar desde quieto: ancla con la posición anterior fechada en el tick previo, para que
                    // el avatar remoto no empiece a "patinar" antes de que el jugador real se mueva.
                    if (moving && lastSend && now - lastSend > 60 && prevTick > lastSend && st.zone == lastZone) {
                        PlayerPacket a;
                        FillCommon(a, MMO_PKT_POSITION);
                        a.map_id = Proto::MakeMapId(0, lastZone);
                        a.x = lastX; a.y = lastY; a.z = lastZ;
                        a.anim = MOVE_IDLE;
                        a.flags = lastFlags;
                        a.facing = lastOldFacing;
                        Proto::SetYaw(a, lastYaw16);
                        Proto::SetSeq(a, ++seq);
                        Proto::SetStamp(a, (u32)prevTick);
                        net.Queue(a);
                    }
                    PlayerPacket p;
                    FillCommon(p, MMO_PKT_POSITION);
                    u16 matrix = 0;
                    p.map_id = Proto::MakeMapId(matrix, st.zone);
                    p.x = st.x; p.y = st.y; p.z = st.z;
                    // compatibilidad v12.6: 0 S, 1 N, 2 O, 3 E
                    static const u8 kOldFacing[8] = { 1, 0, 2, 3, 1, 1, 0, 0 };
                    p.facing = kOldFacing[st.facing8 & 7];
                    p.anim = st.moveState;
                    p.flags = flags;
                    float yawN = st.yaw;
                    while (yawN < 0) yawN += 6.2831853f;
                    u16 yaw16 = (u16)(yawN * (65536.0f / 6.2831853f));
                    Proto::SetYaw(p, yaw16);
                    lastYaw16 = yaw16;
                    lastOldFacing = p.facing;
                    Proto::SetSeq(p, ++seq);
                    Proto::SetStamp(p, (u32)now);
                    net.Queue(p);
                    lastSend = now; lastZone = st.zone; lastFacing = st.facing8; lastState = st.moveState; lastFlags = flags;
                    lastX = st.x; lastY = st.y; lastZ = st.z;
                }
            } else {
                lastSend = 0;          // al volver, sin "ancla" con una posición de antes del hueco
                if (now - lastPing >= 1000) {
                    // latido: mantiene viva la conexión durante combates, títulos u oculto
                    PlayerPacket p;
                    FillCommon(p, MMO_PKT_PING);
                    p.flags = flags;
                    Proto::SetStamp(p, (u32)now);
                    net.Queue(p);
                    lastPing = now;
                }
            }
            // ---- paquetes de la interfaz (chat, emotes, invitaciones)
            PlayerPacket out;
            int sent = 0;
            while (sent++ < 8 && App::PopPacket(out)) {
                out.player_id = App::MyId();
                net.Queue(out);
            }
            if (!net.Flush()) {
                Log::Write("Conexion perdida (envio)");
                App::SetStatus(Lang::TOsd("Conexion perdida"));
                OnSessionLost();
                nextTry = now + 1500;
            }
            MMO::g_players.ExpireStale(osGetTime());
        }
        // v14: progreso (ranking) y combate en curso
        Stats::Update(now, st.inBattle);
        // Prueba de desarrollo: con el archivo mmo_dev_menu.txt el menú SELECT se abre solo (una vez) a los 4 s de
        // estar caminando y recorre todas sus secciones. Sin ese archivo no hace nada.
        {
            static int devState = 0;                     // 0 sin mirar, 1 activo, 2 hecho / no hay archivo
            static u64 freeSince = 0;
            if (devState == 0) devState = File::Exists("mmo_dev_menu.txt") ? 1 : 2;
            if (devState == 1) {
                bool free = st.valid && st.playerState == Game::STATE_FREE && !st.inBattle;
                if (!free) freeSince = 0;
                else if (!freeSince) freeSince = now;
                else if (now - freeSince > 4000) {
                    char mode[16] = { 0 };
                    File f;
                    if (File::Open(f, "mmo_dev_menu.txt", File::READ) == 0) { f.Read(mode, sizeof(mode) - 1); f.Close(); }
                    devState = 2;
                    Ui::StartDemo(!strncmp(mode, "idiomas", 7) ? 1 : 0);
                    PluginMenu::ForceOpen();
                    Log::Write("menu: prueba automatica (%s)", !strncmp(mode, "idiomas", 7) ? "idiomas" : "secciones");
                }
            }
        }
        SkinTex::Update(now);              // skin perfecta (color en la textura real del personaje)
        if (net.IsConnected()) {
            static u64 nextStats = 0, nextBattle = 0, lastBattleSend = 0;
            static bool battleSent = false;
            static Stats::Battle lastBt = {};
            Stats::Mine m0 = Stats::GetMine();
            // al arrancar/título: nada que mandar. v16.1: tampoco antes de saber el género (modelo correcto)
            if (now >= nextStats && App::GenderKnown() && (m0.okMisc || m0.okParty || m0.okDex)) {
                nextStats = now + 10000;
                Stats::Mine m = m0;
                PlayerPacket p;
                FillCommon(p, MMO_PKT_STATS);
                u8 *b = (u8 *)p.msg;
                b[0] = (u8)m.hours; b[1] = (u8)(m.hours >> 8); b[2] = m.minutes; b[3] = m.badges;
                b[4] = (u8)m.caught; b[5] = (u8)(m.caught >> 8); b[6] = (u8)m.seen; b[7] = (u8)(m.seen >> 8);
                b[8] = (u8)m.money; b[9] = (u8)(m.money >> 8); b[10] = (u8)(m.money >> 16); b[11] = (u8)(m.money >> 24);
                b[12] = m.partyCount; b[13] = m.avgLevel; b[14] = m.maxLevel; b[15] = (u8)m.leadSpecies; b[16] = (u8)(m.leadSpecies >> 8);
                b[17] = (u8)((m.okTime ? 1 : 0) | (m.okDex ? 2 : 0) | (m.okParty ? 4 : 0) | (m.okMisc ? 8 : 0) | (m.league ? 0x10 : 0));
                net.Queue(p);
            }
            Stats::Battle bt = Stats::GetBattle();
            // v17.0 r3: en cuanto cambia algo (otro Pokémon, PS) y como mucho cada 150 ms; si nada cambia, cada 1 s
            // para que el panel de tus amigos siga abierto. Antes iba fijo cada 400 ms (más 300 ms de lectura).
            bool btChanged = bt.kind != lastBt.kind || bt.mySpecies != lastBt.mySpecies || bt.myLevel != lastBt.myLevel ||
                             bt.myHp != lastBt.myHp || bt.myHpMax != lastBt.myHpMax || bt.opSpecies != lastBt.opSpecies ||
                             bt.opLevel != lastBt.opLevel || bt.opHp != lastBt.opHp || bt.opHpMax != lastBt.opHpMax;
            if ((bt.kind && now >= nextBattle && (btChanged || now - lastBattleSend >= 1000)) || (!bt.kind && battleSent)) {
                nextBattle = now + 150;
                lastBattleSend = now;
                lastBt = bt;
                battleSent = bt.kind != 0;
                PlayerPacket p;
                FillCommon(p, MMO_PKT_BATTLE);
                u8 *b = (u8 *)p.msg;
                b[0] = bt.kind;
                b[1] = (u8)bt.mySpecies; b[2] = (u8)(bt.mySpecies >> 8); b[3] = bt.myLevel;
                b[4] = (u8)bt.myHp; b[5] = (u8)(bt.myHp >> 8); b[6] = (u8)bt.myHpMax; b[7] = (u8)(bt.myHpMax >> 8);
                b[8] = (u8)bt.opSpecies; b[9] = (u8)(bt.opSpecies >> 8); b[10] = bt.opLevel;
                b[11] = (u8)bt.opHp; b[12] = (u8)(bt.opHp >> 8); b[13] = (u8)bt.opHpMax; b[14] = (u8)(bt.opHpMax >> 8);
                net.Queue(p);
            }
        }
        static u64 nextDiag = 0;
        if (now >= nextDiag) {
            nextDiag = now + 30000;
            Log::Write("diag: valid=%d zona=%d pos=(%.1f,%.1f,%.1f) vel=%.1f mov=%d estado=%d combate=%d cam=0x%08lX con=%d jug=%d",
                       st.valid, st.zone, st.x, st.y, st.z, st.speed, st.moveState, st.playerState, st.inBattle,
                       (unsigned long)Game::CameraAddr(), App::IsConnected(), MMO::g_players.ActiveCount());
            Perf::Window pw = Perf::TakeWindow();
            char net[96] = "";
            MMO::g_players.NetSummary(net, sizeof(net));
            Log::Write("perf: fps=%lu picos=%lu peor=%lums nivel=%d red[%s]",
                       (unsigned long)(pw.ms ? pw.frames * 1000 / pw.ms : 0), (unsigned long)pw.spikes, (unsigned long)pw.worstMs,
                       Perf::Level(), net);
            {
                static u32 lastCap = 0; static u64 lastTk = 0;
                u32 cap; u64 tk;
                DepthProbe::Stats(cap, tk);
                if (cap != lastCap)
                    Log::Write("profundidad: %lu lecturas en 30 s, %.2f ms de media", (unsigned long)(cap - lastCap),
                               (double)(tk - lastTk) / (double)(cap - lastCap) / (double)(SYSCLOCK_ARM11 / 1000));
                lastCap = cap; lastTk = tk;
            }
        }
        prevTick = now;
        svcSleepThread(33 * 1000000LL);
    }
    SkinTex::RestoreAll();
    net.Disconnect();
    net.ShutdownSoc();
}

// =============================================================================================
// Menús
// =============================================================================================
static void QueueSimple(u8 type, u8 anim, u8 btype, const char *msg = nullptr) {
    PlayerPacket p;
    FillCommon(p, type);
    p.anim = anim;
    p.btype = btype;
    if (msg) Proto::CopyText(p.msg, sizeof(p.msg), msg);
    App::QueuePacket(p);
}

// Para el menú propio (Ui.cpp)
void UiSendSimple(u8 type, u8 anim, u8 btype, const char *msg) { QueueSimple(type, anim, btype, msg); }

// =============================================================================================
namespace CTRPluginFramework {

// v17: antes de que el juego arranque (CTRPF lo llama con el juego aún parado): el idioma del juego tiene que estar
// enganchado antes de que EventLangSelect lo ponga al arrancar (GameLang.hpp).
void PatchProcess(FwkSettings &) {
    GameLang::Early();
}

int main(void) {
    Log::Init();
    Log::Write("ORAS MMO %s iniciando", MMO_VERSION_STR);
    Cfg::Load();
    GameLang::LogStatus();
    Lang::Init();
    // Fase 2: el gancho se instala UNA vez al arrancar (KB §12.8). v16.1: siempre (si la build del juego no es la
    // estudiada, Install() lo detecta y no engancha nada)
    GameHook::Install();
    DepthProbe::Install();                 // v17: profundidad de la escena (tejados delante de los acompañantes)
    Overlay::Init();
    Follower3D::Reserve();                 // v16.3: su espacio fijo, ahora que la memoria está entera (256 KB seguidos)

    PluginMenu *menu = new PluginMenu("Pokemon ORAS MMO " MMO_VERSION_STR, 15, 0, 0,
                                      "Multijugador en tiempo real para Rubi Omega / Zafiro Alfa.");
    menu->SynchronizeWithFrame(true);
    menu->OnOpening = Ui::OnMenuHotkey;      // SELECT abre el menú propio del MMO

    // v17: sin entradas: el menú de CTRPF ya no se abre (SELECT abre el propio del MMO, Ui.cpp)

    OSD::Run(Overlay::OnFrame);

    // Prioridad 0x31: una por debajo del hilo principal del juego (0x30, exheader de ORAS).
    // Con 0x30 ambos se turnaban en el núcleo 0 de Azahar (KB §12.3).
    Thread netThread = threadCreate(NetworkThread, nullptr, 0x8000, 0x31, -2, false);
    Thread assetThread = threadCreate(AssetThread, nullptr, 0x10000, 0x3F, -2, false);

    menu->Run();

    s_running = false;
    if (netThread) { threadJoin(netThread, 3000000000ULL); threadFree(netThread); }
    if (assetThread) { threadJoin(assetThread, 3000000000ULL); threadFree(assetThread); }
    delete menu;
    return 0;
}

} // namespace CTRPluginFramework
