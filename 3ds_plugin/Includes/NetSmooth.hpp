/*
 * NetSmooth.hpp — ORAS MMO v16
 * Colchón de red por jugador: cuanto más irregular llega su conexión (jitter), más atrás en el tiempo se le
 * dibuja, para que siempre haya dos posiciones reales entre las que interpolar (sin tirones). Quien tiene buena
 * conexión se sigue viendo casi en directo (100 ms).
 * Funciones puras constexpr (C++11); los static_assert son las pruebas.
 */
#pragma once

namespace NetSmooth {

static constexpr int DELAY_MIN_MS = 100, DELAY_MAX_MS = 450;
static constexpr int EXTRAPOLATE_MAX_MS = 250;   // predicción máxima si se retrasa un paquete...
static constexpr float EXTRAPOLATE_MAX_DIST = 18.f;   // ...y nunca más de una casilla
static constexpr float CORRECT_MS = 150.f;       // un error de posición se corrige en este tiempo, no de golpe

// v16.1: el colchón sube al instante si la conexión empeora y baja despacio (40 ms por segundo) si mejora.
// Antes seguía a la media del jitter, que cambia cada medio segundo, y para alcanzarla el reloj de reproducción
// iba hasta un 60 % más rápido o lento: el amigo se veía acelerado y a tirones (mucho más en bici).
static constexpr float HOLD_DECAY_MS_PER_S = 40.f;
constexpr float HoldDelay(float heldMs, float targetMs, float dtMs) {
    return targetMs >= heldMs ? targetMs
         : (heldMs - targetMs <= HOLD_DECAY_MS_PER_S * dtMs / 1000.f ? targetMs : heldMs - HOLD_DECAY_MS_PER_S * dtMs / 1000.f);
}
// Velocidad del reloj de reproducción para corregir un desfase de errMs: como mucho ±8 % (no se nota)
static constexpr double RATE_MAX_DEV = 0.08;
constexpr double PlayRate(double errMs) {
    return errMs / 2000.0 > RATE_MAX_DEV ? 1.0 + RATE_MAX_DEV : errMs / 2000.0 < -RATE_MAX_DEV ? 1.0 - RATE_MAX_DEV : 1.0 + errMs / 2000.0;
}

// v16.1: ritmo del reloj del emisor frente al mío, medido en ventanas de >= 5 s (su emulador puede ir al 85 % o
// al 130 %): media móvil 1/4 de cada medida; medidas absurdas (pausas, reinicios) se ignoran.
static constexpr double SKEW_MIN = 0.5, SKEW_MAX = 1.6;
constexpr double SkewUpdate(double skew, double senderMs, double localMs) {
    return (localMs <= 0.0 || senderMs / localMs < SKEW_MIN || senderMs / localMs > SKEW_MAX) ? skew
         : skew + (senderMs / localMs - skew) * 0.25;
}

// jitter medio (ms) -> retraso de dibujo (ms): 90 + 2.5 * jitter, entre 100 y 450
constexpr int JitterDelayMs(int jitterMs) {
    return (90 + jitterMs * 5 / 2) < DELAY_MIN_MS ? DELAY_MIN_MS
         : (90 + jitterMs * 5 / 2) > DELAY_MAX_MS ? DELAY_MAX_MS
         : (90 + jitterMs * 5 / 2);
}

// Media móvil del jitter (1/16 por muestra), en ms x16 para no perder precisión. 'sampleMs' = |Δllegada − Δemisor|
constexpr int JitterUpdateQ4(int jitterQ4, int sampleMs) {
    return jitterQ4 + ((sampleMs > 1000 ? 1000 : sampleMs) * 16 - jitterQ4) / 16;
}

static_assert(JitterDelayMs(0) == 100, "sin jitter: minimo 100 ms");
static_assert(JitterDelayMs(40) == 190, "40 ms de jitter -> 190 ms");
static_assert(JitterDelayMs(1000) == 450, "tope 450 ms");
static_assert(JitterUpdateQ4(0, 160) == 160, "1/16 de 160 ms = 10 ms (x16 = 160)");
static_assert(JitterUpdateQ4(160 * 16, 160) == 160 * 16, "estable si la muestra es igual a la media");
static_assert(HoldDelay(200.f, 300.f, 33.f) == 300.f, "si la conexion empeora, el colchon sube al instante");
static_assert(HoldDelay(300.f, 100.f, 1000.f) == 260.f, "si mejora, baja 40 ms por segundo");
static_assert(HoldDelay(101.f, 100.f, 1000.f) == 100.f, "sin pasarse del objetivo al bajar");
static_assert(PlayRate(0.0) == 1.0, "sin desfase: velocidad normal");
static_assert(PlayRate(5000.0) == 1.0 + RATE_MAX_DEV && PlayRate(-5000.0) == 1.0 - RATE_MAX_DEV, "tope +-8 %");
static_assert(PlayRate(100.0) > 1.049 && PlayRate(100.0) < 1.051, "100 ms de desfase: 5 % mas rapido");
static_assert(SkewUpdate(1.0, 5000.0, 5000.0) == 1.0, "mismo ritmo: 1");
static_assert(SkewUpdate(1.0, 4000.0, 5000.0) > 0.9499 && SkewUpdate(1.0, 4000.0, 5000.0) < 0.9501, "emisor al 80 %: se acerca 1/4 por medida");
static_assert(SkewUpdate(1.0, 1000.0, 5000.0) == 1.0 && SkewUpdate(1.0, 9000.0, 5000.0) == 1.0, "medidas absurdas: se ignoran");

} // namespace NetSmooth
