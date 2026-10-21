#include "GameHook.hpp"
#include "DepthProbe.hpp"
#include "Lang.hpp"
#include "GameFont.hpp"
#include "GameMaps.hpp"
#include "Perf.hpp"
#include "Overlay.hpp"
#include "App.hpp"
#include "Config.hpp"
#include "GameState.hpp"
#include "Players.hpp"
#include "Render3D.hpp"
#include "SkinColors.hpp"
#include "HeroAssets.hpp"
#include "TownMap.hpp"
#include "Emotes.hpp"
#include "Species.hpp"
#include "SkinTex.hpp"
#include "GameStats.hpp"
#include "Follower3D.hpp"
#include "FriendMotion.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include "Log.hpp"

using namespace CTRPluginFramework;

namespace Overlay {

// Color fijo por jugador (1..12) para nombres y pines
static const u8 kColors[12][3] = {
    {255, 214, 64}, {90, 200, 255}, {255, 120, 120}, {140, 255, 140}, {255, 150, 255}, {255, 170, 70},
    {160, 160, 255}, {120, 255, 230}, {255, 255, 150}, {200, 140, 255}, {255, 200, 200}, {180, 230, 120}
};

struct AvatarState {
    bool  init;
    int   anim, prevAnim;
    float frame, prevFrame, blend;
    float lastPhase;
    u64   lastT, nextBlink, blinkUntil;
};
static AvatarState s_av[MAX_MMO_PLAYERS];
static const Screen *s_real = nullptr;     // framebuffer real (para LEER lo que muestra el juego)
static MMO::RemoteView s_views[MAX_MMO_PLAYERS];
static FriendMotion::State s_mot[MAX_MMO_PLAYERS];   // v16.1: físicas de cada amigo (mismas que su modelo real)
static u64 s_frameCount = 0;

static uint64_t TickNow() { return svcGetSystemTick(); }

bool Init() {
    memset(s_av, 0, sizeof(s_av));
    R3D::SetClock(TickNow, SYSCLOCK_ARM11);
    return R3D::Init();
}

// ---------------------------------------------------------------------------------------------
// Framebuffer de la 3DS: vertical de 240 px por columna (pantalla rotada). Píxel (x,y) en horizontal:
//   dirección = base + x * Stride + (239 - y) * BytesPerPixel
// Se usa GetFramebuffer() si responde; si no, se calcula con los campos de Screen.
bool MakeTarget(const Screen &s, R3D::Target &t) {
    u8 *p00 = s.GetFramebuffer(0, 0), *p10 = s.GetFramebuffer(1, 0), *p01 = s.GetFramebuffer(0, 1);
    int fmt = (int)s.Format & 7;
    int bpp = (int)s.BytesPerPixel;
    if (p00 && p10 && p01) {
        t.base = p00;
        t.dx = (int)(p10 - p00);
        t.dy = (int)(p01 - p00);
    } else if (s.LeftFramebuffer && s.Stride && bpp >= 2 && bpp <= 4) {
        t.base = (u8 *)(s.LeftFramebuffer + 239 * bpp);
        t.dx = (int)s.Stride;
        t.dy = -bpp;
    } else {
        return false;
    }
    t.fmt = fmt;
    t.w = s.IsTop ? 400 : 320;
    t.h = 240;
    t.track = false;
    return fmt <= 4;
}

void DevShot(const Screen &s, const char *path) {
    R3D::Target t;
    if (!MakeTarget(s, t) || t.w <= 0 || t.h <= 0 || t.w > 400) return;
    File f(path, File::RWC | File::TRUNCATE);
    if (!f.IsOpen()) return;
    const int W = t.w, H = t.h, row = (W * 3 + 3) & ~3;
    u8 hd[54] = { 'B', 'M' };
    u32 v[13] = { (u32)(54 + row * H), 0, 54, 40, (u32)W, (u32)H, 1u | (24u << 16), 0, (u32)(row * H), 2835, 2835, 0, 0 };
    memcpy(hd + 2, v, sizeof(v));
    f.Write(hd, 54);
    static u8 line[400 * 3 + 4];
    for (int y = H - 1; y >= 0; y--) {                        // BMP: de abajo arriba, BGR
        memset(line, 0, sizeof(line));
        for (int x = 0; x < W; x++) {
            const u8 *p = t.base + x * t.dx + y * t.dy;
            int r = 0, g = 0, b = 0;
            switch (t.fmt) {
                case R3D::FB_RGBA8: r = p[3]; g = p[2]; b = p[1]; break;
                case R3D::FB_BGR8: r = p[2]; g = p[1]; b = p[0]; break;
                case R3D::FB_RGB565: { u16 c = (u16)(p[0] | (p[1] << 8)); r = (c >> 11) << 3; g = ((c >> 5) & 63) << 2; b = (c & 31) << 3; break; }
                case R3D::FB_RGB5A1: { u16 c = (u16)(p[0] | (p[1] << 8)); r = (c >> 11) << 3; g = ((c >> 6) & 31) << 3; b = ((c >> 1) & 31) << 3; break; }
                default: { u16 c = (u16)(p[0] | (p[1] << 8)); r = (c >> 12) << 4; g = ((c >> 8) & 15) << 4; b = ((c >> 4) & 15) << 4; break; }
            }
            line[x * 3] = (u8)b; line[x * 3 + 1] = (u8)g; line[x * 3 + 2] = (u8)r;
        }
        f.Write(line, row);
    }
}

static inline int Clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void Box(const Screen &s, int x, int y, int w, int h, const Color &c, bool filled = true) {
    int W = s.IsTop ? 400 : 320;
    int x0 = Clampi(x, 0, W - 1), y0 = Clampi(y, 0, 239);
    int x1 = Clampi(x + w, 0, W), y1 = Clampi(y + h, 0, 240);
    if (x1 - x0 <= 0 || y1 - y0 <= 0) return;
    s.DrawRect((u32)x0, (u32)y0, (u32)(x1 - x0), (u32)(y1 - y0), c, filled);
}

static int TextW(const char *ascii) { return (int)strlen(ascii) * 6; }

static void Text(const Screen &s, const char *ascii, int x, int y, const Color &fg) {
    int W = s.IsTop ? 400 : 320;
    if (y < 0 || y > 230 || x > W - 6) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "%s", ascii);
    const char *p = buf;
    while (x < 0 && *p) { x += 6; p++; }
    size_t maxChars = (size_t)((W - x) / 6);
    if (strlen(p) > maxChars) buf[(p - buf) + maxChars] = 0;
    if (!*p) return;
    s.Draw(p, (u32)x, (u32)y, fg, Color(0, 0, 0, 0));
}

// v17: la letra pequeña solo tiene letras latinas. En japonés/coreano, los nombres de lugares y "Tú" van con la
// fuente del propio juego (GameFont, 16 px de alto); si aún no está lista, en inglés con la letra pequeña.
static bool CjkOsd() { return Lang::Cjk() && GameFont::Active(); }
static bool HighB(const char *t) { for (; t && *t; t++) if ((u8)*t >= 0x80) return true; return false; }
static bool Big(const char *utf8) { return CjkOsd() && HighB(utf8); }
static int  OsdW(const char *utf8) {
    if (Big(utf8)) return GameFont::Width(utf8);
    char a[96]; App::AsciiFold(utf8, a, sizeof(a)); return TextW(a);
}
static int  OsdH(const char *utf8) { return Big(utf8) ? 16 : 10; }   // alto de la línea
static void OsdText(const Screen &s, const char *utf8, int x, int y, const Color &fg) {   // y = arriba de la línea
    if (Big(utf8)) { GameFont::Draw(s, utf8, x, y, fg, s.IsTop ? 400 : 320); return; }
    char a[96]; App::AsciiFold(utf8, a, sizeof(a)); Text(s, a, x, y, fg);
}
static const char *ZoneOsd(u16 zone) { return CjkOsd() ? TownMap::ZoneName(zone) : TownMap::ZoneNameOsd(zone); }

static void Label(const Screen &s, const char *ascii, int cx, int y, const Color &fg, const Color &bg) {
    int w = TextW(ascii) + 6;
    Box(s, cx - w / 2, y - 1, w, 11, bg);
    Text(s, ascii, cx - w / 2 + 3, y, fg);
}

