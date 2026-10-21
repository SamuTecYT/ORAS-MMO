#include "Players.hpp"
#include "TownMap.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <strings.h>

using namespace CTRPluginFramework;

namespace MMO {

PlayerTable g_players;

static inline float WrapPi(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

void PlayerTable::Clear() {
    Lock l(m_mutex);
    for (auto &p : m_players) p = RemotePlayer();
}

void PlayerTable::OnLeave(u8 id) {
    if (id < 1 || id > MAX_MMO_PLAYERS) return;
    Lock l(m_mutex);
    m_players[id - 1] = RemotePlayer();
}

void PlayerTable::SetBubble(u8 id, const char *txt, u64 nowMs) {
    if (id < 1 || id > MAX_MMO_PLAYERS) return;
    Lock l(m_mutex);
    RemotePlayer &rp = m_players[id - 1];
    Proto::CopyText(rp.bubble, sizeof(rp.bubble), txt);
    rp.bubbleTime = nowMs;
}

bool PlayerTable::GetName(u8 id, char *out, size_t cap) {
    if (id < 1 || id > MAX_MMO_PLAYERS || !cap) return false;
    Lock l(m_mutex);
    const RemotePlayer &rp = m_players[id - 1];
    if (rp.name[0]) Proto::CopyText(out, cap, rp.name);
    else snprintf(out, cap, "J%u", (unsigned)id);
    return rp.active;
}

int PlayerTable::ActiveCount() {
    Lock l(m_mutex);
    int n = 0;
    for (auto &p : m_players) n += p.active ? 1 : 0;
    return n;
}

void PlayerTable::DecodeStats(const PlayerPacket &p, RemoteStats &st) {
    const u8 *m = (const u8 *)p.msg;
    st.hours = (u16)(m[0] | (m[1] << 8)); st.minutes = m[2]; st.badges = m[3];
    st.caught = (u16)(m[4] | (m[5] << 8)); st.seen = (u16)(m[6] | (m[7] << 8));
    st.money = (u32)m[8] | ((u32)m[9] << 8) | ((u32)m[10] << 16) | ((u32)m[11] << 24);
    st.party = m[12]; st.avgLv = m[13]; st.maxLv = m[14]; st.lead = (u16)(m[15] | (m[16] << 8)); st.okMask = m[17];
    st.valid = true;
}

// Nombre como lo guarda el servidor: solo ASCII imprimible, sin espacios al principio ni al final
static void NormName(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    for (size_t i = 0; src && src[i] && i < 14 && n + 1 < cap; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c >= 0x20 && c <= 0x7E) dst[n++] = (char)c;
    }
    while (n && dst[n - 1] == ' ') n--;
    dst[n] = 0;
    size_t lead = 0;
    while (dst[lead] == ' ') lead++;
    if (lead) memmove(dst, dst + lead, n - lead + 1);
}
bool SameRankName(const char *a, const char *b) {
    char x[16], y[16];
    NormName(x, sizeof(x), a); NormName(y, sizeof(y), b);
    return x[0] && strcasecmp(x, y) == 0;
}

// Inserta o actualiza una entrada del ranking guardado (lleno: reemplaza la más antigua). Con el mutex tomado.
// v16.1: una persona = un nombre (el modelo no cuenta: antes de detectar el género se manda "chico").
void PlayerTable::BookPut(const char *name, u16 model, const RemoteStats &st, u16 daysAgo, bool offline, u64 nowMs) {
    if (!name || !name[0]) return;
    int slot = -1, oldest = -1;
    for (int i = 0; i < RANKBOOK_MAX; i++) {
        RankBookEntry &e = m_book[i];
        if (e.used && SameRankName(e.name, name)) { slot = i; break; }
        if (!e.used) { if (oldest < 0 || m_book[oldest].used) oldest = i; }
        else if (oldest < 0 || (m_book[oldest].used && e.updated < m_book[oldest].updated)) oldest = i;
    }
    if (slot < 0) slot = oldest;
    RankBookEntry &e = m_book[slot];
    e.used = true;
    Proto::CopyText(e.name, sizeof(e.name), name);
    e.model = model; e.stats = st; e.daysAgo = daysAgo; e.offline = offline; e.updated = nowMs;
}

// v16.3: una persona que se cambia el nombre (p. ej. entró como "Jugador" y luego puso el suyo) salía DOS veces en el
// ranking, con los mismos puntos: la entrada del nombre viejo se borra.
void PlayerTable::BookForgetLocked(const char *name) {
    if (!name || !name[0]) return;
    for (auto &e : m_book) if (e.used && SameRankName(e.name, name)) e.used = false;
}

void PlayerTable::BookForget(const char *name) {
    Lock l(m_mutex);
    BookForgetLocked(name);
}

int PlayerTable::CopyRankBook(RankBookEntry *out, int cap) {
    Lock l(m_mutex);
    int n = 0;
    for (int i = 0; i < RANKBOOK_MAX && n < cap; i++) {
        if (!m_book[i].used) continue;
        out[n] = m_book[i];
        // ¿Sigue conectado? (la instantánea pudo quedar vieja)
        out[n].offline = true;
        for (const auto &rp : m_players)
            if (rp.active && SameRankName(rp.name, out[n].name)) out[n].offline = false;
        n++;
    }
    return n;
}

void PlayerTable::NetSummary(char *out, size_t cap) {
    if (!cap) return;
    out[0] = 0;
    Lock l(m_mutex);
    size_t n = 0;
    for (const auto &rp : m_players) {
        if (!rp.active || n + 1 >= cap) continue;
        int j = rp.jitterQ4 / 16;
        int w = snprintf(out + n, cap - n, "%sJ%u jit=%d ret=%d", n ? " " : "", (unsigned)rp.id, j, NetSmooth::JitterDelayMs(j));
        if (w < 0) break;
        n += (size_t)w;
        if (n >= cap) { out[cap - 1] = 0; break; }
    }
}

void PlayerTable::OnPacket(const PlayerPacket &p, u64 nowMs) {
    u8 id = p.player_id;
    if (p.type == MMO_PKT_RANK_SNAPSHOT) {           // id 0 = desconectado
        char nm[14];
        Proto::CopyText(nm, sizeof(nm), p.nickname);
        RemoteStats st;
        DecodeStats(p, st);
        const u8 *m = (const u8 *)p.msg;
        Lock l(m_mutex);
        BookPut(nm, p.model_id, st, (u16)(m[18] | (m[19] << 8)), (p.flags & MMO_FLAG_OFFLINE) != 0, nowMs);
        return;
    }
    if (id < 1 || id > MAX_MMO_PLAYERS || id == localId) return;
    Lock l(m_mutex);
    RemotePlayer &rp = m_players[id - 1];
    if (!rp.active) {
        rp = RemotePlayer();
        rp.active = true;
        rp.id = id;
    }
    rp.lastRecv = nowMs;
    if (p.nickname[0]) {
        char nm[sizeof(rp.name)];
        Proto::CopyText(nm, sizeof(nm), p.nickname);
        if (rp.name[0] && !SameRankName(rp.name, nm)) BookForgetLocked(rp.name);   // se cambió el nombre
        memcpy(rp.name, nm, sizeof(nm));
    }

    // Un cliente v13 solo manda PING cuando NO tiene posición que compartir (oculto, título, combate
    // sin jugador válido): se borra su historial para que no quede un "fantasma" congelado.
    if (p.type == MMO_PKT_PING && Proto::GetVersion(p) >= 13) {
        rp.count = 0;
        rp.lastValid = false;
        rp.playInit = false;
        rp.dispValid = false;
        rp.prevValid = false;
        return;
    }
    if (p.type == MMO_PKT_STATS) {
        RemoteStats st;
        DecodeStats(p, st);
        if (!(st.okMask & 0x0F)) return;                 // aún sin datos (arrancando): no pisa lo conocido
        rp.stats = st;
        u16 model = (p.model_id == MMO_MODEL_MAY || p.model_id == MMO_MODEL_BRENDAN) ? p.model_id : rp.model;
        BookPut(rp.name, model, rp.stats, 0, false, nowMs);
        return;
    }
    if (p.type == MMO_PKT_BATTLE) {
        const u8 *m = (const u8 *)p.msg;
        RemoteBattle &b = rp.battle;
        b.kind = m[0];
        b.mySp = (u16)(m[1] | (m[2] << 8)); b.myLv = m[3]; b.myHp = (u16)(m[4] | (m[5] << 8)); b.myHpMax = (u16)(m[6] | (m[7] << 8));
        b.opSp = (u16)(m[8] | (m[9] << 8)); b.opLv = m[10]; b.opHp = (u16)(m[11] | (m[12] << 8)); b.opHpMax = (u16)(m[13] | (m[14] << 8));
        rp.battleTime = nowMs;
        return;
    }
    if (p.type != MMO_PKT_POSITION) return;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
        std::fabs(p.x) > 60000.f || std::fabs(p.y) > 60000.f || std::fabs(p.z) > 60000.f) return;
    if (p.model_id == MMO_MODEL_MAY || p.model_id == MMO_MODEL_BRENDAN) rp.model = p.model_id;
    rp.version = Proto::GetVersion(p);
    rp.variant = (rp.version >= 13) ? (p.lvls[3] & 0x0F) : 0;      // nibble bajo: ropa
    rp.tag = (rp.version >= 13) ? (p.lvls[3] >> 4) : 0;              // nibble alto: etiqueta
    rp.acc = p.btype <= 16 ? p.btype : 0;                            // v16.5: gorro / lazo (v16.3 manda 0)

