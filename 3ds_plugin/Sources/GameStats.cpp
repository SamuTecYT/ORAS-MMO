#include "GameStats.hpp"
#include "GameState.hpp"
#include "Log.hpp"
#include <CTRPluginFramework.hpp>
#include <cstring>

using namespace CTRPluginFramework;

namespace Stats {

static Mutex  s_mutex;
static Mine   s_mine;
static Battle s_battle;

static const u32 OFF_PARTY  = 0x8C9E8;
static const u32 SLOT       = 0x1E4;
static const u32 PK_SIZE    = 232;         // 8 + 4 bloques de 56
static const u32 PK_STATS   = 344;         // estadísticas de equipo dentro del hueco
static const u32 MONGAP     = 580;

static inline bool Ok(u32 a, u32 len) {
    if (a < 0x08000000 || a + len > 0x10000000) return false;
    return Process::CheckAddress(a & ~0xFFFu, MEMPERM_READ) && Process::CheckAddress((a + len - 1) & ~0xFFFu, MEMPERM_READ);
}
static inline u8  R8(u32 a)  { return *(volatile u8 *)a; }
static inline u16 R16(u32 a) { return (u16)(R8(a) | (R8(a + 1) << 8)); }
static inline u32 R32(u32 a) { return (u32)R16(a) | ((u32)R16(a + 2) << 16); }

// ---------------------------------------------------------------------------------------------
// PK6: descifrado (igual que PKHeX/Citra-Tracker) solo de lo que usamos
struct Pk { u16 species; u8 level; u16 hp, hpMax; };
static bool ReadPk(u32 a, Pk &out) {
    memset(&out, 0, sizeof(out));
    if (!Ok(a, PK_STATS + 22)) return false;
    u8 e[PK_SIZE];
    for (u32 i = 0; i < PK_SIZE; i++) e[i] = R8(a + i);
    u32 pv = e[0] | (e[1] << 8) | (e[2] << 16) | ((u32)e[3] << 24);
    if (!e[0] && !pv) return false;
    static const u8 kPosA[24] = { 0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 2, 3, 1, 1, 2, 3, 2, 3, 1, 1, 2, 3, 2, 3 };
    u32 sv = ((pv >> 13) & 31) % 24;
    // descifrar los 224 bytes de bloques con el LCRNG sembrado con el PV
    u32 seed = pv;
    for (u32 i = 8; i < PK_SIZE; i += 2) {
        seed = seed * 0x41C64E6Du + 0x6073u;
        u16 w = (u16)(e[i] | (e[i + 1] << 8)) ^ (u16)(seed >> 16);
        e[i] = (u8)w; e[i + 1] = (u8)(w >> 8);
    }
    u32 aOff = 8 + 56 * kPosA[sv];
    out.species = (u16)(e[aOff] | (e[aOff + 1] << 8));
    // estadísticas de equipo: mismo PV, la secuencia vuelve a empezar
    u8 st[22];
    for (u32 i = 0; i < 22; i++) st[i] = R8(a + PK_STATS + i);
    seed = pv;
    for (u32 i = 0; i < 22; i += 2) {
        seed = seed * 0x41C64E6Du + 0x6073u;
        u16 w = (u16)(st[i] | (st[i + 1] << 8)) ^ (u16)(seed >> 16);
        st[i] = (u8)w; st[i + 1] = (u8)(w >> 8);
    }
    out.level = st[4];
    out.hp = (u16)(st[8] | (st[9] << 8));
    out.hpMax = (u16)(st[10] | (st[11] << 8));
    return out.species >= 1 && out.species <= 721 && out.level >= 1 && out.level <= 100 && out.hpMax > 0 && out.hp <= out.hpMax;
}

// ---------------------------------------------------------------------------------------------
// v16: todo por la cadena de punteros del propio juego (KB §2.1, decompilado 2026-10-16), sin búsquedas
//   GS = *0x0062F7C4, GM = *(GS+0x1C), GD = *GM (GameData, todos los bloques del guardado)
//   Misc  GD+0x3418: dinero u32 +0x10, medallas u32 +0x14 (BadgeGetCount 0x4E9E64, dinero 0x3E59E0)  [S]
//   PlayTime GD+0x104C: datos +4 (8 B): horas u16, minutos u8, segundos u8 (orden PKHeX)       [S tamaño / I orden]
//   Zukan GD+0x13648: firma 0x2F120F17 en +4, capturados bits +0xC, vistos 4 x 0x60 desde +0x6C (0x4EB210/0x4EB294) [S]
//   Dendou GD+0x17A9C: datos +4, 16 entradas x 0x1B4, 6 miembros x 0x48 (especie u16 al inicio)  [S tamaño / I formato]
//   Equipo vivo GM+0x14: punteros a 6 PokemonParam +0x00..+0x14, cuántos u8 +0x18 (0x4D3424/0x3B6150) [S]
//     PokemonParam+0xC -> CoreParam: +4 extra de equipo (0x1C B), +8 PK6 (0xE8 B), +0xC u8 "cifrado" (0x168EC0) [S]
static const u32 GS_STATIC = 0x0062F7C4;
static const u32 GD_MISC = 0x3418, GD_PLAYTIME = 0x104C, GD_ZUKAN = 0x13648, GD_DENDOU = 0x17A9C;
static const u32 ZUKAN_MAGIC = 0x2F120F17;

static inline bool InHeap(u32 a) { return a >= 0x08000000 && a < 0x0E000000 && !(a & 3); }
static bool Chain(u32 &GM, u32 &GD) {
    if (!Process::CheckAddress(GS_STATIC & ~0xFFFu, MEMPERM_READ)) return false;
    u32 GS = *(volatile u32 *)GS_STATIC;
    if (!InHeap(GS) || !Ok(GS + 0x1C, 4)) return false;
    GM = R32(GS + 0x1C);
    if (!InHeap(GM) || !Ok(GM, 0x20)) return false;
    GD = R32(GM);
    return InHeap(GD) && Ok(GD + GD_DENDOU, 0x1B44);
}

static void Decrypt(u8 *d, u32 len, u32 pv) {       // LCRNG sembrado con el PV (cada bloque empieza de nuevo)
    u32 seed = pv;
    for (u32 i = 0; i + 1 < len; i += 2) {
        seed = seed * 0x41C64E6Du + 0x6073u;
        u16 w = (u16)(d[i] | (d[i + 1] << 8)) ^ (u16)(seed >> 16);
        d[i] = (u8)w; d[i + 1] = (u8)(w >> 8);
    }
}

// Un Pokémon del equipo vivo. false si el juego lo está tocando justo ahora (la suma de control no cuadra).
static bool ReadMember(u32 member, Pk &out) {
    memset(&out, 0, sizeof(out));
    if (!InHeap(member) || !Ok(member + 0xC, 4)) return false;
    u32 core = R32(member + 0xC);
    if (!InHeap(core) || !Ok(core, 0x10)) return false;
    u32 ex = R32(core + 4), pk = R32(core + 8);
    bool enc = R8(core + 0xC) != 0;
    if (!InHeap(pk) || !Ok(pk, PK_SIZE) || !InHeap(ex) || !Ok(ex, 0x1C)) return false;
    u8 e[PK_SIZE], st[0x1C];
    for (u32 i = 0; i < PK_SIZE; i++) e[i] = R8(pk + i);
    for (u32 i = 0; i < 0x1C; i++) st[i] = R8(ex + i);
    u32 pv = e[0] | (e[1] << 8) | (e[2] << 16) | ((u32)e[3] << 24);
    if (enc) { Decrypt(e + 8, PK_SIZE - 8, pv); Decrypt(st, 0x1C, pv); }
    u16 sum = 0;
    for (u32 i = 8; i < PK_SIZE; i += 2) sum = (u16)(sum + (e[i] | (e[i + 1] << 8)));
    if (sum != (u16)(e[6] | (e[7] << 8))) return false;
    static const u8 kPosA[24] = { 0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 2, 3, 1, 1, 2, 3, 2, 3, 1, 1, 2, 3, 2, 3 };
    u32 aOff = 8 + 56 * kPosA[((pv >> 13) & 31) % 24];
    out.species = (u16)(e[aOff] | (e[aOff + 1] << 8));
    out.level = st[4];
    out.hp = (u16)(st[8] | (st[9] << 8));
    out.hpMax = (u16)(st[10] | (st[11] << 8));
    return out.species >= 1 && out.species <= 721 && out.level >= 1 && out.level <= 100;
}

static int PopBits(u32 a, u32 bytes) {
    int n = 0;
    for (u32 i = 0; i < bytes; i++) { u8 b = R8(a + i); while (b) { n += b & 1; b >>= 1; } }
    return n;
}

// ---------------------------------------------------------------------------------------------
static u64 s_nextMine = 0, s_nextBattle = 0, s_nextLog = 0;

static void UpdateMine(u32 P, u64 now) {
    (void)P; (void)now;
    u32 GM, GD;
    if (!Chain(GM, GD)) return;                         // título / carga: se conserva lo último bueno
    Mine m;
    { Lock l(s_mutex); m = s_mine; }
    // Dinero y medallas
    u32 money = R32(GD + GD_MISC + 0x10), badges = R32(GD + GD_MISC + 0x14);
    if (money <= 9999999u && badges <= 0xFF) { m.money = money; m.badges = (u8)badges; m.okMisc = true; }
    // Tiempo de juego
    u32 pt = GD + GD_PLAYTIME + 4;
    u16 h = R16(pt); u8 mi = R8(pt + 2), se = R8(pt + 3);
    if (h <= 9999 && mi < 60 && se < 60) { m.hours = h; m.minutes = mi; m.okTime = true; }
    // Pokédex
    u32 z = GD + GD_ZUKAN;
    if (Ok(z, 0x6C + 4 * 0x60) && R32(z + 4) == ZUKAN_MAGIC) {
        m.caught = (u16)PopBits(z + 0xC, 0x5A);
        int seen = 0;
        for (u32 i = 0; i < 0x5A; i++) {
            u8 b = R8(z + 0x6C + i) | R8(z + 0xCC + i) | R8(z + 0x12C + i) | R8(z + 0x18C + i);
            while (b) { seen += b & 1; b >>= 1; }
        }
        m.seen = (u16)seen;
        m.okDex = m.caught <= m.seen && m.seen <= 721;
    }
    // Liga: alguna entrada del Salón de la Fama con un Pokémon válido
    m.league = false;
    for (u32 e = 0; e < 16 && !m.league; e++) {
        u16 sp = R16(GD + GD_DENDOU + 4 + e * 0x1B4);
        if (sp >= 1 && sp <= 721) m.league = true;
    }
    // Equipo vivo (cambia al reordenar en el menú)
    u32 party = R32(GM + 0x14);
    if (InHeap(party) && Ok(party, 0x1C)) {
        u8 n = R8(party + 0x18);
        if (n <= 6) {
            int cnt = 0, sum = 0, mx = 0; u16 lead = 0; bool torn = false;
            for (u32 i = 0; i < n; i++) {
                Pk pk;
                if (!ReadMember(R32(party + i * 4), pk)) { torn = true; break; }
                if (!cnt) lead = pk.species;
                cnt++; sum += pk.level; if (pk.level > mx) mx = pk.level;
            }
            if (!torn && cnt) {                          // a medias (el juego lo estaba cifrando): se guarda lo anterior
                m.okParty = true; m.partyCount = (u8)cnt; m.avgLevel = (u8)(sum / cnt); m.maxLevel = (u8)mx;
                if (lead != m.leadSpecies) Log::Write("stats: lider del equipo = %u", lead);
                m.leadSpecies = lead;
            }
        }
    }
    Lock l(s_mutex);
    s_mine = m;
}

static void UpdateBattle(u32 P) {
    Battle b;
    memset(&b, 0, sizeof(b));
    u32 party = P + OFF_PARTY;
    u32 base = party - 6000000u;
    u32 wildMy = base + 812440u, wildOp = base + 815420u, trMy = base + 809556u, trOp = base + 812536u;
    u32 curOp = party - 0xAF2F5Cu + 0x22EA60u;
    u32 wildPP = party - 0xAF2F5Cu - 20u, trPP = wildPP + 6928u;
    Pk op0;
    u32 my = 0, op = 0, pp = 0;
    if (ReadPk(wildOp, op0) && Ok(wildPP, 1) && R8(wildPP) < 65) { my = wildMy; op = wildOp; pp = wildPP; b.kind = 1; }
    else if (ReadPk(trOp, op0) && Ok(trPP, 1) && R8(trPP) < 65) { my = trMy; op = trOp; pp = trPP; b.kind = 2; }
    if (b.kind) {
        int myCount = 0;
        Pk mine0;
        for (int i = 0; i < 6; i++) { Pk t; if (ReadPk(my + i * SLOT, t)) { if (!myCount) mine0 = t; myCount++; } }
        // rival activo: el de la especie que el juego marca como actual
        u16 cur = Ok(curOp, 2) ? R16(curOp) : 0;
        int opIdx = 0;
        Pk opk = op0;
        for (int i = 0; i < 6; i++) { Pk t; if (ReadPk(op + i * SLOT, t) && t.species == cur) { opIdx = i; opk = t; break; } }
        if (myCount) {
            b.mySpecies = mine0.species; b.myLevel = mine0.level; b.myHp = mine0.hp; b.myHpMax = mine0.hpMax;
            // nivel y PS en vivo (estructura de combate, índice 1 = mi primer Pokémon)
            u32 s = pp + MONGAP * 0 ;
            if (Ok(s - 266, 16)) {
                u16 h1 = R16(s - 264), h2 = R16(s - 266); u8 lv = R8(s - 256);
                u16 hp = h1 < h2 ? h1 : h2, hpMax = h1 < h2 ? h2 : h1;
                if (hpMax && lv >= 1 && lv <= 100) { b.myHp = hp; b.myHpMax = hpMax; b.myLevel = lv; }
            }
        }
        b.opSpecies = opk.species; b.opLevel = opk.level; b.opHp = opk.hp; b.opHpMax = opk.hpMax;
        u32 s = pp + MONGAP * (u32)(myCount + opIdx);
        if (Ok(s - 266, 16)) {
            u16 h1 = R16(s - 264), h2 = R16(s - 266); u8 lv = R8(s - 256);
            u16 hp = h1 < h2 ? h1 : h2, hpMax = h1 < h2 ? h2 : h1;
            if (hpMax && lv >= 1 && lv <= 100) { b.opHp = hp; b.opHpMax = hpMax; b.opLevel = lv; }
        }
    }
    Lock l(s_mutex);
    s_battle = b;
}

void Update(u64 now, bool inBattle) {
    u32 P = Game::PlayerBlockAddr();
    if (!P) return;
    if (now >= s_nextMine) { s_nextMine = now + 500; UpdateMine(P, now); }
    if (inBattle) {
        if (now >= s_nextBattle) { s_nextBattle = now + 300; UpdateBattle(P); }
    } else if (s_battle.kind) {
        Lock l(s_mutex);
        memset(&s_battle, 0, sizeof(s_battle));
    }
    if (now >= s_nextLog) {
        s_nextLog = now + 30000;
        Mine m = GetMine(); Battle b = GetBattle();
        Log::Write("stats: liga=%d t=%d(%u:%02u) med=%02X(%d) dex=%d(%u/%u) dinero=%d(%lu) equipo=%d(%u nv%u-%u lider %u) combate=%u %u nv%u %u/%u vs %u nv%u %u/%u",
                   m.league, m.okTime, m.hours, m.minutes, m.badges, m.okMisc, m.okDex, m.caught, m.seen, m.okMisc, (unsigned long)m.money,
                   m.okParty, m.partyCount, m.avgLevel, m.maxLevel, m.leadSpecies,
                   b.kind, b.mySpecies, b.myLevel, b.myHp, b.myHpMax, b.opSpecies, b.opLevel, b.opHp, b.opHpMax);
    }
}

Mine GetMine() { Lock l(s_mutex); return s_mine; }
Battle GetBattle() { Lock l(s_mutex); return s_battle; }

Rank::Input ToRank(const Mine &m) {
    Rank::Input in{};
    in.badgeMask = m.okMisc ? m.badges : 0;
    in.league = m.league;
    in.okDex = m.okDex; in.caught = m.caught;
    in.okParty = m.okParty; in.maxLevel = m.maxLevel; in.avgLevel = m.avgLevel;
    return in;
}

int Score(const Mine &m) { return Rank::Total(ToRank(m)); }

} // namespace Stats