// v16: qué bici lleva (bit del paquete) y su modelo para dibujarla junto al personaje
static inline bool BikeIsAcro(u8 flags) { return ((flags & MMO_FLAG_BIKE_ALT) != 0) == (MMO_FORM_ACRO == 2); }
static const Hero::Model *BikeFor(int anim) {
    if (anim == Hero::ANIM_MACH_IDLE || anim == Hero::ANIM_MACH_RIDE) return Hero::Get(Hero::MODEL_MACH);
    if (anim == Hero::ANIM_ACRO_IDLE || anim == Hero::ANIM_ACRO_RIDE) return Hero::Get(Hero::MODEL_ACRO);
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Animación de cada avatar (cuál, en qué cuadro, mezcla y parpadeo)
static void UpdateAnim(AvatarState &st, const MMO::RemoteView &v, const Hero::Model &M, u64 now) {
    float dt = st.init ? (float)(s64)(now - st.lastT) / 1000.0f : 0.0f;
    if (dt < 0) dt = 0;
    if (dt > 0.1f) dt = 0.1f;
    st.lastT = now;
    if (!st.init) {
        st.init = true; st.anim = Hero::ANIM_IDLE; st.prevAnim = -1; st.frame = 0; st.prevFrame = 0; st.blend = 1;
        st.lastPhase = v.animPhase; st.nextBlink = now + 1500 + (v.id * 377) % 2500;
    }
    // Elección por velocidad real (con histéresis)
    float sp = v.speed;
    int target;
    if (v.state == MOVE_BIKE) {                          // v16: sobre su bici (pedalea si avanza)
        bool acro = BikeIsAcro(v.flags);
        target = (sp > 8.f) ? (acro ? Hero::ANIM_ACRO_RIDE : Hero::ANIM_MACH_RIDE) : (acro ? Hero::ANIM_ACRO_IDLE : Hero::ANIM_MACH_IDLE);
    }
    else if (v.state == MOVE_RUN) target = (sp > 8.f) ? Hero::ANIM_RUN : Hero::ANIM_IDLE;
    else if (sp < 6.f) target = Hero::ANIM_IDLE;
    else if (sp > (st.anim == Hero::ANIM_RUN ? 85.f : 105.f)) target = Hero::ANIM_RUN;
    else target = Hero::ANIM_WALK;
    if (st.anim == Hero::ANIM_WALK && target == Hero::ANIM_IDLE && sp >= 3.f) target = Hero::ANIM_WALK;

    if (target != st.anim) {
        st.prevAnim = st.anim; st.prevFrame = st.frame;
        st.anim = target; st.blend = 0;
        if (target == Hero::ANIM_IDLE) st.frame = 0;
    }
    st.blend += dt / 0.18f;
    if (st.blend > 1) { st.blend = 1; st.prevAnim = -1; }

    // Avance del ciclo según la distancia REAL recorrida (los pies no patinan)
    float dPhase = v.animPhase - st.lastPhase;
    st.lastPhase = v.animPhase;
    if (dPhase < 0 || dPhase > 200.f) dPhase = 0;
    int N = M.animFrames[st.anim];
    if (st.anim == Hero::ANIM_IDLE || M.animStride[st.anim] <= 0) st.frame += dt * 30.f;
    else st.frame += dPhase / M.animStride[st.anim] * (float)N;
    if (N > 0) st.frame = fmodf(st.frame, (float)N);
    if (st.prevAnim >= 0) {
        int Np = M.animFrames[st.prevAnim];
        st.prevFrame += dt * 30.f;
        if (Np > 0) st.prevFrame = fmodf(st.prevFrame, (float)Np);
    }
    // Parpadeo natural cada 2.5 - 5 s
    if (now >= st.nextBlink) {
        st.blinkUntil = now + 110;
        st.nextBlink = now + 2500 + ((now * 7919 + v.id * 104729) % 2500);
    }
}

// ---------------------------------------------------------------------------------------------
struct OvStats { int frames, views, same, front, makeFail; u64 folTicks; int folDraws; };
static OvStats s_ov;
static u64 s_hudUntil = 0;

void PokeHud() { s_hudUntil = osGetTime() + 5000; }

// Colores de etiqueta elegibles (0 = el automático de cada número de jugador; SkinColors.hpp, 16)
static void TagRGB(u8 id, u8 tag, u8 out[3]) {
    const u8 *c = (tag >= 1 && tag < SkinColors::TAG_COUNT) ? SkinColors::kTagRgb[tag] : kColors[(id - 1) % 12];
    out[0] = c[0]; out[1] = c[1]; out[2] = c[2];
}

static void DiscO(const Screen &s, int cx, int cy, int r, const Color &c) {
    for (int dy = -r; dy <= r; dy++) {
        int half = (int)sqrtf((float)(r * r - dy * dy) + 0.5f);
        Box(s, cx - half, cy + dy, 2 * half + 1, 1, c);
    }
}

// Etiqueta de nombre redondeada del color del jugador (como las del juego de referencia)
static void NamePill(const Screen &s, const char *ascii, int cx, int y, const u8 c[3]) {
    int w = TextW(ascii) + 10, h = 12;
    int x = Clampi(cx - w / 2, 2, 398 - w);
    Color edge(18, 20, 30, 235);
    Color fill((u8)(c[0] * 3 / 4), (u8)(c[1] * 3 / 4), (u8)(c[2] * 3 / 4), 240);
    Box(s, x + 2, y, w - 4, h, edge);
    Box(s, x, y + 2, w, h - 4, edge);
    Box(s, x + 1, y + 1, w - 2, h - 2, edge);
    Box(s, x + 2, y + 1, w - 4, h - 2, fill);
    Box(s, x + 1, y + 2, w - 2, h - 4, fill);
    int luma = (c[0] * 3 + c[1] * 6 + c[2]) / 10;
    Text(s, ascii, x + 5, y + 1, luma > 175 ? Color(16, 18, 28, 255) : Color(255, 255, 255, 255));
}

// Burbuja blanca con colita hacia abajo; bottomY = punta de la colita. Devuelve el borde superior.
static int BubbleFrame(const Screen &s, int cx, int bottomY, int w, int h, int &x, int &y) {
    x = Clampi(cx - w / 2, 2, 398 - w);
    y = bottomY - 5 - h;
    Color edge(30, 34, 48, 255), fill(250, 250, 255, 255);
    Box(s, x + 2, y, w - 4, h, edge);
    Box(s, x, y + 2, w, h - 4, edge);
    Box(s, x + 1, y + 1, w - 2, h - 2, edge);
    Box(s, x + 2, y + 1, w - 4, h - 2, fill);
    Box(s, x + 1, y + 2, w - 2, h - 4, fill);
    int tx = Clampi(cx, x + 6, x + w - 7);
    for (int i = 0; i < 5; i++) {                       // colita
        int half = 4 - i;
        Box(s, tx - half - 1, y + h - 1 + i, 2 * half + 3, 1, edge);
        if (half > 0) Box(s, tx - half, y + h - 1 + i, 2 * half + 1, 1, fill);
    }
    return y;
}

static void SpeechBubble(const Screen &s, const char *utf8, int cx, int bottomY) {
    char a[32];
    App::AsciiFold(utf8, a, sizeof(a));
    int tw = TextW(a);
    int x, y;
    BubbleFrame(s, cx, bottomY, tw + 12, 14, x, y);
    Text(s, a, x + 6, y + 2, Color(24, 30, 52, 255));
}

// Emotes: pixel art propio (Emotes.cpp) ampliado x2 dentro de la burbuja
static void PutRect(void *user, int x, int y, int size, unsigned char r, unsigned char g, unsigned char b) {
    Box(*(const Screen *)user, x, y, size, size, Color(r, g, b, 255));
}
static void EmoteBubble(const Screen &s, int e, int cx, int bottomY) {
    int x, y;
    BubbleFrame(s, cx, bottomY, 30, 30, x, y);
    if (y < 0) return;
    Emotes::Draw(e, x + 3, y + 3, 2, PutRect, (void *)&s);
}

static void Bubble(const Screen &s, const char *b, int cx, int bottomY) {
    if (b[0] == '\x01') EmoteBubble(s, (b[1] >= 'A' && b[1] < 'A' + Emotes::COUNT) ? b[1] - 'A' : 0, cx, bottomY);
    else SpeechBubble(s, b, cx, bottomY);
}

// Mi propio avatar con mi personalización (encima del modelo del juego) — animación por distancia real
static AvatarState s_myAv;
static float s_myPhase = 0.f, s_myLastX = 0.f, s_myLastY = 0.f, s_myYaw = 0.f;
static bool  s_myInit = false;

static bool BuildMyInstance(const Game::LocalState &me, u64 now, R3D::Instance &in) {
    if (!Cfg::showMySkin || Cfg::colorVariant == 0 || App::GetAssetState() != App::ASSETS_OK) { s_myInit = false; return false; }
    // Tu color se aplica SOLO en la textura real del personaje (SkinTex): sin desfase. Ya no se dibuja encima.
    s_myInit = false;
    return false;
    // Poses que no tenemos (bici, escenas, diálogos): se ve el modelo original del juego, sin superponer nada
    // (y un margen tras bajar de la bici: el modelo del juego tarda un instante en volver a la pose normal)
    static u64 rideUntil = 0;
    if (me.ride != 0 || me.speed > 150.f) rideUntil = now + 600;
    if (me.ride != 0 || me.playerState != Game::STATE_FREE || now < rideUntil) { s_myInit = false; return false; }
    u16 mid = App::LocalModelId();
    const Hero::Model *M = Hero::Get(mid == MMO_MODEL_MAY ? Hero::MODEL_MAY : Hero::MODEL_BRENDAN);
    if (!M) return false;
    float dx = me.x - s_myLastX, dy = me.y - s_myLastY, d = sqrtf(dx * dx + dy * dy);
    if (!s_myInit) { d = 0; s_myYaw = me.yaw; s_myInit = true; }
    if (d > 0.05f && d < 90.f) { s_myPhase += d; s_myYaw = atan2f(dx, dy); }     // giro real (stick analógico)
    else if (d == 0.f && me.moveState == MOVE_IDLE) s_myYaw = me.yaw;
    s_myLastX = me.x; s_myLastY = me.y;
    MMO::RemoteView v;
    memset(&v, 0, sizeof(v));
    v.id = 13; v.speed = me.speed; v.state = me.moveState; v.animPhase = s_myPhase;
    v.flags = me.ride == 2 ? MMO_FLAG_BIKE_ALT : 0;
    UpdateAnim(s_myAv, v, *M, now);
    memset(&in, 0, sizeof(in));
    in.model = M;
    in.pos = V3(me.x, me.y, me.z);
    in.yaw = s_myYaw;
    in.anim = s_myAv.anim; in.frame = s_myAv.frame;
    in.prevAnim = s_myAv.prevAnim; in.prevFrame = s_myAv.prevFrame; in.blend = s_myAv.blend;
    in.blink = now < s_myAv.blinkUntil;
    in.id = 13;
    in.variant = (u8)(Cfg::colorVariant < SkinColors::COUNT ? Cfg::colorVariant : 0);
    in.acc = (u8)Cfg::accColor;
    in.attach = BikeFor(s_myAv.anim);
    in.urgent = true;                                   // pegado al modelo del juego en cada cuadro
    return true;
}

// ---------------------------------------------------------------------------------------------
// Pokémon acompañante (v16.1: mejor diseño y con físicas). Sigue las huellas de su entrenador con un muelle
// amortiguado (arranca y frena con suavidad, sin quedarse atrás ni pasarse), nunca se mete encima de nadie
// (entrenadores ni otros Pokémon), al andar da saltitos que lo aplastan al caer y lo estiran al subir, quieto
// respira y mira a su entrenador, y salta de alegría con los emotes Feliz, Corazón y Risa. v16.2: el dibujo es un
// render del MODELO 3D del propio juego (Follower3D) en 8 direcciones, con la luz del ambiente.
struct Trail {
    bool  init;
    u16   zone;
    float hx[48], hy[48], hz[48];   // huellas del líder (la más nueva en head)
    int   head, count;
    float fx, fy, fz;               // posición del acompañante
    float vx, vy;                   // su velocidad (unidades/s)
    float tx, ty;                   // su objetivo del cuadro anterior (velocidad del camino)
    float px, py;                   // posición del cuadro anterior (para saber hacia dónde mira)
    float lx, ly;                   // última posición del líder
    float phase;                    // distancia recorrida (saltitos)
    bool  right;                    // mira a la derecha (el ícono original mira a la izquierda)
    s8    oct;                      // v16.2: dirección respecto a la cámara, 0 = de frente ... 4 = de espaldas (x45°)
    float radW;                     // v16.2: medio ancho del dibujo en unidades del mundo (para apartarse lo justo)
    u16   radSp;                    // especie a la que corresponde radW
    u64   joyAt;                    // inicio del salto de alegría (0 = ninguno)
    bool  bubbleOn;                 // su entrenador tenía burbuja el cuadro anterior
    s8    side;                     // lado al que se aparta si quedaría escondido detrás (+1 / -1)
    s8    place;                    // v17.0 r2: cuadros que quedan para buscarle sitio libre al aparecer (0 = ya está)
    bool  zHid;                     // v17.0 r2: el cuadro anterior estaba casi entero detrás de algo (no se pintó)
};
static Trail s_trail[MAX_MMO_PLAYERS + 1];   // 0 = yo, i + 1 = jugador remoto i
static const float FOLLOW_GAP = 22.f;        // algo más de un paso detrás
static const float HERO_H = 33.5f;           // altura del protagonista en unidades del mundo
static const float SPRING_W = 11.f;          // rigidez del muelle (1/s): llega a su sitio en ~0,3 s sin pasarse
static const float POKE_SPACE = 13.f;        // espacio personal con los entrenadores...
static const float POKE_POKE = 12.f;         // ...y con otros Pokémon
static const u32   JOY_MS = 700;             // salto de alegría: dos botes
static const float TRAINER_HALF = 7.f;       // medio ancho del entrenador (unidades del mundo)
// Ejes de la cámara sobre el suelo (de su matriz): derecha de la pantalla y "hacia el fondo" (más lejos)
static bool  s_camAxes = false;
static float s_camRight[2] = { 1.f, 0.f }, s_camFwd[2] = { 0.f, -1.f };

// ---- v17.0 r2: ¿cabe el Pokémon ahí? -----------------------------------------------------------------------------
// No se conoce el mapa de choques del juego, pero sí la profundidad REAL de la escena (DepthProbe): en el punto de la
// pantalla donde cae el suelo de (x, y) a la altura z, si la escena está bastante MÁS CERCA de la cámara que ese suelo,
// algo lo ocupa o lo tapa (pared, tejado, seto, árbol, valla, otro modelo del juego); si está bastante MÁS LEJOS, el
// suelo de ahí es más bajo (escalón, agua, barranco). Vale para todas las estructuras de todos los mapas.
static const float GROUND_NEAR = 9.f;        // media casilla: flores y hierba baja no cuentan, un seto o una pared sí
static const float GROUND_FAR = 12.f;
static const s8    PLACE_FRAMES = 8;          // al aparecer: ~0,25 s como mucho buscando sitio (sin dibujarlo)
static const Game::CameraState *s_cam = nullptr;   // la cámara de este cuadro (DrawAvatars)
static bool s_zFresh = false;                       // hay profundidad capturada en este cuadro (o el anterior)
static void WantDepth(int x0, int y0, int x1, int y1, int M = 12);

// 1 = suelo libre a la vista, 0 = no se sabe (fuera de lo capturado o el juego no escribe profundidad ahí, como el suelo
// del Centro Pokémon), -1 = ocupado
static int GroundAt(float x, float y, float z) {
    if (!s_cam || !s_zFresh) return 0;
    float sx, sy, w;
    if (!Game::Project(*s_cam, V3(x, z, y), sx, sy, w)) return 0;
    u32 d;
    if (!DepthProbe::At((int)(sx + 0.5f), (int)(sy + 0.5f), d) || d >= 0xFFFFFF) return 0;
    u32 nearT = R3D::DepthThreshold(s_cam->viewProj, V3(x, z, y), GROUND_NEAR);
    if (nearT && d < nearT) return -1;
    u32 farT = R3D::DepthThreshold(s_cam->viewProj, V3(x, z, y), -GROUND_FAR);
    if (farT && d > farT) return -1;
    return 1;
}

// La huella del Pokémon con los pies en (x, y): el centro y sus dos lados (a lo ancho de la pantalla, que es por donde
// se aparta). -1 = algo la ocupa, 1 = libre, 0 = no se sabe.
static int Footprint(const Trail &T, float x, float y, float z) {
    float hw = (T.radW > 0.f ? T.radW : 12.f) * 0.6f;
    if (hw < 4.f) hw = 4.f;
    if (hw > 14.f) hw = 14.f;
    int r0 = GroundAt(x, y, z);
    if (r0 < 0 || !s_camAxes) return r0;
    int r1 = GroundAt(x + s_camRight[0] * hw, y + s_camRight[1] * hw, z);
    if (r1 < 0) return -1;
    int r2 = GroundAt(x - s_camRight[0] * hw, y - s_camRight[1] * hw, z);
    if (r2 < 0) return -1;
    return (r0 > 0 || r1 > 0 || r2 > 0) ? 1 : 0;
}

// Zona de la pantalla con los suelos que se van a mirar: se pide a DepthProbe para el cuadro siguiente
struct GroundBox {
    int x0 = 9999, y0 = 9999, x1 = -9999, y1 = -9999;
    void Add(float x, float y, float z, float r) {
        float sx, sy, w;
        if (!s_cam || !Game::Project(*s_cam, V3(x, z, y), sx, sy, w)) return;
        int m = 4 + (int)(r * 0.8f);                 // a lo ancho de la huella (en la pantalla, ~1 px por unidad)
        if ((int)sx - m < x0) x0 = (int)sx - m;
        if ((int)sy - 4 < y0) y0 = (int)sy - 4;
        if ((int)sx + m > x1) x1 = (int)sx + m;
        if ((int)sy + 4 > y1) y1 = (int)sy + 4;
    }
    void Want() const { if (x1 > x0) WantDepth(x0, y0, x1, y1, 6); }   // puntos del suelo: poco margen (lo que se mueve la cámara)
};

// Punto de las huellas a 'dist' unidades del líder (x, y, z), recorriéndolas hacia atrás
static void TrailPoint(const Trail &T, float x, float y, float z, float dist, float &tx, float &ty, float &tz) {
    float cx = x, cy = y, cz = z, need = dist;
    for (int k = 0; k < T.count; k++) {
        int i = (T.head - k + 48) % 48;
        float sx = T.hx[i] - cx, sy = T.hy[i] - cy, d = sqrtf(sx * sx + sy * sy);
        if (d >= need && d > 1e-3f) {
            float f = need / d;
            tx = cx + sx * f; ty = cy + sy * f; tz = cz + (T.hz[i] - cz) * f;
            return;
        }
        need -= d; cx = T.hx[i]; cy = T.hy[i]; cz = T.hz[i];
    }
    tx = cx; ty = cy; tz = cz;
}

static void TrailReset(Trail &T, float x, float y, float z, float yaw, u16 zone) {
    T.init = true; T.zone = zone;
    T.fx = x - sinf(yaw) * FOLLOW_GAP; T.fy = y - cosf(yaw) * FOLLOW_GAP; T.fz = z;
    T.hx[0] = T.fx; T.hy[0] = T.fy; T.hz[0] = z;
    T.hx[1] = x;    T.hy[1] = y;    T.hz[1] = z;
    T.head = 1; T.count = 2;
    T.vx = T.vy = 0.f; T.tx = T.fx; T.ty = T.fy;
    T.px = T.fx; T.py = T.fy; T.lx = x; T.ly = y; T.phase = 0.f; T.joyAt = 0;
    if (!(T.radW > 0.f)) T.radW = 12.f;
    T.place = PLACE_FRAMES; T.zHid = false;
}

// v17.0 r2: al aparecer (zona nueva, puerta, salto grande) se pone en el primer sitio LIBRE alrededor de su
// entrenador: detrás, en diagonal detrás, a los lados, en diagonal delante y delante. Antes aparecía siempre detrás, y al
// salir por una puerta "detrás" es dentro de la casa. Mientras busca (unos cuadros) no se dibuja.
static void PlaceFollower(Trail &T, float x, float y, float z, float yaw) {
    static const float kAng[8] = { 3.14159265f, 2.35619449f, -2.35619449f, 1.57079633f, -1.57079633f, 0.78539816f, -0.78539816f, 0.f };
    GroundBox b;
    int pick = -1, unk = -1;
    float px[8], py[8];
    for (int k = 0; k < 8; k++) {
        px[k] = x + sinf(yaw + kAng[k]) * FOLLOW_GAP; py[k] = y + cosf(yaw + kAng[k]) * FOLLOW_GAP;
        b.Add(px[k], py[k], z, T.radW);
        if (pick >= 0) continue;
        int f = Footprint(T, px[k], py[k], z);
        if (f > 0) pick = k;
        else if (f == 0 && unk < 0) unk = k;
    }
    b.Want();
    if (--T.place > 0 && pick < 0) return;              // aún sin profundidad de esos sitios: el cuadro siguiente
    if (pick < 0) pick = unk >= 0 ? unk : 0;
    T.place = 0;
    T.fx = px[pick]; T.fy = py[pick]; T.fz = z;
    T.hx[0] = T.fx; T.hy[0] = T.fy; T.hz[0] = z;
    T.hx[1] = x;    T.hy[1] = y;    T.hz[1] = z;
    T.head = 1; T.count = 2;
    T.vx = T.vy = 0.f; T.tx = T.fx; T.ty = T.fy; T.px = T.fx; T.py = T.fy;
    static u64 nextLog = 0;
    u64 now = osGetTime();
    if (now >= nextLog) { nextLog = now + 3000; Log::Write("acomp: aparece en el sitio %d (%s)", pick, unk == pick ? "sin datos" : "libre"); }
}

static void TrailStep(Trail &T, float x, float y, float z, float yaw, u16 zone, float dt) {
    float jx = x - T.lx, jy = y - T.ly;
    if (!T.init || (zone != T.zone && !TownMap::SameSpace(zone, T.zone)) || jx * jx + jy * jy > 90.f * 90.f) { TrailReset(T, x, y, z, yaw, zone); return; }
    float lsp = dt > 0.f ? sqrtf(jx * jx + jy * jy) / dt : 0.f;     // velocidad del líder
    T.lx = x; T.ly = y; T.zone = zone;
    if (T.place > 0) { PlaceFollower(T, x, y, z, yaw); return; }
    float dx = x - T.hx[T.head], dy = y - T.hy[T.head];
    if (dx * dx + dy * dy >= 9.f) {
        T.head = (T.head + 1) % 48;
        T.hx[T.head] = x; T.hy[T.head] = y; T.hz[T.head] = z;
        if (T.count < 48) T.count++;
    }
    // punto del camino a FOLLOW_GAP del líder, recorriendo las huellas hacia atrás
    float tx, ty, tz;
    TrailPoint(T, x, y, z, FOLLOW_GAP, tx, ty, tz);
    // v16.1: justo DETRÁS de su entrenador (visto desde la cámara) quedaría escondido o "en su cara": se aparta a un
    // lado, en diagonal, como en los juegos con acompañante.
    // v16.2: lo justo para que su dibujo NO toque al entrenador (medio entrenador + medio Pokémon + margen): antes
    // eran 16 unidades fijas y un Pokémon ancho (Salamence) seguía con media cara dentro del hueco que lo tapaba.
    // También cuando va DELANTE (al subir por la pantalla): si no, su dibujo tapaba las piernas del entrenador.
    if (s_camAxes) {
        float gap = TRAINER_HALF + (T.radW > 0.f ? T.radW : 12.f) + 2.f;
        if (gap > 42.f) gap = 42.f;
        float rx = tx - x, ry = ty - y;
        float along = rx * s_camFwd[0] + ry * s_camFwd[1], side = rx * s_camRight[0] + ry * s_camRight[1];
        if (fabsf(along) > 2.f && fabsf(side) < gap) {
            if (fabsf(side) > 2.f) T.side = side > 0 ? 1 : -1;
            else if (!T.side) T.side = 1;
            // v17.0 r2: solo a un lado con SUELO LIBRE (Footprint): el de siempre, si no el otro, y si no, algo menos
            // apartado. Sin sitio a ningún lado (pasillo, junto a una casa) sigue tus huellas, un poco más atrás para no
            // taparte. Antes se apartaba hasta 2 casillas sin mirar y se metía en casas, setos y árboles.
            static const float kFrac[4] = { 1.f, 1.f, 0.6f, 0.6f };
            GroundBox b;
            bool placed = false;
            for (int c = 0; c < 4; c++) {
                int sg = (c & 1) ? -T.side : T.side;
                float push = sg * gap * kFrac[c] - side;
                float cx = tx + s_camRight[0] * push, cy = ty + s_camRight[1] * push;
                if (c < 2) b.Add(cx, cy, tz, T.radW);         // la profundidad de los dos lados, siempre
                if (placed || Footprint(T, cx, cy, tz) < 0) continue;
                T.side = (s8)sg;
                tx = cx; ty = cy;
                placed = true;
            }
            b.Want();
            if (!placed) {
                float extra = 8.f + 0.8f * (T.radW > 0.f ? T.radW : 12.f);
                if (extra > 30.f) extra = 30.f;
                TrailPoint(T, x, y, z, FOLLOW_GAP + extra, tx, ty, tz);
            }
        }
    }
    T.px = T.fx; T.py = T.fy;
    // muelle con amortiguación crítica + la velocidad del propio camino (así no se queda atrás en bici)
    float tvx = dt > 0.f ? (tx - T.tx) / dt : 0.f, tvy = dt > 0.f ? (ty - T.ty) / dt : 0.f;
    T.tx = tx; T.ty = ty;
    // v17.0 r2: en pasos de 1/30 s como mucho. Con un cuadro lento (una carga, un tirón, Android) un solo paso grande
    // hacía que el muelle se pasara de un lado a otro: el Pokémon temblaba y cambiaba de dirección cada cuadro.
    int ns = (int)(dt / 0.034f) + 1;
    if (ns > 4) ns = 4;
    float h = dt / ns, vmax = lsp * 1.5f + 80.f;
    for (int k = 0; k < ns; k++) {
        float ax = SPRING_W * SPRING_W * (tx - T.fx) + 2.f * SPRING_W * (tvx - T.vx);
        float ay = SPRING_W * SPRING_W * (ty - T.fy) + 2.f * SPRING_W * (tvy - T.vy);
        T.vx += ax * h; T.vy += ay * h;
        float sp = sqrtf(T.vx * T.vx + T.vy * T.vy);
        if (sp > vmax && sp > 0.f) { T.vx *= vmax / sp; T.vy *= vmax / sp; }
        T.fx += T.vx * h; T.fy += T.vy * h;
    }
    T.fz += (tz - T.fz) * (1.f - expf(-dt * 12.f));
    float mx = T.fx - T.px, my = T.fy - T.py;
    T.phase += sqrtf(mx * mx + my * my);
}

// Salto de alegría cuando su entrenador manda Feliz, Corazón o Risa (una vez por burbuja nueva)
static void TrailReact(Trail &T, const char *bubble, u64 now) {
    bool on = bubble && bubble[0];
    if (on && !T.bubbleOn && bubble[0] == '\x01' && bubble[1] >= 'A' && bubble[1] <= 'C') T.joyAt = now;
    T.bubbleOn = on;
}

// Empuja (x, y) hasta quedar a minD de (ox, oy); share = la parte del empujón que le toca (0.5 entre dos Pokémon)
static void PushOut(float &x, float &y, float ox, float oy, float minD, float share) {
    float dx = x - ox, dy = y - oy, d = sqrtf(dx * dx + dy * dy);
    if (!(d < minD)) return;
    if (d < 0.01f) { dx = 0.f; dy = 1.f; d = 1.f; }
    float push = (minD - d) * share;
    x += dx / d * push; y += dy / d * push;
}

// ---- Luz del ambiente para los acompañantes (v16.2) ------------------------------------------------------------
// Una vez por zona (1,2 s después de entrar, caminando libre) se miran ~150 puntos de la imagen del juego (fuera del
// centro, donde estáis tú y tu Pokémon): el percentil 85 de la luminancia dice cuánta luz hay (una cueva o el fondo
// del mar dan poca, un Centro Pokémon mucha) y el color medio de esas zonas claras, su tono (agua azul, lava
// naranja...). El dibujo del Pokémon se multiplica por eso (suave: 28 % de tono). Leer la pantalla en Azahar obliga
// a bajarla de la GPU, así que se hace UNA vez por zona y con lecturas de 32 bits.
static int   s_tint[3] = { 256, 256, 256 };            // luz aplicada (256 = 1.0)
static float s_tintCur[3] = { 1.f, 1.f, 1.f }, s_tintGoal[3] = { 1.f, 1.f, 1.f };
static u16   s_probeZone = 0xFFFF;
static u64   s_probeAt = 0;
static bool  s_probeDone = true;

static void ProbeLight(const R3D::Target &t, u16 zone, u64 now) {
    int lum[176];
    u8 cr[176], cg[176], cb[176];
    int n = 0;
    for (int gy = 0; gy < 11; gy++)
        for (int gx = 0; gx < 15; gx++) {
            int x = 14 + gx * 26, y = 10 + gy * 20;
            if (x > 128 && x < 272 && y > 46 && y < 206) continue;
            u8 r, g, b;
            R3D::GetPixelFast(t, x, y, r, g, b);
            cr[n] = r; cg[n] = g; cb[n] = b;
            lum[n] = (r * 77 + g * 151 + b * 28) >> 8;
            n++;
        }
    if (n < 20) return;
    int srt[176];
    memcpy(srt, lum, sizeof(int) * n);
    for (int i = 1; i < n; i++) { int v = srt[i], j = i - 1; while (j >= 0 && srt[j] > v) { srt[j + 1] = srt[j]; j--; } srt[j + 1] = v; }
    int L85 = srt[n * 85 / 100];
    if (L85 < 24) { s_probeAt = now + 1000; s_probeDone = false; return; }     // pantalla en negro: se repite luego
    int thr = L85 * 85 / 100, m = 0;
    long sr = 0, sg = 0, sb = 0;
    for (int i = 0; i < n; i++) if (lum[i] >= thr) { sr += cr[i]; sg += cg[i]; sb += cb[i]; m++; }
    if (!m) return;
    float hr = (float)sr / m, hg = (float)sg / m, hb = (float)sb / m;
    float mx = hr > hg ? (hr > hb ? hr : hb) : (hg > hb ? hg : hb);
    if (mx < 1.f) mx = 1.f;
    float bright = L85 / 205.f;
    bright = bright < 0.62f ? 0.62f : bright > 1.03f ? 1.03f : bright;
    s_tintGoal[0] = bright * (0.72f + 0.28f * hr / mx);
    s_tintGoal[1] = bright * (0.72f + 0.28f * hg / mx);
    s_tintGoal[2] = bright * (0.72f + 0.28f * hb / mx);
    Log::Write("luz: zona %u L85=%d brillo %.2f -> tinte %.2f/%.2f/%.2f", (unsigned)zone, L85, (double)bright,
               (double)s_tintGoal[0], (double)s_tintGoal[1], (double)s_tintGoal[2]);
}

static void UpdateLight(const R3D::Target &t, u16 zone, u64 now, float dt) {
    if (zone != s_probeZone) { s_probeZone = zone; s_probeAt = now + 1200; s_probeDone = false; }
    if (!s_probeDone && now >= s_probeAt) { s_probeDone = true; ProbeLight(t, zone, now); }
    float a = 1.f - expf(-dt * 3.f);                     // ~0,3 s para llegar al tono nuevo
    for (int i = 0; i < 3; i++) {
        s_tintCur[i] += (s_tintGoal[i] - s_tintCur[i]) * a;
        s_tint[i] = (int)(s_tintCur[i] * 256.f + 0.5f);
    }
}

// Dibuja el acompañante. holes = silueta del entrenador que va DELANTE (cabeza + cuerpo, nh rectángulos) o nullptr.
// v17: zonas de la pantalla donde se ha pintado algo que puede quedar tapado por la escena; al final del cuadro se piden
// a DepthProbe para el siguiente (con margen: en un cuadro se mueven unos pocos píxeles).
static DepthProbe::Rect s_wantR[16];          // DepthProbe toma 16 como mucho: con más, se juntan en el último
static int s_nWantR = 0;
static void WantDepth(int x0, int y0, int x1, int y1, int M) {
    if (x1 <= x0 || y1 <= y0) return;
    DepthProbe::Rect r;
    r.x0 = (s16)Clampi(x0 - M, 0, 400); r.y0 = (s16)Clampi(y0 - M, 0, 240);
    r.x1 = (s16)Clampi(x1 + M, 0, 400); r.y1 = (s16)Clampi(y1 + M, 0, 240);
    if (r.x1 <= r.x0 || r.y1 <= r.y0) return;
    if (s_nWantR < 16) { s_wantR[s_nWantR++] = r; return; }
    DepthProbe::Rect &L = s_wantR[15];
    if (r.x0 < L.x0) L.x0 = r.x0;
    if (r.y0 < L.y0) L.y0 = r.y0;
    if (r.x1 > L.x1) L.x1 = r.x1;
    if (r.y1 > L.y1) L.y1 = r.y1;
}

// v16.2: render del modelo 3D del juego (Follower3D) en 8 direcciones y con la luz del ambiente. Mientras carga su
// dibujo (unas décimas de segundo) no se pinta; sin paquete 3D se usa el ícono del juego.
static bool DrawFollower(const R3D::Target &t, const Game::CameraState &cam, Trail &T, u16 species, u64 now, const int *holes, int nh,
                         bool cheap) {
    float sx, sy, w, hx, hy, hw;
    if (!Game::Project(cam, V3(T.fx, T.fz, T.fy), sx, sy, w)) return false;
    if (!Game::Project(cam, V3(T.fx, T.fz + HERO_H, T.fy), hx, hy, hw)) return false;
    float hPx = sy - hy;
    if (!(hPx > 6.f && hPx < 300.f) || sx < -140 || sx > 540 || sy < -40 || sy > 380) return false;
    // hacia dónde mira, respecto a la cámara (8 direcciones): andando, hacia donde va; quieto, hacia su entrenador
    float mx = T.fx - T.px, my = T.fy - T.py;
    bool moving = mx * mx + my * my > 0.01f;
    float vx = 0.f, vy = 0.f;
    bool have = false;
    if (mx * mx + my * my > 0.04f) { vx = mx; vy = my; have = true; }
    else if (!moving) { vx = T.lx - T.fx; vy = T.ly - T.fy; have = vx * vx + vy * vy > 36.f; }
    if (have && s_camAxes) {
        float rc = vx * s_camRight[0] + vy * s_camRight[1], fc = vx * s_camFwd[0] + vy * s_camFwd[1];
        float ang = atan2f(rc, -fc) * 57.2957795f;          // 0 = hacia la cámara, +90 = a la derecha
        float diff = ang - (float)(T.oct * 45);
        while (diff > 180.f) diff -= 360.f;
        while (diff < -180.f) diff += 360.f;
        if (fabsf(diff) > 30.f) {                            // con margen: no tiembla entre dos direcciones
            int o = (int)floorf((ang + 22.5f) / 45.f);
            T.oct = (s8)(((o % 8) + 8) % 8);
        }
        if (rc > 0.05f) T.right = true;
        else if (rc < -0.05f) T.right = false;
    }
    float u = hPx / 33.3f;                                  // medida de los botes (la de antes)
    float sp = sqrtf(mx * mx + my * my);                   // unidades por cuadro
    float joyHop = 0.f, joySx = 0.f, joySy = 0.f;          // salto de alegría (emotes Feliz, Corazón, Risa)
    if (T.joyAt && now - T.joyAt < JOY_MS) {
        float q = (float)(now - T.joyAt) / (JOY_MS * 0.5f);
        float jh = sinf((q - floorf(q)) * 3.14159265f);   // dos botes
        joyHop = jh * 7.f * u;
        if (jh > 0.6f) { joySy = 0.06f; joySx = -0.04f; }
        else if (jh < 0.2f) { joySy = -0.07f; joySx = 0.05f; }
    } else T.joyAt = 0;
    // 1) el modelo 3D horneado, con paso animado (v16.2: 4 cuadros por ciclo con las patas de su esqueleto)
    static const s8 kDir[8] = { 0, 1, 2, 3, 4, 3, 2, 1 };
    int oc = T.oct & 7;
    const Follower3D::View *v = Follower3D::Begin(species, kDir[oc], now);
    if (v) {
        bool walks = Follower3D::Walks(v);
        float ref = (float)Follower3D::RefPx();
        // el ancho para apartarse es el MAYOR de las direcciones ya vistas de esa especie: si siguiera al dibujo de
        // cada cuadro, al girarse cambiaba su sitio, se movía, se volvía a girar... y temblaba de lado a lado quieto
        // (comprobado en vivo 2026-10-17: Salamence de lado 27 u, de 3/4 22 u)
        float r = Follower3D::RadiusPx(v) / ref * HERO_H;
        if (T.radSp != species) { T.radSp = species; T.radW = r; }
        else if (r > T.radW) T.radW = r;
        // v16.2 r6: ciclo de 8 cuadros (paquete nuevo; el viejo trae 4). Con patas avanza con lo recorrido: la zancada
        // viene horneada por especie (los pies no resbalan), con un tope de 2,8 ciclos por segundo para que los
        // pequeños no tiemblen al correr. El sube y baja del cuerpo ya viene en los cuadros nuevos.
        int nw = Follower3D::Frames() - 1;
        if (nw < 1) nw = 1;
        float cyc = Follower3D::Stride(v);
        if (!(cyc > 0.f)) { cyc = 14.f + 0.9f * T.radW; if (cyc < 16.f) cyc = 16.f; if (cyc > 40.f) cyc = 40.f; }
        float minCyc = sp * 30.f / 2.8f;                       // sp = unidades por cuadro (el juego va a 30)
        if (cyc < minCyc) cyc = minCyc;
        int loopMs = Follower3D::CycleMs(v);
        if (loopMs < 400) loopMs = 880;
        int frame = 0;
        float hop = 0.f, sqx = 1.f, sqy = 1.f;
        if (moving && walks) {
            float ph = fmodf(T.phase, cyc) / cyc;              // 0..1 dentro del ciclo
            frame = 1 + (int)(ph * nw) % nw;
            if (nw < 8) hop = fabsf(sinf(ph * 6.2831853f)) * (sp > 2.5f ? 0.35f : 0.55f) * u;   // paquete viejo
        } else if (walks) {
            float b = sinf((float)(now % 100000u) * 0.0042f);  // respira (~1,5 s por ciclo)
            sqy += 0.02f * b; sqx -= 0.01f * b;
        } else {
            // sin patas: su propio bucle de reposo (algo más rápido mientras se mueve) y flotan subiendo y bajando
            u32 per = (u32)loopMs / (u32)nw;
            if (moving) per = per * 2 / 3;
            if (per < 30) per = 30;
            frame = 1 + (int)((now / per) % (u32)nw);
            hop = (0.5f + 0.5f * sinf((float)(now % 100000u) * (moving ? 0.006f : 0.0035f))) * 0.9f * u;
        }
        hop += joyHop; sqx += joySx; sqy += joySy;
        R3D::Target tz = t;                                   // v17: lo que tenga delante en la escena lo tapa
        if (t.zbuf) tz.zthr = R3D::DepthThreshold(cam.viewProj, V3(T.fx, T.fz, T.fy), R3D::DEPTH_MARGIN);
        tz.zskip = T.zHid;                                    // v17.0 r2: casi entero detrás de algo: no se pinta nada
        bool ok = Follower3D::Draw(tz, v, frame, oc >= 5, hPx / ref, sx, sy - hop, sqx, sqy, s_tint, holes, nh, cheap);
        {
            int zt, zh;
            R3D::LastDepth(zt, zh);
            T.zHid = ok && R3D::MostlyHidden(zt, zh, T.zHid);
        }
        if (ok) { int a, b, c, d; Follower3D::LastRect(a, b, c, d); WantDepth(a, b, c, d); }
        Follower3D::End();
        static u64 nextLog = 0;
        if (now >= nextLog) {
            nextLog = now + 10000;
            Log::Write("acomp3d: especie %d dir %d%s cuadro %d %s escala %.2f (entrenador %.0f px) ancho/2 %.1f u tinte %d/%d/%d tapado %d",
                       species, kDir[oc], oc >= 5 ? " espejo" : "", frame, walks ? "patas" : "flota", (double)(hPx / ref), (double)hPx,
                       (double)T.radW, s_tint[0], s_tint[1], s_tint[2], holes ? 1 : 0);
        }
        return ok;
    }
    // v16.3: solo el modelo 3D (los íconos viejos ya no existen). Mientras llega (unas centésimas) no se dibuja nada.
    return false;
}

static bool DrawAvatars(const Screen &s, const Game::LocalState &me, const Game::CameraState &cam, u64 now) {
    R3D::Target t;
    if (!MakeTarget(s, t)) { s_ov.makeFail++; return false; }
    {   // v16.2: luz del ambiente (antes de pintar nada: se lee la imagen limpia del juego)
        static u64 lastL = 0;
        float dtl = lastL && now > lastL ? (now - lastL) / 1000.f : 0.033f;
        lastL = now;
        UpdateLight(t, me.zone, now, dtl > 0.1f ? 0.1f : dtl);
    }
    {   // v17: profundidad real de la escena (DepthProbe): capturada en este cuadro, o en el anterior como mucho
        static u32 lastFrame = 0;
        static int age = 99;
        u32 df = DepthProbe::Frame();
        if (df != lastFrame) { lastFrame = df; age = 0; } else if (age < 99) age++;
        DepthProbe::Rect r;
        if (age <= 1 && DepthProbe::Region(r)) {
            t.zbuf = DepthProbe::Buffer();
            t.zx0 = r.x0; t.zy0 = r.y0; t.zx1 = r.x1; t.zy1 = r.y1;
        }
        s_nWantR = 0;
        s_cam = &cam;                                        // v17.0 r2: para mirar si el suelo está libre (GroundAt)
        s_zFresh = t.zbuf != nullptr;
    }

    struct Item { int i; float w; };                     // i = jugador remoto, o -1 = yo
    Item items[MAX_MMO_PLAYERS + 1];
    int n = 0;
    for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
        const MMO::RemoteView &v = s_views[i];
        if (!v.valid) { s_av[i].init = false; continue; }
        s_ov.views++;
        if (v.flags & (MMO_FLAG_TRANSITION | MMO_FLAG_HIDDEN)) continue;
        if (!TownMap::SameSpace(v.zone, me.zone)) continue;
        s_ov.same++;
        float sx, sy, w;
        if (!Game::Project(cam, V3(v.x, v.z, v.y), sx, sy, w)) { items[n].i = i; items[n].w = -1; n++; continue; }
        s_ov.front++;
        items[n].i = i; items[n].w = w; n++;
    }
    R3D::Instance myIn;
    bool mine = BuildMyInstance(me, now, myIn);
    if (mine) {
        float sx, sy, w;
        if (Game::Project(cam, V3(me.x, me.z, me.y), sx, sy, w)) { items[n].i = -1; items[n].w = w; n++; }
        else mine = false;
    }
    char myBubble[24];
    bool hasMyBubble = App::GetMyBubble(myBubble, sizeof(myBubble), now);

    // Pokémon acompañantes: se mueven siempre y se dibujan encima de todo (de lejos a cerca)
    struct Fol { int trail; u16 species; float w, sx, sy; };
    Fol fol[MAX_MMO_PLAYERS + 1];
    int nf = 0;
    {
        static u64 lastMs = 0;
        float dt = lastMs && now > lastMs ? (now - lastMs) / 1000.f : 0.033f;
        if (dt > 0.1f) dt = 0.1f;
        lastMs = now;
        Stats::Mine mm = Stats::GetMine();
        {   // ejes de la cámara en el suelo: fila x (derecha) y fila w (profundidad) de la matriz
            const float *M = cam.viewProj.m;
            float rl = sqrtf(M[0] * M[0] + M[2] * M[2]), fl = sqrtf(M[12] * M[12] + M[14] * M[14]);
            s_camAxes = rl > 1e-6f && fl > 1e-6f;
            if (s_camAxes) { s_camRight[0] = M[0] / rl; s_camRight[1] = M[2] / rl; s_camFwd[0] = M[12] / fl; s_camFwd[1] = M[14] / fl; }
        }
        // v16.3: el modo ahorro ya NO oculta los Pokémon de los amigos (salían y desaparecían a ratos): se dibujan con
        // un suavizado más barato. Ocultarlos es una elección del jugador (Pantalla -> Acompañante: El mío / Ninguno).
        int followers = Cfg::followers;
        u16 sp[MAX_MMO_PLAYERS + 1] = {};             // especie de cada rastro activo este cuadro (0 = ninguno)
        if (followers != 2 && mm.okParty && mm.leadSpecies >= 1 && mm.leadSpecies <= 721) {
            TrailStep(s_trail[0], me.x, me.y, me.z, me.yaw, me.zone, dt);
            TrailReact(s_trail[0], hasMyBubble ? myBubble : nullptr, now);
            sp[0] = s_trail[0].place ? 0 : mm.leadSpecies;     // v17.0 r2: aún buscando sitio libre: no se dibuja
        } else s_trail[0].init = false;
        for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
            const MMO::RemoteView &v = s_views[i];
            Trail &T = s_trail[i + 1];
            bool ok = followers == 0 && v.valid && !(v.flags & (MMO_FLAG_TRANSITION | MMO_FLAG_HIDDEN | MMO_FLAG_IN_BATTLE)) &&
                      TownMap::SameSpace(v.zone, me.zone) && v.stats.valid && v.stats.lead >= 1 && v.stats.lead <= 721;
            if (!ok) { T.init = false; continue; }
            TrailStep(T, v.x, v.y, v.z, v.yaw, v.zone, dt);
            TrailReact(T, v.bubble, now);
            sp[i + 1] = T.place ? 0 : v.stats.lead;
        }
        // v16.1: espacio personal: ni encima de un entrenador (si este se da la vuelta) ni de otro Pokémon
        for (int a = 0; a <= MAX_MMO_PLAYERS; a++) {
            if (!sp[a]) continue;
            Trail &A = s_trail[a];
            PushOut(A.fx, A.fy, me.x, me.y, POKE_SPACE, 1.f);
            for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
                const MMO::RemoteView &v = s_views[i];
                if (v.valid && !(v.flags & (MMO_FLAG_TRANSITION | MMO_FLAG_HIDDEN)) && TownMap::SameSpace(v.zone, me.zone))
                    PushOut(A.fx, A.fy, v.x, v.y, POKE_SPACE, 1.f);
            }
            for (int b = a + 1; b <= MAX_MMO_PLAYERS; b++) {
                if (!sp[b]) continue;
                Trail &B = s_trail[b];
                float bx = B.fx, by = B.fy;
                PushOut(B.fx, B.fy, A.fx, A.fy, POKE_POKE, 0.5f);
                PushOut(A.fx, A.fy, bx, by, POKE_POKE, 0.5f);
            }
        }
        for (int a = 0; a <= MAX_MMO_PLAYERS; a++) {
            if (!sp[a]) continue;
            Trail &T = s_trail[a];
            Fol &f = fol[nf];
            if (!Game::Project(cam, V3(T.fx, T.fz, T.fy), f.sx, f.sy, f.w)) continue;
            f.trail = a; f.species = sp[a];
            nf++;
        }
        // de lejos a cerca
        for (int a = 0; a < nf; a++)
            for (int b = a + 1; b < nf; b++)
                if (fol[b].w > fol[a].w) { Fol tmp = fol[a]; fol[a] = fol[b]; fol[b] = tmp; }
    }
    if (!n && !hasMyBubble && !nf) { DepthProbe::Want(s_wantR, s_nWantR); s_cam = nullptr; return false; }
    // de lejos a cerca
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (items[b].w > items[a].w) { Item tmp = items[a]; items[a] = items[b]; items[b] = tmp; }

    bool assets = App::GetAssetState() == App::ASSETS_OK;
    R3D::DrawInfo info[MAX_MMO_PLAYERS];
    memset(info, 0, sizeof(info));
    R3D::Instance inst[MAX_MMO_PLAYERS + 1];
    R3D::DrawInfo dinfo[MAX_MMO_PLAYERS + 1];
    int owner[MAX_MMO_PLAYERS + 1];
    int m = 0;
    for (int k = 0; k < n; k++) {
        int i = items[k].i;
        if (i < 0) { inst[m] = myIn; owner[m++] = -1; continue; }
        const MMO::RemoteView &v = s_views[i];
        if (items[k].w <= 0 || v.id < 1 || v.id > R3D::MAX_SLOTS) continue;
        if (GameHook::IsNative(v.id)) continue;             // lo dibuja el propio juego
        const Hero::Model *M = assets ? Hero::Get(v.model == MMO_MODEL_MAY ? Hero::MODEL_MAY : Hero::MODEL_BRENDAN) : nullptr;
        if (!M && assets) M = Hero::Get(v.model == MMO_MODEL_MAY ? Hero::MODEL_BRENDAN : Hero::MODEL_MAY);   // el otro modelo
        if (!M) continue;
        AvatarState &st = s_av[i];
        UpdateAnim(st, v, *M, now);
        R3D::Instance &in = inst[m];
        memset(&in, 0, sizeof(in));
        in.model = M;
        in.pos = V3(v.x, v.y, v.z);
        in.yaw = v.yaw;
        in.anim = st.anim; in.frame = st.frame;
        in.prevAnim = st.prevAnim; in.prevFrame = st.prevFrame; in.blend = st.blend;
        in.blink = now < st.blinkUntil;
        in.id = v.id;
        in.variant = v.variant < SkinColors::COUNT ? v.variant : 0;
        in.acc = v.acc;
        in.attach = BikeFor(st.anim);
        owner[m++] = i;
    }
    bool drew = false;
    if (m) {
        R3D::DrawAll(t, inst, m, cam.viewProj, dinfo);
        for (int j = 0; j < m; j++) {
            if (owner[j] >= 0) info[owner[j]] = dinfo[j];
            drew |= dinfo[j].drawn;
            if (dinfo[j].drawn) WantDepth(dinfo[j].minX, dinfo[j].minY, dinfo[j].maxX + 1, dinfo[j].maxY + 1);
        }
    }
    // Acompañantes, de lejos a cerca. v16.1: si el Pokémon va DETRÁS de su entrenador (más lejos de la cámara), el
    // cuerpo del entrenador lo tapa (no se pinta en su silueta): antes se dibujaba siempre encima y quedaba en su cara.
    // v16.2: la silueta es cabeza + cuerpo (dos rectángulos ajustados), no un rectángulo ancho que borraba trozos del
    // Pokémon que estaban AL LADO del entrenador (la "cara cortada").
    for (int j = 0; j < nf; j++) {
        int hole[8];
        const int *hp = nullptr;
        int tr = fol[j].trail;
        bool have = false;
        float tx = 0, ty = 0, tz = 0;
        if (tr == 0) { tx = me.x; ty = me.y; tz = me.z; have = true; }
        else if (s_views[tr - 1].valid) { tx = s_views[tr - 1].x; ty = s_views[tr - 1].y; tz = s_views[tr - 1].z; have = true; }
        float fx, fy, fw, hx2, hy2, hw2;
        if (have && Game::Project(cam, V3(tx, tz, ty), fx, fy, fw) && Game::Project(cam, V3(tx, tz + HERO_H, ty), hx2, hy2, hw2) &&
            fol[j].w > fw + 0.5f) {
            float h = fy - hy2, neck = hy2 + h * 0.42f;
            hole[0] = (int)(fx - h * 0.21f); hole[1] = (int)(hy2 - 1); hole[2] = (int)(fx + h * 0.21f); hole[3] = (int)neck;   // cabeza
            hole[4] = (int)(fx - h * 0.15f); hole[5] = (int)neck;    hole[6] = (int)(fx + h * 0.15f); hole[7] = (int)(fy + 1); // cuerpo
            hp = hole;
        }
        u64 tf = svcGetSystemTick();
        drew |= DrawFollower(t, cam, s_trail[tr], fol[j].species, now, hp, 2, Perf::Level() >= 2);
        s_ov.folTicks += svcGetSystemTick() - tf; s_ov.folDraws++;
    }
    DepthProbe::Want(s_wantR, s_nWantR);                      // v17: profundidad de estas zonas en el cuadro siguiente
    s_cam = nullptr;                                          // (la cámara es de este cuadro)

    // Nombres, burbujas e indicadores de los que están fuera de la pantalla
    for (int k = 0; k < n; k++) {
        int i = items[k].i;
        if (i < 0) continue;
        const MMO::RemoteView &v = s_views[i];
        u8 c[3];
        TagRGB(v.id, v.tag, c);
        char name[32];
        App::AsciiFold(v.name, name, sizeof(name));
        float dist = sqrtf((v.x - me.x) * (v.x - me.x) + (v.y - me.y) * (v.y - me.y));
        float psx = 0, psy = 0, pw = 0;
        bool front = Game::Project(cam, V3(v.x, v.z, v.y), psx, psy, pw);
        bool inside = front && psx >= 0 && psx < 400 && psy >= 0 && psy < 240;
        bool native = GameHook::IsNative(v.id);
        bool onScreen = info[i].drawn || (!assets && inside) || (native && inside);
        if (onScreen) {
            int hx, hy;
            float nx, ny, nw;
            if (info[i].drawn) { hx = (int)info[i].headX; hy = (int)info[i].headY - 12; }
            else if (native && Game::Project(cam, V3(v.x, v.z + 36.f, v.y), nx, ny, nw)) { hx = (int)nx; hy = (int)ny - 12; }
            else {
                Box(s, (int)psx - 3, (int)psy - 6, 7, 7, Color(c[0], c[1], c[2], 255));    // marcador simple sin modelos
                hx = (int)psx; hy = (int)psy - 20;
            }
            int top = hy;
            if (Cfg::showNames) {
                char tag[64];
                char st[24] = "";
                if (v.flags & (MMO_FLAG_IN_BATTLE | MMO_FLAG_MENU))
                    App::AsciiFold(Lang::TOsd((v.flags & MMO_FLAG_IN_BATTLE) ? " [combate]" : " [menu]"), st, sizeof(st));
                snprintf(tag, sizeof(tag), "%s%s", name, st);
                NamePill(s, tag, hx, hy, c);
            }
            if (v.bubble[0]) Bubble(s, v.bubble, hx, top - 1);
            drew = true;
        } else {
            // Fuera de la pantalla: flecha en el borde con el nombre y la distancia.
            // La dirección sale de las coordenadas de recorte (válidas también detrás de la cámara).
            if (inside && assets) continue;                  // en pantalla pero tapado/fuera de rango del modelo
            float cc[4];
            M44Point(cam.viewProj, V3(v.x, v.z, v.y), cc);
            float dx = cc[0] * 200.f, dy = -cc[1] * 120.f;
            if (cc[3] < 0) { dx = -dx; dy = -dy; }
            float mm = fmaxf(fabsf(dx) / 180.f, fabsf(dy) / 100.f);
            if (!(mm > 1e-6f)) continue;
            int ex = (int)(200 + dx / mm), ey = (int)(120 + dy / mm);
            char tag[40];
            snprintf(tag, sizeof(tag), "%s %dm", name, (int)(dist / 18.f + 0.5f));
            NamePill(s, tag, Clampi(ex, 30, 370), Clampi(ey - 6, 26, 222), c);
            drew = true;
        }
    }
    // mi propia burbuja (lo que acabo de decir)
    if (hasMyBubble) {
        float sx, sy, w;
        if (Game::Project(cam, V3(me.x, me.z + 38.f, me.y), sx, sy, w) && sx > -40 && sx < 440 && sy > 0 && sy < 260) {
            Bubble(s, myBubble, (int)sx, (int)sy);
            drew = true;
        }
    }
    return drew;
}

