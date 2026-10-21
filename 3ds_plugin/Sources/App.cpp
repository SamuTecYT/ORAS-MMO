#include "App.hpp"
#include "Config.hpp"
#include <CTRPluginFramework.hpp>
#include <cstring>
#include <cstdio>

using namespace CTRPluginFramework;

namespace App {

const char *const kQuickChats[8] = {
    "Hola a todos!", "Estoy aqui!", "Vamos a batallar!", "Sigueme!",
    "Mira este Pokemon!", "Intercambiamos?", "GG!", "Hasta luego!"
};
const char *const kEmotes[5] = { "Saludar", "Saltar", "Corazon", "Pregunta", "Exclamacion" };
const char *const kEmoteIcons[5] = { "(^o^)/", "\\(^_^)/", "<3", "(?)", "(!)" };

static Mutex s_mutex;
static volatile bool s_connected = false;
static volatile u8   s_myId = 0;
static volatile bool s_reconnect = false;
static char  s_status[48] = "Iniciando...";

static PlayerPacket s_q[16];
static int  s_qHead = 0, s_qCount = 0;

struct ChatLine { char text[48]; u32 rgb; u64 time; };
static ChatLine s_chat[4];
static int  s_chatCount = 0;

static Invite s_invite = {};
static Outgoing s_outgoing = {};

static volatile int s_assetState = ASSETS_LOADING;
static char s_assetErr[64] = "";
static volatile int s_female = -1;

bool IsConnected() { return s_connected; }
u8 MyId() { return s_myId; }
void SetConnection(bool c, u8 id) { Lock l(s_mutex); s_connected = c; s_myId = c ? id : 0; }
void RequestReconnect() { s_reconnect = true; }
bool TakeReconnectRequest() { bool r = s_reconnect; s_reconnect = false; return r; }

void SetStatus(const char *t) { Lock l(s_mutex); snprintf(s_status, sizeof(s_status), "%s", t ? t : ""); }
void GetStatus(char *o, size_t cap) { Lock l(s_mutex); snprintf(o, cap, "%s", s_status); }

void QueuePacket(const PlayerPacket &p) {
    Lock l(s_mutex);
    if (s_qCount == 16) { s_qHead = (s_qHead + 1) % 16; s_qCount--; }   // descarta el más viejo
    s_q[(s_qHead + s_qCount) % 16] = p;
    s_qCount++;
}

bool PopPacket(PlayerPacket &out) {
    Lock l(s_mutex);
    if (!s_qCount) return false;
    out = s_q[s_qHead];
    s_qHead = (s_qHead + 1) % 16;
    s_qCount--;
    return true;
}

void PushChat(const char *text, u32 rgb) {
    Lock l(s_mutex);
    if (s_chatCount == 4) { memmove(&s_chat[0], &s_chat[1], sizeof(ChatLine) * 3); s_chatCount = 3; }
    ChatLine &c = s_chat[s_chatCount++];
    AsciiFold(text, c.text, sizeof(c.text));
    c.rgb = rgb;
    c.time = osGetTime();
}

int GetChat(char lines[][48], u32 *colors, u64 *ages, int max, u64 now) {
    Lock l(s_mutex);
    int n = 0;
    int start = s_chatCount > max ? s_chatCount - max : 0;     // las más recientes, en orden
    for (int i = start; i < s_chatCount && n < max; i++) {
        memcpy(lines[n], s_chat[i].text, 48);
        colors[n] = s_chat[i].rgb;
        s64 age = (s64)(now - s_chat[i].time);
        ages[n] = age > 0 ? (u64)age : 0;
        n++;
    }
    return n;
}

static char s_myBubble[24] = "";
static u64  s_myBubbleTime = 0;
void SetMyBubble(const char *text, u64 now) { Lock l(s_mutex); snprintf(s_myBubble, sizeof(s_myBubble), "%s", text ? text : ""); s_myBubbleTime = now; }
bool GetMyBubble(char *out, size_t cap, u64 now) {
    Lock l(s_mutex);
    if (!s_myBubble[0] || (s64)(now - s_myBubbleTime) > 6000) return false;
    snprintf(out, cap, "%s", s_myBubble);
    return true;
}

void SetInvite(const Invite &inv) { Lock l(s_mutex); s_invite = inv; }
Invite GetInvite() { Lock l(s_mutex); return s_invite; }
void ClearInvite() { Lock l(s_mutex); s_invite.pending = false; }
void SetOutgoing(const Outgoing &o) { Lock l(s_mutex); s_outgoing = o; }
Outgoing GetOutgoing() { Lock l(s_mutex); return s_outgoing; }
void ClearOutgoing() { Lock l(s_mutex); s_outgoing.pending = false; }

int GetAssetState() { return s_assetState; }
void SetAssetState(int st, const char *err) {
    Lock l(s_mutex);
    snprintf(s_assetErr, sizeof(s_assetErr), "%s", err ? err : "");
    s_assetState = st;
}
void GetAssetError(char *o, size_t cap) { Lock l(s_mutex); snprintf(o, cap, "%s", s_assetErr); }

void SetDetectedFemale(int f) { s_female = f; }
bool GenderKnown() { return s_female >= 0; }
u16 LocalModelId() {
    return (s_female == 1) ? MMO_MODEL_MAY : MMO_MODEL_BRENDAN;
}

u16 GameModelId() { return (s_female == 1) ? MMO_MODEL_MAY : MMO_MODEL_BRENDAN; }

const char *ChatText(u8 id) { return (id >= 1 && id <= 8) ? kQuickChats[id - 1] : "..."; }

// UTF-8 -> ASCII imprimible (la fuente del OSD solo tiene ASCII)
void AsciiFold(const char *s, char *out, size_t cap) {
    if (!cap) return;
    size_t o = 0;
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    while (*p && o + 1 < cap) {
        unsigned c = *p;
        if (c < 0x80) { out[o++] = (c >= 32 && c < 127) ? (char)c : ' '; p++; continue; }
        // secuencias de 2 bytes de Latin-1
        if ((c & 0xE0) == 0xC0 && p[1]) {
            unsigned cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
            char r = '?';
            if ((cp >= 0xC0 && cp <= 0xC5)) r = 'A'; else if (cp >= 0xE0 && cp <= 0xE5) r = 'a';
            else if (cp >= 0xC8 && cp <= 0xCB) r = 'E'; else if (cp >= 0xE8 && cp <= 0xEB) r = 'e';
            else if (cp >= 0xCC && cp <= 0xCF) r = 'I'; else if (cp >= 0xEC && cp <= 0xEF) r = 'i';
            else if (cp >= 0xD2 && cp <= 0xD6) r = 'O'; else if (cp >= 0xF2 && cp <= 0xF6) r = 'o';
            else if (cp >= 0xD9 && cp <= 0xDC) r = 'U'; else if (cp >= 0xF9 && cp <= 0xFC) r = 'u';
            else if (cp == 0xD1) r = 'N'; else if (cp == 0xF1) r = 'n';
            else if (cp == 0xBF || cp == 0xA1) { p += 2; continue; }   // ¿ ¡
            else if (cp == 0xE9) r = 'e';
            out[o++] = r;
            p += 2;
            continue;
        }
        // otros (3-4 bytes): saltar la secuencia
        int len = (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 1;
        for (int i = 0; i < len && *p; i++) p++;
        out[o++] = '?';
    }
    out[o] = 0;
}

} // namespace App