    // Reloj del emisor: v13 lo manda; v12.6 no -> usamos la hora de llegada.
    u32 stamp = (rp.version >= 13) ? Proto::GetStamp(p) : (u32)nowMs;
    bool overwriteHead = false;
    if (rp.count > 0) {
        s32 d = (s32)(stamp - rp.snaps[rp.head].t);
        if (d == 0 && rp.version < 13) overwriteHead = true;      // v12.6: mismo ms de llegada -> gana el más nuevo
        else if (d <= 0 && d > -5000) return;                     // duplicado o desordenado
        else if (d <= -5000) { rp.count = 0; rp.offsetValid = false; }   // el emisor reinició su juego
    }
    // Desfase de relojes: mínimo observado (latencia mínima) y subida lenta para seguir la deriva
    // (p. ej. un emulador que corre al 85 %). Punto fijo Q8 para no perder los milisegundos.
    s64 off = (s64)nowMs - (s64)stamp;
    if (!rp.offsetValid || off < rp.clockOffset || off - rp.clockOffset > 3000) {
        rp.offQ8 = off * 256;
        rp.offsetValid = true;
    } else {
        rp.offQ8 += (off * 256 - rp.offQ8) / 48;
    }
    rp.clockOffset = rp.offQ8 / 256;
    // v17.0 r3: en combate su emulador puede ir a otro ritmo (escenas más pesadas). Esas ventanas no cuentan para
    // medir el ritmo de su reloj y, al terminar el combate, la reproducción se vuelve a enganchar al momento. Antes
    // el ritmo "de combate" se quedaba y, al volver a caminar, se le dibujaba con retraso hasta que se corregía.
    bool inBattle = (p.flags & MMO_FLAG_IN_BATTLE) != 0;
    if (rp.wasInBattle && !inBattle) rp.playInit = false;
    rp.wasInBattle = inBattle;
    if (inBattle) rp.skewRef = false;
    // v16.1: ritmo de su reloj (ventanas de >= 5 s; un hueco de más de 2 s sin datos reinicia la medida)
    else if (rp.version >= 13) {
        if (!rp.skewRef || (s64)(nowMs - rp.newestRecv) > 2000) { rp.skewStamp = stamp; rp.skewNow = nowMs; rp.skewRef = true; }
        else if (nowMs - rp.skewNow >= 5000) {
            rp.skew = NetSmooth::SkewUpdate(rp.skew, (double)(s32)(stamp - rp.skewStamp), (double)(nowMs - rp.skewNow));
            rp.skewStamp = stamp; rp.skewNow = nowMs;
        }
    }