// ---------------------------------------------------------------------------------------------
// Panel "cristal líquido" (v15): fondo desenfocado y oscurecido de lo que hay detrás, borde de luz,
// brillo arriba y esquinas redondeadas suavizadas. Todo por píxel sobre el lienzo (sigue en HD alrededor).
static inline u8 Mix8(int a, int b, int k256) { return (u8)(a + (((b - a) * k256) >> 8)); }

// Barra de PS redondeada con brillo; frac en 0..1 (ya animada)
static void HpBar2(const R3D::Target &t, int x, int y, int w, float frac, u64 now) {
    if (frac < 0.f) frac = 0.f;
    if (frac > 1.f) frac = 1.f;
    int f = (int)((w - 2) * frac + 0.5f);
    u8 cr, cg, cb;
    if (frac > 0.5f) { cr = 76; cg = 224; cb = 120; }
    else if (frac > 0.2f) { cr = 246; cg = 204; cb = 52; }
    else { bool on = ((now / 250) & 1) != 0; cr = on ? 255 : 220; cg = on ? 90 : 60; cb = on ? 90 : 60; }
    for (int yy = 0; yy < 5; yy++)
        for (int xx = 0; xx < w; xx++) {
            if ((xx == 0 || xx == w - 1) && (yy == 0 || yy == 4)) continue;   // puntas redondeadas
            int px = x + xx, py = y + yy;
            if (px < 0 || py < 0 || px >= t.w || py >= t.h) continue;
            bool fill = xx >= 1 && xx <= f && yy >= 1 && yy <= 3;
            if (fill) {
                int lift = yy == 1 ? 70 : 0;
                R3D::PutPixel(t, px, py, Mix8(cr, 255, lift), Mix8(cg, 255, lift), Mix8(cb, 255, lift));
            } else {
                u8 r0, g0, b0;
                R3D::GetPixel(t, px, py, r0, g0, b0);
                R3D::PutPixel(t, px, py, Mix8(r0, 6, 170), Mix8(g0, 8, 170), Mix8(b0, 14, 170));
            }
        }
}

