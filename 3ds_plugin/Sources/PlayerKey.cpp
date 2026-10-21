#include "PlayerKey.hpp"
#include "Log.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstring>

using namespace CTRPluginFramework;

namespace {

// SHA-256 (FIPS 180-4), pequeño y sin memoria dinámica
struct Sha256 {
    u32 h[8];
    u8  buf[64];
    u64 len = 0;
    u32 n = 0;

    Sha256() {
        static const u32 iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
        memcpy(h, iv, sizeof(h));
    }
    static u32 Ror(u32 x, int r) { return (x >> r) | (x << (32 - r)); }
    void Block(const u8 *p) {
        static const u32 K[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
        u32 w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (u32)p[4 * i] << 24 | (u32)p[4 * i + 1] << 16 | (u32)p[4 * i + 2] << 8 | (u32)p[4 * i + 3];
        for (int i = 16; i < 64; i++) {
            u32 s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            u32 s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
        for (int i = 0; i < 64; i++) {
            u32 t1 = k + (Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            u32 t2 = (Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
    }
    void Add(const void *data, size_t size) {
        const u8 *p = (const u8 *)data;
        len += size;
        while (size) {
            u32 take = 64 - n;
            if (take > size) take = (u32)size;
            memcpy(buf + n, p, take);
            n += take; p += take; size -= take;
            if (n == 64) { Block(buf); n = 0; }
        }
    }
    void Done(u8 out[32]) {
        u64 bits = len * 8;
        static const u8 one = 0x80, zero = 0;
        Add(&one, 1);
        while (n != 56) Add(&zero, 1);
        u8 L[8];
        for (int i = 0; i < 8; i++) L[i] = (u8)(bits >> (56 - 8 * i));
        Add(L, 8);
        for (int i = 0; i < 8; i++) {
            out[4 * i] = (u8)(h[i] >> 24); out[4 * i + 1] = (u8)(h[i] >> 16);
            out[4 * i + 2] = (u8)(h[i] >> 8); out[4 * i + 3] = (u8)h[i];
        }
    }
};

const char *KEY_FILE = "mmo_key.txt";
u8   s_key[16];
bool s_ready = false;

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// 32 cifras hexadecimales al principio (lo que venga después, p. ej. un salto de línea, no cuenta)
bool ReadKey(u8 out[16]) {
    char buf[64] = {0};
    File f(KEY_FILE, File::READ);
    if (!f.IsOpen()) return false;
    f.Read(buf, sizeof(buf) - 1);
    f.Close();
    u8 any = 0;
    for (int i = 0; i < 16; i++) {
        int hi = HexVal(buf[2 * i]), lo = HexVal(buf[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (u8)(hi << 4 | lo);
        any |= out[i];
    }
    return any != 0;
}

} // namespace

namespace PlayerKey {

void Init() {
    if (ReadKey(s_key)) { s_ready = true; return; }
    // Llave nueva: azar del sistema (ps:ps; Azahar usa el generador seguro del PC) mezclado con los relojes
    Sha256 s;
    u8 rnd[32] = {0};
    bool sys = false;
    if (R_SUCCEEDED(psInit())) {
        sys = R_SUCCEEDED(PS_GenerateRandomBytes(rnd, sizeof(rnd)));
        psExit();
    }
    u64 mix[4] = { svcGetSystemTick(), osGetTime(), (u64)(uintptr_t)&s, (u64)(uintptr_t)rnd };
    s.Add(rnd, sizeof(rnd));
    s.Add(mix, sizeof(mix));
    u8 d[32];
    s.Done(d);
    memcpy(s_key, d, sizeof(s_key));
    char hex[33];
    for (int i = 0; i < 16; i++) snprintf(hex + 2 * i, 3, "%02x", s_key[i]);
    {
        File f(KEY_FILE, File::RWC | File::TRUNCATE);
        if (f.IsOpen()) { f.Write(hex, 32); f.Close(); }
    }
    // Solo se usa si quedó guardada: una llave que se pierde al reiniciar dejaría el nombre bloqueado
    u8 back[16];
    s_ready = ReadKey(back) && memcmp(back, s_key, sizeof(back)) == 0;
    Log::Write(s_ready ? "Llave del ranking creada (mmo_key.txt%s)" : "No se pudo guardar mmo_key.txt: ranking sin llave%s",
               sys ? "" : ", azar del reloj");
}

bool Ready() { return s_ready; }

void Token(const std::string &server, int port, const std::string &room, u8 out[16]) {
    static const char tag[] = "ORASMMO-KEY1|";
    char where[96];
    int n = snprintf(where, sizeof(where), "|%s:%d|%s", server.c_str(), port, room.c_str());
    if (n < 0) n = 0;
    if (n >= (int)sizeof(where)) n = (int)sizeof(where) - 1;
    Sha256 s;
    s.Add(tag, sizeof(tag) - 1);
    s.Add(s_key, sizeof(s_key));
    s.Add(where, (size_t)n);
    u8 d[32];
    s.Done(d);
    memcpy(out, d, 16);
}

} // namespace PlayerKey