    Snapshot s;
    s.t = stamp;
    s.x = p.x; s.y = p.y; s.z = p.z;
    if (rp.version >= 13) {
        s.yaw = (float)Proto::GetYaw(p) * (6.2831853f / 65536.0f);
    } else {
        static const float kFacing[4] = { 0.0f, 3.14159265f, -1.5707963f, 1.5707963f };
        s.yaw = kFacing[p.facing & 3];
    }
    s.zone = Proto::ZoneOf(p.map_id);
    s.area = Proto::AreaOf(p.map_id);
    s.state = p.anim;
    s.flags = p.flags;
    rp.newestRecv = nowMs;
    // v16: jitter = cuánto difiere el ritmo de llegada del ritmo con el que el emisor los mandó
    if (rp.version >= 13 && p.type == MMO_PKT_POSITION) {
        if (rp.prevValid) {
            s64 dRecv = (s64)(nowMs - rp.prevRecv), dSend = (s64)(s32)(stamp - rp.prevStamp);
            s64 j = dRecv - dSend;
            if (j < 0) j = -j;
            if (dSend > 0 && dSend < 2000) rp.jitterQ4 = NetSmooth::JitterUpdateQ4(rp.jitterQ4, (int)j);
        }
        rp.prevRecv = nowMs; rp.prevStamp = stamp; rp.prevValid = true;
    }
    if (overwriteHead) { rp.snaps[rp.head] = s; return; }
    rp.head = (rp.count == 0) ? 0 : (rp.head + 1) % SNAP_COUNT;
    rp.snaps[rp.head] = s;
    if (rp.count < SNAP_COUNT) rp.count++;
}