static void ShadowText(const Screen &s, const char *txt, int x, int y, const Color &c) {
    Text(s, txt, x + 1, y + 1, Color(4, 6, 12, 255));
    Text(s, txt, x, y, c);
}

// v16.3: el Pokémon (su modelo 3D, cuadro quieto de 3/4) en una caja de 40x30. false = aún no está cargado.
static bool DrawPoke(const R3D::Target &t, u16 species, int x, int y, bool faceRight, u64 now) {
    return species >= 1 && species <= 721 && Follower3D::DrawFit(t, species, x, y, 40, 30, faceRight, now);
}

// Combate de un amigo cercano, en vivo (sin entrar): cristal arriba a la derecha que entra deslizándose,
// se amplía unos segundos cuando algo cambia y luego se queda compacto (los dos Pokémon y sus PS).
static bool DrawBattleWatch(const Screen &s, const Game::LocalState &me, u64 now) {
    static MMO::RemoteBattle B;
    static char who[24];
    static u8 col[3];
    static float appear = 0.f, expand = 1.f, dispA = 1.f, dispB = 1.f;
    static u64 last = 0, expandUntil = 0;
    static u16 lastHpA = 0xFFFF, lastHpB = 0xFFFF;
    static u8 watching = 0;
    float dt = last && now > last ? (now - last) / 1000.f : 0.033f;
    if (dt > 0.1f) dt = 0.1f;
    last = now;

    int best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
        const MMO::RemoteView &v = s_views[i];
        if (!v.valid || !(v.flags & MMO_FLAG_IN_BATTLE) || !v.battle.kind || v.battleAge > 3000) continue;
        if (!TownMap::SameSpace(v.zone, me.zone)) continue;
        float d = sqrtf((v.x - me.x) * (v.x - me.x) + (v.y - me.y) * (v.y - me.y));
        if (d < 300.f && d < bestD) { bestD = d; best = i; }
    }
    if (best >= 0) {
        const MMO::RemoteView &v = s_views[best];
        // v17.0 r3: sale otro Pokémon: su barra empieza en sus PS (no baja ni sube desde la del anterior) y se amplía
        if (v.battle.mySp != B.mySp) { dispA = v.battle.myHpMax ? (float)v.battle.myHp / v.battle.myHpMax : 0.f; expandUntil = now + 4000; }
        if (v.battle.opSp != B.opSp) { dispB = v.battle.opHpMax ? (float)v.battle.opHp / v.battle.opHpMax : 0.f; expandUntil = now + 4000; }
        if (v.id != watching || appear <= 0.f) {          // combate nuevo: entra ampliado
            watching = v.id; expandUntil = now + 5000; lastHpA = lastHpB = 0xFFFF;
            dispA = v.battle.myHpMax ? (float)v.battle.myHp / v.battle.myHpMax : 0.f;
            dispB = v.battle.opHpMax ? (float)v.battle.opHp / v.battle.opHpMax : 0.f;
        }
        B = v.battle;
        App::AsciiFold(v.name, who, sizeof(who));
        TagRGB(v.id, v.tag, col);
        if ((lastHpA != 0xFFFF && B.myHp != lastHpA) || (lastHpB != 0xFFFF && B.opHp != lastHpB)) expandUntil = now + 4000;
        lastHpA = B.myHp; lastHpB = B.opHp;
        appear += dt * 4.f;
    } else {
        appear -= dt * 4.f;
    }
    if (appear > 1.f) appear = 1.f;
    if (appear <= 0.f) { appear = 0.f; watching = 0; return false; }
    float wantE = now < expandUntil ? 1.f : 0.f;
    expand += (wantE - expand) * (1.f - expf(-dt * 10.f));
    float tA = B.myHpMax ? (float)B.myHp / B.myHpMax : 0.f, tB = B.opHpMax ? (float)B.opHp / B.opHpMax : 0.f;
    float step = dt * 0.9f;                                // los PS bajan/suben suavemente
    float dA = tA - dispA, dB = tB - dispB;
    dispA += fabsf(dA) < step ? dA : (dA > 0 ? step : -step);
    dispB += fabsf(dB) < step ? dB : (dB > 0 ? step : -step);

    R3D::Target t;
    if (!MakeTarget(s, t)) return false;
    float e = expand * expand * (3.f - 2.f * expand);     // suavizado
    float a = appear * appear * (3.f - 2.f * appear);
    int w = (int)(100 + 76 * e), h = (int)(38 + 22 * e);
    int x = 400 - 6 - w + (int)((1.f - a) * (w + 10)), y = 6;
    bool big = e > 0.92f, small = e < 0.08f;

    // El cristal, los Pokémon y las barras se dibujan en una imagen que solo se rehace cuando algo cambia
    // (o cada 200 ms para seguir el fondo). Cada cuadro solo se copia: casi no cuesta.
    static u16 img[176 * 60];
    static int cw = 0, chh = 0, cx = -999, cA = -1, cB = -1, cMode = -1;
    static u16 cSpA = 0, cSpB = 0;
    static bool cMissing = false;                           // algún Pokémon aún no estaba cargado: se rehace
    static u64 cTime = 0;
    int qa = (int)(dispA * 100), qb = (int)(dispB * 100), mode = big ? 2 : small ? 1 : 0;
    bool lowBlink = (dispA < 0.2f || dispB < 0.2f) && now - cTime >= 125;
    if (w != cw || h != chh || x != cx || qa != cA || qb != cB || mode != cMode || lowBlink || B.mySp != cSpA ||
        B.opSp != cSpB || (cMissing && now - cTime >= 50)) {
        cw = w; chh = h; cx = x; cA = qa; cB = qb; cMode = mode; cTime = now; cSpA = B.mySp; cSpB = B.opSp;
        R3D::Target bt;
        bt.base = (u8 *)img; bt.dx = 2; bt.dy = w * 2; bt.fmt = R3D::FB_RGB565; bt.w = w; bt.h = h; bt.track = false;
        for (int i = 0; i < w * h; i++) img[i] = 0;            // sin fondo: limpio
        bool okA = true, okB = true;
        if (big) {
            okA = DrawPoke(bt, B.mySp, 22, 13, true, now);
            okB = DrawPoke(bt, B.opSp, w - 62, 13, false, now);
            HpBar2(bt, 5, 53, w / 2 - 12, dispA, now);
            HpBar2(bt, w / 2 + 7, 53, w / 2 - 12, dispB, now);
        } else if (small) {
            okA = DrawPoke(bt, B.mySp, 4, 1, true, now);
            okB = DrawPoke(bt, B.opSp, w - 44, 1, false, now);
            HpBar2(bt, 6, 31, 38, dispA, now);
            HpBar2(bt, w - 44, 31, 38, dispB, now);
        }
        cMissing = !okA || !okB;
    }
    R3D::Blit565Key(t, img, w, h, x, y);
    if (a < 0.6f) return true;
    char line[48];
    if (big) {
        bool pulse = ((now / 500) & 1) != 0;
        Box(s, x + 7, y + 6, 5, 5, pulse ? Color(255, 70, 70, 255) : Color(200, 40, 40, 255));
        { char vs[24]; App::AsciiFold(Lang::TOsd(B.kind == 2 ? "vs entrenador" : "vs salvaje"), vs, sizeof(vs));
          snprintf(line, sizeof(line), "%s %s", who, vs); }
        ShadowText(s, line, x + 15, y + 4, Color(col[0], col[1], col[2], 255));
        ShadowText(s, "VS", x + w / 2 - 6, y + 24, Color(255, 225, 120, 255));
        { char sp[24]; App::AsciiFold(Species::Osd(B.mySp), sp, sizeof(sp)); snprintf(line, sizeof(line), "%.8s %u", sp, B.myLv); }
        ShadowText(s, line, x + 5, y + 42, Color(236, 242, 255, 255));
        { char sp[24]; App::AsciiFold(Species::Osd(B.opSp), sp, sizeof(sp)); snprintf(line, sizeof(line), "%.8s %u", sp, B.opLv); }
        ShadowText(s, line, x + w - 6 - TextW(line), y + 42, Color(255, 214, 214, 255));
    } else if (small) {
        ShadowText(s, "vs", x + w / 2 - 6, y + 12, Color(255, 225, 120, 255));
    }
    return true;
}

