/*
 * Config.hpp — ORAS MMO v13
 * Ajustes persistentes del jugador. Todo se guarda en archivos de texto junto al plugin
 * (carpeta de trabajo de CTRPF, normalmente /luma/plugins/<TitleID>/).
 *
 *  - Servidor: el de MMO_DEFAULT_SERVER (Protocol.hpp; 127.0.0.1 en el código público, o el de
 *    ServerConfig.local.hpp). Si el jugador elige otro en el menú, o escribe mmo_server.txt ("IP" o
 *    "IP:puerto"), se usa ese. El antiguo mmo_ip.txt de v12.6 se ignora a propósito.
 *  - Sala (mmo_room.txt), nombre (mmo_name.txt) y opciones visuales (mmo_settings.txt).
 */
#pragma once
#include <3ds.h>
#include <string>

namespace Cfg {
    enum CharacterChoice : int { CHAR_AUTO = 0, CHAR_MAY = 1, CHAR_BRENDAN = 2 };

    void        Load();
    void        Save();

    std::string ServerIp();          // IP efectiva (manual o la automática)
    int         ServerPort();        // puerto efectivo (el de mmo_server.txt "IP:puerto" o MMO_DEFAULT_PORT)
    bool        HasManualIp();
    void        SetManualIp(const std::string &ip);   // "IP" o "IP:puerto"; "" = volver a la automática
    std::string Room();
    void        SetRoom(const std::string &room);
    std::string Name();
    void        SetName(const std::string &name);

    // Opciones (lectura/escritura directa desde el hilo del menú; el resto solo las lee)
    extern bool showHud;        // (compatibilidad) = hudMode != 2
    extern int  hudMode;        // 0 automático (unos segundos al pasar algo; el juego queda en HD), 1 siempre, 2 nunca
    extern int  mapMode;        // mapa de amigos en la pantalla táctil: 0 automático (con el AreaNav), 1 siempre, 2 nunca
    extern bool showNames;
    extern bool occlusion;      // reservado (se guarda para compatibilidad; sin uso por ahora)
    extern int  character;      // CharacterChoice
    extern int  colorVariant;   // 0..15 color de la ropa roja (0 = original; nombres y tintes en SkinColors.hpp)
    extern int  gameLang;       // v17: idioma del juego y del menú (GameLang.hpp): 0 automático, 1 ja, 2 en, 3 fr, 4 it, 5 de, 7 es, 8 ko, 9 ja kanji
    extern int  accColor;       // v16.5: gorro (Bruno) / lazo (Aura): 0 como la ropa, 1 original, 2..16 color 1..15
    extern int  tagColor;       // 0..15 color de la placa de tu nombre (0 = el de tu número de jugador)
    extern bool showMySkin;     // verte a ti mismo con tu personalización (si no es la original)
    extern int  followers;      // Pokémon acompañante: 0 el de todos, 1 solo el mío, 2 ninguno
    extern int  perfMode;       // v16: 0 automático, 1 calidad, 2 ahorro
    extern bool nativeMode;     // v16: funciones con el motor del juego (amigos como modelos reales). Se aplica al reiniciar
    void        ApplyPerf();    // pasa perfMode a Perf::SetForced
    extern int  origGender;     // género ORIGINAL de la partida si el plugin lo cambió (-1 = no se ha tocado)
    extern bool hdCanvas;       // dibujar sin quitar el HD del juego (lienzo + copias de la GPU)
    extern bool hideMe;         // (retirado del menú; siempre false)
    static const int NUM_COLORS = 16;   // = SkinColors::COUNT
    static const int NUM_ACC    = 17;   // = SkinColors::ACC_COUNT
    static const int NUM_TAGS   = 16;   // colores de la placa del nombre (= SkinColors::TAG_COUNT)
}
