/*
 * App.hpp — ORAS MMO v13
 * Estado compartido entre el hilo de red, el hilo de carga, el OSD (hilo del juego) y los menús.
 * Todo lo mutable entre hilos pasa por funciones con mutex o por tipos atómicos simples.
 */
#pragma once
#include <3ds.h>
#include "Protocol.hpp"

namespace App {

// ---- conexión
bool  IsConnected();
u8    MyId();
void  SetConnection(bool connected, u8 id);
void  RequestReconnect();           // desde el menú
bool  TakeReconnectRequest();       // hilo de red
void  SetStatus(const char *txt);   // texto corto de estado ("Conectando...", "sala llena")
void  GetStatus(char *out, size_t cap);

// ---- paquetes salientes de la interfaz (chat, emotes, invitaciones)
void  QueuePacket(const PlayerPacket &p);
bool  PopPacket(PlayerPacket &out);

// ---- registro de chat (últimas líneas para el HUD)
void  PushChat(const char *text, u32 rgb);
int   GetChat(char lines[][48], u32 *colors, u64 *ages, int max, u64 now);

// ---- mi propio bocadillo (lo que acabo de decir, sobre mi personaje)
void  SetMyBubble(const char *text, u64 now);
bool  GetMyBubble(char *out, size_t cap, u64 now);   // false si no hay o ya caducó

// ---- invitaciones (combate / intercambio)
struct Invite { bool pending; u8 from; u8 type; char name[14]; };
void  SetInvite(const Invite &inv);
Invite GetInvite();
void  ClearInvite();
struct Outgoing { bool pending; u8 to; u8 type; };    // invitación que YO envié
void  SetOutgoing(const Outgoing &o);
Outgoing GetOutgoing();
void  ClearOutgoing();

// ---- recursos 3D
enum AssetState { ASSETS_LOADING = 0, ASSETS_OK, ASSETS_FAILED };
int   GetAssetState();
void  SetAssetState(int st, const char *err);
void  GetAssetError(char *out, size_t cap);

// ---- modelo del jugador local (171 May / 172 Brendan) según el juego o la elección del menú
u16   LocalModelId();
u16   GameModelId();                 // el modelo que de verdad carga el juego (género de la partida)
void  SetDetectedFemale(int female);   // -1 desconocido, 0 chico, 1 chica
bool  GenderKnown();                   // v16.1: ya se leyó (o se dio por chico tras 60 s)

// ---- utilidades de texto
void  AsciiFold(const char *utf8, char *out, size_t cap);   // "Villa Raíz" -> "Villa Raiz" (fuente del OSD)
const char *ChatText(u8 id);                                // mensajes rápidos 1..8
extern const char *const kQuickChats[8];
extern const char *const kEmotes[5];
extern const char *const kEmoteIcons[5];

} // namespace App