static void DrawHud(const Screen &s, const Game::LocalState &me, int online) {
    char l1[64], l2[64], st[48];
    bool conn = App::IsConnected();
    if (conn) {
        char nm[24];
        std::string myName = Cfg::Name();
        App::AsciiFold(myName.c_str(), nm, sizeof(nm));
        char fmt[48];
        App::AsciiFold(Lang::TOsd("MMO %s  %s  %d en linea"), fmt, sizeof(fmt));
        snprintf(l1, sizeof(l1), fmt, MMO_VERSION_STR, nm, online + 1);
    } else {
        App::GetStatus(st, sizeof(st));
        char fst[48];
        App::AsciiFold(st, fst, sizeof(fst));
        snprintf(l1, sizeof(l1), "MMO %s  %s", MMO_VERSION_STR, fst);
    }
    if (me.valid) snprintf(l2, sizeof(l2), "%s", ZoneOsd(me.zone));
    else l2[0] = 0;
    int w = TextW(l1);
    if (OsdW(l2) > w) w = OsdW(l2);
    w += 10;
    int h = l2[0] ? 13 + OsdH(l2) : 12;
    Box(s, 3, 3, w, h, Color(8, 12, 22, 165));
    Box(s, 3, 3, 2, h, conn ? Color(60, 230, 120, 255) : Color(240, 120, 40, 255));
    Text(s, l1, 8, 4, conn ? Color(150, 255, 170, 255) : Color(255, 200, 120, 255));
    if (l2[0]) OsdText(s, l2, 8, Big(l2) ? 14 : 15, Color(230, 230, 240, 255));
}

