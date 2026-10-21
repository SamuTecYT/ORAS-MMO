/*
 * Math3D.hpp — ORAS MMO v13
 * Matemática mínima para el motor de avatares: vectores, matrices 3x4 (afines, fila mayor,
 * igual que las de los BCH del juego) y 4x4 (proyección).
 */
#pragma once
#include <cmath>
#include <cstring>

struct Vec3 {
    float x, y, z;
};

static inline Vec3 V3(float x, float y, float z) { Vec3 v = { x, y, z }; return v; }
static inline Vec3 Add(const Vec3 &a, const Vec3 &b) { return V3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3 Sub(const Vec3 &a, const Vec3 &b) { return V3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3 Mul(const Vec3 &a, float s) { return V3(a.x * s, a.y * s, a.z * s); }
static inline float Dot(const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3 Cross(const Vec3 &a, const Vec3 &b) {
    return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float Length(const Vec3 &a) { return sqrtf(Dot(a, a)); }
static inline Vec3 Normalize(const Vec3 &a) {
    float l = Length(a);
    return l > 1e-12f ? Mul(a, 1.0f / l) : V3(0, 0, 0);
}

// Matriz afín 3x4 fila mayor: m[r*4 + c]; la columna 3 es la traslación.
struct Mat34 {
    float m[12];
};

static inline void M34Identity(Mat34 &o) {
    memset(o.m, 0, sizeof(o.m));
    o.m[0] = o.m[5] = o.m[10] = 1.f;
}

static inline void M34Mul(Mat34 &o, const Mat34 &a, const Mat34 &b) {   // o = a * b
    Mat34 r;
    for (int i = 0; i < 3; i++) {
        const float *ar = a.m + i * 4;
        for (int j = 0; j < 4; j++) {
            float v = ar[0] * b.m[j] + ar[1] * b.m[4 + j] + ar[2] * b.m[8 + j];
            if (j == 3) v += ar[3];
            r.m[i * 4 + j] = v;
        }
    }
    o = r;
}

static inline Vec3 M34Point(const Mat34 &a, const Vec3 &p) {
    return V3(a.m[0] * p.x + a.m[1] * p.y + a.m[2] * p.z + a.m[3],
              a.m[4] * p.x + a.m[5] * p.y + a.m[6] * p.z + a.m[7],
              a.m[8] * p.x + a.m[9] * p.y + a.m[10] * p.z + a.m[11]);
}

static inline Vec3 M34Dir(const Mat34 &a, const Vec3 &p) {
    return V3(a.m[0] * p.x + a.m[1] * p.y + a.m[2] * p.z,
              a.m[4] * p.x + a.m[5] * p.y + a.m[6] * p.z,
              a.m[8] * p.x + a.m[9] * p.y + a.m[10] * p.z);
}

// R = Rz * Ry * Rx (convención H3D verificada: rota primero en X), luego escala y traslación.
static inline void M34FromSRT(Mat34 &o, const float s[3], const float r[3], const float t[3]) {
    float cx = cosf(r[0]), sx = sinf(r[0]);
    float cy = cosf(r[1]), sy = sinf(r[1]);
    float cz = cosf(r[2]), sz = sinf(r[2]);
    float R[9] = {
        cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx,
        sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx,
        -sy,     cy * sx,                cy * cx
    };
    for (int i = 0; i < 3; i++) {
        o.m[i * 4 + 0] = R[i * 3 + 0] * s[0];
        o.m[i * 4 + 1] = R[i * 3 + 1] * s[1];
        o.m[i * 4 + 2] = R[i * 3 + 2] * s[2];
        o.m[i * 4 + 3] = t[i];
    }
}

// Rotación de 'ang' radianes alrededor de un eje unitario (sin traslación)
static inline void M34AxisAngle(Mat34 &o, const Vec3 &axis, float ang) {
    float c = cosf(ang), s = sinf(ang), C = 1.f - c;
    float x = axis.x, y = axis.y, z = axis.z;
    float R[12] = {
        c + x * x * C,     x * y * C - z * s, x * z * C + y * s, 0,
        y * x * C + z * s, c + y * y * C,     y * z * C - x * s, 0,
        z * x * C - y * s, z * y * C + x * s, c + z * z * C,     0
    };
    memcpy(o.m, R, sizeof(R));
}

// Solo la parte 3x3 transpuesta (inversa de una rotación pura)
static inline void M34TransposeRot(Mat34 &o, const Mat34 &a) {
    float r[12] = {
        a.m[0], a.m[4], a.m[8], 0,
        a.m[1], a.m[5], a.m[9], 0,
        a.m[2], a.m[6], a.m[10], 0
    };
    memcpy(o.m, r, sizeof(r));
}

// Extrae la rotación (normalizando columnas para quitar la escala)
static inline void M34RotationOnly(Mat34 &o, const Mat34 &a) {
    for (int c = 0; c < 3; c++) {
        Vec3 col = V3(a.m[c], a.m[4 + c], a.m[8 + c]);
        col = Normalize(col);
        o.m[c] = col.x; o.m[4 + c] = col.y; o.m[8 + c] = col.z;
    }
    o.m[3] = o.m[7] = o.m[11] = 0.f;
}

// Matriz 4x4 fila mayor (para view-projection): clip = M * (x,y,z,1)
struct Mat44 {
    float m[16];
};

static inline void M44Mul(Mat44 &o, const Mat44 &a, const Mat44 &b) {
    Mat44 r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i * 4 + j] = a.m[i * 4 + 0] * b.m[j] + a.m[i * 4 + 1] * b.m[4 + j] +
                             a.m[i * 4 + 2] * b.m[8 + j] + a.m[i * 4 + 3] * b.m[12 + j];
    o = r;
}

static inline void M44FromM34(Mat44 &o, const Mat34 &a) {
    memcpy(o.m, a.m, sizeof(a.m));
    o.m[12] = o.m[13] = o.m[14] = 0.f;
    o.m[15] = 1.f;
}

static inline void M44Point(const Mat44 &a, const Vec3 &p, float out[4]) {
    for (int i = 0; i < 4; i++)
        out[i] = a.m[i * 4 + 0] * p.x + a.m[i * 4 + 1] * p.y + a.m[i * 4 + 2] * p.z + a.m[i * 4 + 3];
}