void PlayerTable::ExpireStale(u64 nowMs) {
    Lock l(m_mutex);
    for (auto &rp : m_players)
        if (rp.active && (s64)(nowMs - rp.lastRecv) > (s64)PLAYER_TIMEOUT_MS) rp = RemotePlayer();   // con signo: lastRecv puede ser posterior a nowMs
}

// Interpola el historial en el instante 'senderT' (reloj del emisor)
void PlayerTable::Interpolate(const RemotePlayer &rp, s64 senderT, RemoteView &v) {
    const Snapshot &newest = rp.snaps[rp.head];
    auto at = [&](int age) -> const Snapshot & { return rp.snaps[(rp.head - age + SNAP_COUNT) % SNAP_COUNT]; };

    const Snapshot *a = nullptr, *b = nullptr;
    if (senderT >= (s64)newest.t) {
        a = &newest;
    } else {
        for (int age = 1; age < rp.count; age++) {
            const Snapshot &older = at(age);
            if ((s64)older.t <= senderT) { a = &older; b = &at(age - 1); break; }
        }
        if (!a) a = &at(rp.count - 1);   // más viejo que todo el historial
    }

    v.zone = a->zone; v.area = a->area; v.state = a->state; v.flags = a->flags;
    v.speed = 0.f;
    if (b) {
        float dx = b->x - a->x, dy = b->y - a->y, dz = b->z - a->z;
        float d = sqrtf(dx * dx + dy * dy);
        float span = (float)(b->t - a->t);
        bool jump = !TownMap::SameSpace(a->zone, b->zone) || d > TELEPORT_DIST;   // cruzar ruta->ciudad no es un salto
        if (span <= 0.f || jump) {
            const Snapshot &s = (senderT - (s64)a->t > (s64)(b->t - senderT)) ? *b : *a;
            v.x = s.x; v.y = s.y; v.z = s.z; v.yaw = s.yaw;
            v.zone = s.zone; v.area = s.area; v.state = s.state; v.flags = s.flags;
            return;
        }
        float t = (float)(senderT - (s64)a->t) / span;
        v.x = a->x + dx * t;
        v.y = a->y + dy * t;
        v.z = a->z + dz * t;
        v.yaw = a->yaw + WrapPi(b->yaw - a->yaw) * t;
        v.speed = d * 1000.f / span;
        v.state = (t < 0.5f) ? a->state : b->state;
        v.flags = (t < 0.5f) ? a->flags : b->flags;      // la bici (flags) y el estado de la MISMA muestra
        return;
    }

    // Sin muestra posterior: mantener o extrapolar un poco con la velocidad reciente
    v.x = a->x; v.y = a->y; v.z = a->z; v.yaw = a->yaw;
    if (a == &newest && rp.count >= 2) {
        const Snapshot &prev = at(1);
        float span = (float)(newest.t - prev.t);
        float ahead = (float)(senderT - (s64)newest.t);
        if (span > 0.f && span < 400.f && ahead > 0.f && TownMap::SameSpace(newest.zone, prev.zone) && newest.state != MOVE_IDLE) {
            if (ahead > NetSmooth::EXTRAPOLATE_MAX_MS) ahead = NetSmooth::EXTRAPOLATE_MAX_MS;
            float vx = (newest.x - prev.x) / span, vy = (newest.y - prev.y) / span;
            float d = sqrtf(vx * vx + vy * vy) * 1000.f;
            if (d * span / 1000.f < TELEPORT_DIST) {
                float ex = vx * ahead, ey = vy * ahead, el = sqrtf(ex * ex + ey * ey);
                if (el > NetSmooth::EXTRAPOLATE_MAX_DIST) { ex *= NetSmooth::EXTRAPOLATE_MAX_DIST / el; ey *= NetSmooth::EXTRAPOLATE_MAX_DIST / el; }
                v.x += ex;
                v.y += ey;
                v.speed = d;
            }
        }
    }
}