static bool DrawChat(const Screen &s, u64 now) {
    char lines[3][48]; u32 col[3]; u64 age[3];
    int n = App::GetChat(lines, col, age, 3, now);
    int y = 228;
    bool drew = false;
    for (int i = n - 1; i >= 0; i--) {
        if (age[i] > 7000) continue;
        int w = TextW(lines[i]) + 6;
        Box(s, 3, y - 1, w, 11, Color(8, 12, 22, 170));
        Text(s, lines[i], 6, y, Color((col[i] >> 16) & 255, (col[i] >> 8) & 255, col[i] & 255, 255));
        y -= 12;
        drew = true;
    }
    return drew;
}

static bool DrawTop(const Screen &s, u64 now) {
    DepthProbe::Want(nullptr, 0);                             // v17: DrawAvatars vuelve a pedir lo que necesite
    Game::LocalState me = Game::Snapshot();
    Game::RefreshFlags(me);
    Game::RefreshPos(me);
    s_ov.frames++;
    static u64 nextLog = 0;
    if (now >= nextLog) {
        nextLog = now + 10000;
        const R3D::Stats &g = R3D::g_stats;
        int fr = g.frames > 0 ? g.frames : 1;
        Log::Write("osd: fr=%d views=%d same=%d front=%d mkf=%d st=%d assets=%d | r3d: calls=%d rend=%d reuse=%d skip=%d off=%d behind=%d big=%d sinmodelo=%d sinmem=%d sincache=%d tris=%d/%d px=%d | ciclos/cuadro rast=%lu comp=%lu acomp=%lu (%d) presup=%dk lentos=%d",
                   s_ov.frames, s_ov.views, s_ov.same, s_ov.front, s_ov.makeFail, me.playerState, App::GetAssetState(),
                   g.calls, g.renders, g.reuses, g.skippedBudget, g.offscreen, g.behind, g.tooBig, g.noModel, g.noMemory, g.noCache, g.trisDrawn, g.trisIn, g.pixels,
                   (unsigned long)(g.ticksRender / fr), (unsigned long)(g.ticksComposite / fr),
                   (unsigned long)(s_ov.folTicks / (s_ov.frames > 0 ? s_ov.frames : 1)), s_ov.folDraws, g.budgetK, g.slowFrames);
        {
            Follower3D::Prof &P = Follower3D::g_prof;
            u32 n = P.draws ? P.draws : 1;
            Log::Write("acomp: %lu dibujos (%lu rehechos), ciclos por dibujo: indices %lu paleta %lu reducir %lu borde %lu copiar %lu (%lu px)",
                       (unsigned long)P.draws, (unsigned long)P.misses, (unsigned long)(P.idx / n), (unsigned long)(P.pal / n),
                       (unsigned long)(P.samp / n),
                       (unsigned long)(P.edge / n), (unsigned long)(P.blit / n), (unsigned long)(P.px / n));
            memset(&P, 0, sizeof(P));
        }
        memset(&s_ov, 0, sizeof(s_ov));
        int keepBudget = g.budgetK;
        memset(&R3D::g_stats, 0, sizeof(R3D::g_stats));
        R3D::g_stats.budgetK = keepBudget;
    }
    if (me.valid && (me.inBattle || me.playerState == Game::STATE_LOADING)) return false;   // combate / carga: pantalla limpia
    bool locked = me.valid && me.playerState == Game::STATE_LOCKED;   // puertas, diálogos, menús, escenas
    bool drew = false;
    int withPos = MMO::g_players.Sample(now, s_views);
    GameHook::FeedViews(s_views, MAX_MMO_PLAYERS, now);   // fase 2: cada cuadro, aunque no se dibujen avatares
    // v16.1: lo que dibuja el plugin (bici, ropa de color) usa las mismas físicas que el modelo real del juego:
    // nunca encima de ti y movimiento continuo. Nombres, burbujas y acompañantes siguen esa misma posición.
    for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
        MMO::RemoteView &v = s_views[i];
        if (!v.valid || (v.flags & (MMO_FLAG_TRANSITION | MMO_FLAG_HIDDEN)) || !me.valid || !TownMap::SameSpace(v.zone, me.zone)) {
            s_mot[i].has = false;
            continue;
        }
        FriendMotion::Step(s_mot[i], v.x, v.y, true, me.x, me.y);
        v.x = s_mot[i].x; v.y = s_mot[i].y;
    }
    Game::CameraState cam;
    (void)withPos;
    // menú Y (arriba) o menú X (abajo, con un marco arriba): amigos sí, chat y paneles no (taparían el menú)
    bool menuY = me.valid && (me.shortcutMenu || me.fieldMenu);
    if (!locked && me.valid && Game::ReadCamera(cam)) drew |= DrawAvatars(s, me, cam, now);
    if (!locked && !menuY && me.valid) drew |= DrawBattleWatch(s, me, now);
    // HUD automático: solo unos segundos cuando algo cambia. Cada píxel que el plugin escribe hace que
    // Azahar muestre ese cuadro a resolución nativa; sin escrituras el juego se ve a la resolución alta.
    {
        static u16 lastZone = 0xFFFF;
        static int lastOnline = -1;
        static bool lastConn = false;
        int online = MMO::g_players.ActiveCount();
        bool conn = App::IsConnected();
        if ((me.valid && me.zone != lastZone) || online != lastOnline || conn != lastConn) s_hudUntil = now + 6000;
        if (me.valid) lastZone = me.zone;
        lastOnline = online; lastConn = conn;
        bool show = Cfg::hudMode == 1 || (Cfg::hudMode == 0 && now < s_hudUntil);
        if (show) { DrawHud(s, me, online); drew = true; }
    }
    if (!locked && !menuY) drew |= DrawChat(s, now);
    return drew;
}

// ---------------------------------------------------------------------------------------------
// Pines de amigos agrupados: los que están en el mismo punto comparten pin y las etiquetas nunca se tapan entre sí
// (si no cabe arriba va abajo; si tampoco, se omite el nombre, el pin queda). v16.5: en vez de "Ana +2", el nombre
// va ROTANDO (cada 2,5 s el siguiente, con su color) para ver quién está en cada sitio aunque estén en la misma ciudad.
struct PinItem { int x, y; u8 c[3]; char name[16]; };
static const u64 PIN_ROTATE_MS = 2500;
static void DrawFriendPins(const Screen &s, const R3D::Target &t, PinItem *it, int n, int radius, int minX, int maxX, int maxY, u64 now) {
    bool done[MAX_MMO_PLAYERS] = {};
    int placed[MAX_MMO_PLAYERS][4];
    int np = 0;
    for (int i = 0; i < n; i++) {
        if (done[i]) continue;
        int group[MAX_MMO_PLAYERS], count = 0;
        group[count++] = i;
        for (int j = i + 1; j < n; j++)
            if (!done[j] && abs(it[j].x - it[i].x) <= 6 && abs(it[j].y - it[i].y) <= 6) { done[j] = true; group[count++] = j; }
        const PinItem &who = it[group[(now / PIN_ROTATE_MS) % (u64)count]];   // el pin no se mueve; cambia quién se nombra
        if (count > 1) TownMap::DrawPin(t, it[i].x + 2, it[i].y - 2, radius, 40, 44, 60);   // pin "apilado" detrás
        TownMap::DrawPin(t, it[i].x, it[i].y, radius, who.c[0], who.c[1], who.c[2]);
        char lab[24];
        snprintf(lab, sizeof(lab), "%s", who.name);
        int lw = TextW(lab) + 4;
        int lx = Clampi(it[i].x - lw / 2, minX, maxX - lw);
        int tries[2] = { it[i].y - radius - 12, it[i].y + radius + 3 };
        for (int k = 0; k < 2; k++) {
            int ly = tries[k];
            if (ly < 2 || ly + 10 > maxY) continue;
            bool clash = false;
            for (int q = 0; q < np && !clash; q++)
                clash = lx < placed[q][2] && lx + lw > placed[q][0] && ly - 1 < placed[q][3] && ly + 10 > placed[q][1];
            if (clash) continue;
            Box(s, lx, ly - 1, lw, 10, Color(8, 12, 22, 200));
            Text(s, lab, lx + 2, ly, Color(who.c[0], who.c[1], who.c[2], 255));
            placed[np][0] = lx; placed[np][1] = ly - 1; placed[np][2] = lx + lw; placed[np][3] = ly + 9;
            np++;
            break;
        }
    }
}

// v16: amigos sobre los mapas del PROPIO juego (Vuelo y AreaNav). Posición exacta: la transformación sale de los
// puntos que el juego tiene colocados en pantalla, así que sigue el desplazamiento y el zoom. No tapa nada del juego.
// [x0,x1]x[y0,y1] = zona visible del mapa (fuera de ella el juego dibuja sus marcos y botones).
static bool DrawGamePins(const Screen &s, const GameMaps::Xform &xf, u64 now, int x0, int y0, int x1, int y1) {
    R3D::Target t;
    if (!MakeTarget(s, t)) return false;
    MMO::g_players.Sample(now, s_views);
    int listed = 0, npins = 0;
    char ticker[MAX_MMO_PLAYERS][64];
    PinItem pins[MAX_MMO_PLAYERS];
    for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
        const MMO::RemoteView &rv = s_views[i];
        if (!rv.valid || (rv.flags & MMO_FLAG_HIDDEN)) continue;
        const u8 *c = kColors[(rv.id - 1) % 12];
        char nm[24];
        App::AsciiFold(rv.name, nm, sizeof(nm));
        const char *place = ZoneOsd(rv.zone);
        snprintf(ticker[listed++], 64, "%s: %s", nm, place && place[0] ? place : "?");
        float px, py;
        if (!TownMap::ZoneToImage(rv.zone, rv.x, rv.y, px, py)) continue;
        int sx = (int)(xf.ox + xf.s * px + 0.5f), sy = (int)(xf.oy + xf.s * py + 0.5f);
        if (sx < x0 || sy < y0 || sx > x1 || sy > y1) continue;
        PinItem &pi = pins[npins++];
        pi.x = sx; pi.y = sy; memcpy(pi.c, c, 3); snprintf(pi.name, sizeof(pi.name), "%.15s", nm);
    }
    bool drew = false;
    if (npins) { DrawFriendPins(s, t, pins, npins, xf.s > 1.5f ? 5 : xf.s < 0.8f ? 3 : 4, x0 - 2, x1 + 2, y1 + 2, now); drew = true; }
    // v16.1: TU posición exacta. El icono de cabeza del juego marca solo la ruta o ciudad (en las rutas largas, como
    // la 111, queda lejos de donde estás); este marcador usa tus coordenadas reales, igual que los de tus amigos.
    {
        Game::LocalState me = Game::Snapshot();
        float px, py;
        if (me.valid && TownMap::ZoneToImage(me.zone, me.x, me.y, px, py)) {
            int sx = (int)(xf.ox + xf.s * px + 0.5f), sy = (int)(xf.oy + xf.s * py + 0.5f);
            if (sx >= x0 && sy >= y0 && sx <= x1 && sy <= y1) {
                bool blink = ((now / 500) & 1) != 0;          // late despacio para distinguirse de los amigos
                int r = xf.s > 1.5f ? 5 : 4;
                TownMap::DrawPin(t, sx, sy, r + 1, 255, 255, 255);
                TownMap::DrawPin(t, sx, sy, r - 1, blink ? 255 : 230, blink ? 70 : 40, blink ? 70 : 50);
                const char *you = CjkOsd() ? TR("Tu") : Lang::TOsd("Tu");
                int lh = OsdH(you), lw = OsdW(you) + 4, ly = sy + r + 3;
                if (ly + lh > y1 + 2) ly = sy - r - 3 - lh;
                Box(s, Clampi(sx - lw / 2, x0 - 2, x1 + 2 - lw), ly - 1, lw, lh, Color(8, 12, 22, 200));
                OsdText(s, you, Clampi(sx - lw / 2, x0 - 2, x1 + 2 - lw) + 2, ly - (lh > 10 ? 1 : 0), Color(255, 255, 255, 255));
                drew = true;
            }
        }
    }
    if (listed) {
        int idx = (int)((now / PIN_ROTATE_MS) % listed);    // al mismo ritmo que el nombre del pin: dicen lo mismo
        char line[96];
        snprintf(line, sizeof(line), "[%d/%d] %s", idx + 1, listed, ticker[idx]);
        Box(s, 2, 2, OsdW(line) + 6, OsdH(line) + 1, Color(8, 12, 22, 200));
        OsdText(s, line, 5, Big(line) ? 2 : 3, Color(150, 255, 200, 255));
        drew = true;
    }
    return drew;
}