int PlayerTable::Sample(u64 nowMs, RemoteView *out) {
    Lock l(m_mutex);
    int n = 0;
    for (int i = 0; i < MAX_MMO_PLAYERS; i++) {
        RemotePlayer &rp = m_players[i];
        RemoteView &v = out[i];
        memset(&v, 0, sizeof(v));
        if (!rp.active || rp.count == 0 || rp.id == localId) continue;

        // Reloj de reproducción con ritmo autoajustable: sigue al emisor aunque los emuladores corran a
        // velocidades distintas (p. ej. uno al 130 %). Antes se adelantaba a los datos y el avatar iba a trozos.
        const Snapshot &nw = rp.snaps[rp.head];
        s64 dtS = rp.playInit ? (s64)(nowMs - rp.lastSample) : 0;
        if (dtS < 0) dtS = 0;
        if (dtS > 100) dtS = 100;
        float target = (float)NetSmooth::JitterDelayMs(rp.jitterQ4 / 16);
        rp.delayHold = rp.playInit ? NetSmooth::HoldDelay(rp.delayHold, target, (float)dtS) : target;
        double desired = (double)nw.t + (double)(s64)(nowMs - rp.newestRecv) - (double)rp.delayHold;
        if (!rp.playInit || rp.playT - desired > 1500.0 || desired - rp.playT > 1500.0) {
            rp.playT = desired;
            rp.playInit = true;
        } else {
            // v16.1: al ritmo de SU reloj (skew) con una corrección suave (±8 %) en lugar de hasta ±60 %. Solo si
            // está a punto de quedarse sin datos mientras se mueve, frena más (mejor un poco lento que pararse).
            double rate = rp.skew * NetSmooth::PlayRate(desired - rp.playT);
            bool streaming = (s64)(nowMs - rp.newestRecv) < 120;
            if (streaming && (double)nw.t - rp.playT < 15.0 && rate > 0.75 * rp.skew) rate = 0.75 * rp.skew;
            rp.playT += (double)dtS * rate;
        }
        rp.lastSample = nowMs;
        s64 senderT = (s64)rp.playT;
        Interpolate(rp, senderT, v);

        // v16: si la posición calculada salta más de lo que el jugador pudo moverse (una predicción que falló),
        // el error se reparte en CORRECT_MS en lugar de verse como un tirón. Saltos grandes (puertas) = directo.
        {
            float dt = rp.dispValid ? (float)(s64)(nowMs - rp.dispT) : 0.f;
            if (dt < 0.f) dt = 0.f;
            if (dt > 100.f) dt = 100.f;
            float ex = v.x - rp.dispX, ey = v.y - rp.dispY, ez = v.z - rp.dispZ;
            float err = sqrtf(ex * ex + ey * ey);
            float maxStep = (v.speed * 1.5f + 60.f) * dt / 1000.f + 1.f;
            if (rp.dispValid && err > maxStep && err < TELEPORT_DIST && rp.dispZone == v.zone) {
                float k = dt / NetSmooth::CORRECT_MS;
                if (k * err < maxStep) k = maxStep / err;
                if (k > 1.f) k = 1.f;
                rp.dispX += ex * k; rp.dispY += ey * k; rp.dispZ += ez * k;
                v.x = rp.dispX; v.y = rp.dispY; v.z = rp.dispZ;
            } else {
                rp.dispX = v.x; rp.dispY = v.y; rp.dispZ = v.z;
            }
            rp.dispValid = true;
            rp.dispZone = v.zone;
            rp.dispT = nowMs;
        }

        // Fase de animación: avanza con la distancia recorrida (los pies no "patinan")
        if (rp.lastValid) {
            float dx = v.x - rp.lastX, dy = v.y - rp.lastY;
            float d = sqrtf(dx * dx + dy * dy);
            if (d < TELEPORT_DIST) rp.animPhase += d;
        }
        rp.lastX = v.x; rp.lastY = v.y; rp.lastValid = true;

        v.valid = true;
        v.id = rp.id;
        Proto::CopyText(v.name, sizeof(v.name), rp.name[0] ? rp.name : "");
        if (!v.name[0]) snprintf(v.name, sizeof(v.name), "J%u", (unsigned)rp.id);
        v.model = rp.model;
        v.variant = rp.variant;
        v.acc = rp.acc;
        v.tag = rp.tag;
        v.stats = rp.stats;
        v.battle = rp.battle;
        { s64 ba = (s64)(nowMs - rp.battleTime); v.battleAge = (rp.battleTime && ba >= 0) ? (u64)ba : 999999; }
        v.animPhase = rp.animPhase;
        s64 bAge = (s64)(nowMs - rp.bubbleTime);
        if (bAge < 0) bAge = 0;
        if (rp.bubble[0] && bAge < 6000) {
            Proto::CopyText(v.bubble, sizeof(v.bubble), rp.bubble);
            v.bubbleAge = (u64)bAge;
        }
        n++;
    }
    return n;
}

} // namespace MMO