static bool DrawBottom(const Screen &s, u64 now) {
    if (Cfg::mapMode == 2) return false;
    GameMaps::Xform xf;
    // Mapa de Vuelo y AreaNav del juego: se detectan por sus objetos (sin leer píxeles: KB §13.6)
    if (GameMaps::FlyMap(xf, now)) return xf.ok && DrawGamePins(s, xf, now, 4, 4, 315, 204);
    if (GameMaps::AreaNav(xf, now)) {
        if (!xf.ok) return false;
        // v16.2: con el menú Y el juego oscurece el mapa de abajo (sigue ahí, inactivo): sin pines ni "Tu" encima
        if (Game::Snapshot().shortcutMenu) return false;
        // AreaNav normal: el mapa va dentro del marco del PokéNav; ampliado: toda la pantalla salvo la barra
        if (xf.s < 0.9f) return DrawGamePins(s, xf, now, 4, 20, 238, 194);
        return DrawGamePins(s, xf, now, 4, 4, 315, 204);
    }
    return false;
}

static volatile bool s_busy = false;
bool Busy() { return s_busy; }

// Prueba de desarrollo (v16.2): con el archivo mmo_dev_walk.txt el plugin pulsa A en la pantalla de título hasta
// entrar en la partida y luego sigue un guion. NO guarda. Sin ese archivo no hace nada.
//  - Archivo sin guion: unos pasos de ida y vuelta (izquierda/derecha/abajo/arriba) durante ~60 s.
//  - Guion: líneas "ms TECLA [duración]" (ms desde que se entra en la partida; TECLA = A B X Y L R START SELECT UP
//    DOWN LEFT RIGHT o FIN). Ej.: "8000 X" abre el menú X a los 8 s; "9000 RIGHT 1500" anda 1,5 s a la derecha.
//    v16.3: "12000 FOTO" guarda la pantalla de arriba (con lo que dibuja el plugin) en mmo_dev_top_N.bmp.
//    v16.5: "12000 FOTOB" guarda la de abajo (mapas con los pines de los amigos) en mmo_dev_bot_N.bmp.
struct DevStep { u32 at, key, dur; };
static DevStep s_dev[32];
static int s_ndev = 0;
static u32 DevKey(const char *k) {
    static const struct { const char *n; u32 k; } kMap[] = {
        { "A", Key::A }, { "B", Key::B }, { "X", Key::X }, { "Y", Key::Y }, { "L", Key::L }, { "R", Key::R },
        { "START", Key::Start }, { "SELECT", Key::Select }, { "UP", Key::DPadUp }, { "DOWN", Key::DPadDown },
        { "LEFT", Key::DPadLeft }, { "RIGHT", Key::DPadRight }, { "FIN", 0xFFFFFFFFu }, { "FOTO", 0xFFFFFFFEu },
        { "FOTOB", 0xFFFFFFFDu } };
    for (auto &m : kMap) if (!strcmp(k, m.n)) return m.k;
    return 0;
}
static void DevLoad() {
    File f;
    if (File::Open(f, "mmo_dev_walk.txt", File::READ) != 0) return;
    char buf[1024] = {};
    u64 sz = f.GetSize();
    if (sz > sizeof(buf) - 1) sz = sizeof(buf) - 1;
    f.Read(buf, (u32)sz);
    f.Close();
    char *p = buf;
    while (*p && s_ndev < 32) {
        char *eol = strchr(p, '\n');
        if (eol) *eol = 0;
        unsigned at = 0, dur = 120;
        char key[16] = "";
        if (sscanf(p, "%u %15s %u", &at, key, &dur) >= 2) {
            u32 k = DevKey(key);
            if (k) { s_dev[s_ndev].at = at; s_dev[s_ndev].key = k; s_dev[s_ndev].dur = dur; s_ndev++; }
        }
        if (!eol) break;
        p = eol + 1;
    }
}
// InjectKey de CTRPF solo marca "mantenido" en el anillo del HID (comprobado en libctrpf.a) y una vez por cuadro: el
// HID escribe una entrada nueva cada ~4 ms y el juego lee la última, así que a menudo no lo ve, y los menús (X, título)
// reaccionan a "recién pulsado" (+4 de cada entrada). Un hilo de prueba marca la entrada más reciente cada 0,5 ms:
// mantenido mientras dure el paso y recién pulsado sus primeros 40 ms.
static volatile u32 s_devHold = 0, s_devEdge = 0;
static volatile u64 s_devEdgeUntil = 0;
static void DevInputThread(void *) {
    for (;;) {
        u32 hold = s_devHold, edge = s_devEdge;
        if (!(hold | edge) || !hidSharedMem) { svcSleepThread(4000000LL); continue; }
        u32 i = hidSharedMem[4] & 7;
        hidSharedMem[10 + i * 4] |= hold | edge;
        if (edge) {
            if (osGetTime() < s_devEdgeUntil) hidSharedMem[10 + i * 4 + 1] |= edge;
            else s_devEdge = 0;
        }
        svcSleepThread(500000LL);
    }
}
static void DevPress(u32 key, u64 now) { s_devEdge |= key; s_devEdgeUntil = now + 40; }
static volatile bool s_devShot = false, s_devShotB = false;
static void DevWalk(u64 now) {
    static int state = 0;                     // 0 sin mirar, 1 entrando, 2 en marcha, 3 hecho / no hay archivo
    static u64 t0 = 0, freeSince = 0;
    if (state == 0) {
        state = File::Exists("mmo_dev_walk.txt") ? 1 : 3; t0 = now;
        if (state == 1) {
            DevLoad();
            threadCreate(DevInputThread, nullptr, 0x1000, 0x30, -2, true);
            Log::Write("dev: prueba (%d pasos de guion)", s_ndev);
        }
    }
    if (state == 3) return;
    Game::LocalState me = Game::Snapshot();
    bool free = me.valid && me.playerState == Game::STATE_FREE && !me.inBattle;
    if (state == 1) {
        if (free) { if (!freeSince) freeSince = now; } else freeSince = 0;
        if (freeSince && now - freeSince > 2500) { state = 2; t0 = now; Log::Write("dev: en la partida"); return; }
        static u64 lastPulse = 0;
        if (!free && now - t0 > 6000 && now - lastPulse > 900) { lastPulse = now; DevPress(Key::A, now); }  // título
        s_devHold = 0;
        if (now - t0 > 120000) { state = 3; Log::Write("dev: no se pudo entrar en la partida"); }
        return;
    }
    u64 el = now - t0;
    if (s_ndev) {                                            // guion
        static u8 fired[32];                                 // "recién pulsado" solo al empezar cada paso
        u32 hold = 0;
        for (int i = 0; i < s_ndev; i++) {
            const DevStep &d = s_dev[i];
            if (d.key == 0xFFFFFFFFu) { if (el >= d.at) { s_devHold = 0; state = 3; Log::Write("dev: fin del guion"); return; } continue; }
            if (d.key == 0xFFFFFFFEu) { if (el >= d.at && !fired[i]) { fired[i] = 1; s_devShot = true; } continue; }
            if (d.key == 0xFFFFFFFDu) { if (el >= d.at && !fired[i]) { fired[i] = 1; s_devShotB = true; } continue; }
            if (el >= d.at && el < d.at + d.dur) {
                hold |= d.key;
                if (!fired[i]) { fired[i] = 1; DevPress(d.key, now); Log::Write("dev: pulsa %08lX", (unsigned long)d.key); }
            }
        }
        s_devHold = hold;
        if (el > 600000) { state = 3; Log::Write("dev: fin del guion (tiempo)"); }
        return;
    }
    static const struct { u32 key; u32 ms; } kSteps[] = {
        { Key::DPadLeft, 700 }, { 0, 1200 }, { Key::DPadRight, 700 }, { 0, 1200 }, { Key::DPadDown, 600 }, { 0, 1500 },
        { Key::DPadUp, 600 }, { 0, 1500 }, { Key::DPadRight, 500 }, { 0, 1000 }, { Key::DPadLeft, 500 }, { 0, 2000 },
    };
    const int N = sizeof(kSteps) / sizeof(kSteps[0]);
    u32 total = 0;
    for (int i = 0; i < N; i++) total += kSteps[i].ms;
    if (el > 60000 || !me.valid || me.inBattle) { s_devHold = 0; state = 3; Log::Write("dev: fin del paseo"); return; }
    u32 ph = (u32)(el % total), acc = 0, hold = 0;
    for (int i = 0; i < N; i++) {
        acc += kSteps[i].ms;
        if (ph < acc) { if (kSteps[i].key && free) hold = kSteps[i].key; break; }
    }
    s_devHold = hold;
}

static bool OnFrameImpl(const Screen &screen);
static volatile bool s_hold = false;
void Hold(bool hold) { s_hold = hold; }

bool OnFrame(const Screen &screen) {
    s_busy = true;
    if (s_hold) { s_busy = false; return false; }
    bool r = OnFrameImpl(screen);
    s_busy = false;
    return r;
}

static bool OnFrameImpl(const Screen &screen) {
    u64 now = osGetTime();
    s_frameCount++;
    if (screen.IsTop) DevWalk(now);
    if (screen.IsTop) {
        // duración real del cuadro del juego -> presupuesto del motor 3D
        static u64 lastTick = 0;
        u64 tk = svcGetSystemTick();
        if (lastTick) R3D::FrameFeedback(tk - lastTick);
        lastTick = tk;
        Perf::FrameMark(now);
        static const float kCap[3] = { 1400000.f, 750000.f, 500000.f };   // calidad / justo / ahorro
        R3D::SetBudgetCap(kCap[Perf::Level()]);
    }
    s_real = &screen;
    if (screen.IsTop) {
        bool drew = DrawTop(screen, now);
        if (s_devShot) {                                    // prueba: foto de lo que se ve arriba (con el plugin)
            s_devShot = false;
            static int n = 0;
            char path[40];
            snprintf(path, sizeof(path), "mmo_dev_top_%d.bmp", ++n);
            DevShot(screen, path);
            {
                Game::LocalState me = Game::Snapshot();
                const Trail &T = s_trail[0];
                Log::Write("dev: foto %s zona %d yo (%.0f,%.0f) acomp (%.0f,%.0f)%s%s", path, me.zone, (double)me.x, (double)me.y,
                           (double)T.fx, (double)T.fy, T.place ? " buscando" : "", T.zHid ? " tapado" : "");
            }
            DepthProbe::RequestDump();                      // y la profundidad de ese momento (siguiente cuadro)
        }
        DepthProbe::ServiceDump();                          // prueba: la foto de profundidad pedida con FOTO
        return drew;
    }
    bool drewB = DrawBottom(screen, now);
    if (s_devShotB) {                                       // prueba: foto de la pantalla de abajo (con los pines)
        s_devShotB = false;
        static int nb = 0;
        char path[40];
        snprintf(path, sizeof(path), "mmo_dev_bot_%d.bmp", ++nb);
        DevShot(screen, path);
        Log::Write("dev: foto %s", path);
    }
    return drewB;
}

} // namespace Overlay
