// ORAS MMO HD (todo en 1) - shader de post-procesado para Azahar (renderizador OpenGL)
//
//  1. Suavizado de bordes (FXAA) para quitar los dientes de sierra del 3D escalado.
//  2. Nitidez adaptativa (más detalle en texturas, sin halos en los bordes).
//  3. Iluminación según la escena, detectada sola en cada cuadro y mezclada suavemente:
//       - día soleado: luz cálida, colores vivos y sol más brillante;
//       - atardecer / interiores cálidos: tono dorado;
//       - noche / cuevas: luz de luna azulada que deja ver sin lavar los negros;
//       - mar: azules más profundos y destellos en el agua.
//  4. Resplandor (bloom) en luces y brillos, tono fílmico y viñeta suave.
//
// Uso: Azahar > Emulation > Configure > Graphics > Enhancements > Post-Processing Shader > ORAS_MMO_HD

#define AA_STRENGTH   0.70
#define SHARPEN       0.45
#define VIBRANCE      0.45
#define CONTRAST      0.22
#define BLOOM         0.28
#define VIGNETTE      0.12

float Luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float3 Tonemap(float3 x) {                 // curva fílmica suave (estilo ACES, normalizada para no oscurecer)
    float3 a = x * (2.51 * x + 0.03);
    float3 b = x * (2.43 * x + 0.59) + 0.14;
    return clamp(a / b, 0.0, 1.0) * 1.08;
}

void main()
{
    float2 uv = GetCoordinates();
    float2 px = GetInvResolution();

    // ---- 1. FXAA (versión compacta) ----
    float3 cM = Sample().rgb;
    float3 cN = SampleLocation(uv + float2(0.0, -px.y)).rgb;
    float3 cS = SampleLocation(uv + float2(0.0,  px.y)).rgb;
    float3 cE = SampleLocation(uv + float2( px.x, 0.0)).rgb;
    float3 cW = SampleLocation(uv + float2(-px.x, 0.0)).rgb;
    float lM = Luma(cM), lN = Luma(cN), lS = Luma(cS), lE = Luma(cE), lW = Luma(cW);
    float lMin = min(lM, min(min(lN, lS), min(lE, lW)));
    float lMax = max(lM, max(max(lN, lS), max(lE, lW)));
    float range = lMax - lMin;
    float3 c = cM;
    if (range > max(0.10, lMax * 0.22)) {                                      // solo bordes de geometría, no la textura
        float2 dir = float2(-((lN + lS) - (lE + lW)), (lN + lE) - (lS + lW)) * 0.5;
        float dirReduce = max((lN + lS + lE + lW) * 0.03125, 0.0078125);
        float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
        dir = clamp(dir * rcpMin, float2(-4.0), float2(4.0)) * px;
        float3 a = 0.5 * (SampleLocation(uv + dir * (1.0 / 3.0 - 0.5)).rgb + SampleLocation(uv + dir * (2.0 / 3.0 - 0.5)).rgb);
        float3 b = a * 0.5 + 0.25 * (SampleLocation(uv - dir * 0.5).rgb + SampleLocation(uv + dir * 0.5).rgb);
        float lb = Luma(b);
        float3 aa = (lb < lMin || lb > lMax) ? a : b;
        c = mix(cM, aa, AA_STRENGTH);
    }

    // ---- 2. Nitidez adaptativa ----
    float3 mn = min(c, min(min(cN, cS), min(cE, cW)));
    float3 mx = max(c, max(max(cN, cS), max(cE, cW)));
    float3 amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 0.001), 0.0, 1.0)) * SHARPEN * 0.22;
    c = clamp(c + (c * 4.0 - (cN + cS + cE + cW)) * amp, 0.0, 1.0);

    // ---- 3. Escena: 9 muestras repartidas ----
    float3 acc = float3(0.0);
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            acc += SampleLocation(float2(0.2 + 0.3 * float(i), 0.2 + 0.3 * float(j))).rgb;
    acc /= 9.0;
    float sceneL = Luma(acc);
    float night  = clamp((0.24 - sceneL) / 0.16, 0.0, 1.0);                  // noche / cueva
    float sunny  = clamp((sceneL - 0.38) / 0.20, 0.0, 1.0);                  // exterior luminoso
    float warmSc = clamp((acc.r - acc.b) * 3.0, 0.0, 1.0) * (1.0 - night);   // atardecer / interior cálido
    float seaSc  = clamp((acc.b - max(acc.r, acc.g * 0.9)) * 4.0, 0.0, 1.0); // mar a la vista

    float l = Luma(c);
    // día soleado: luces cálidas y sombras un pelín frías (más volumen)
    float3 sunTint = mix(float3(0.98, 0.99, 1.03), float3(1.035, 1.01, 0.965), smoothstep(0.25, 0.85, l));
    c *= mix(float3(1.0), sunTint, sunny * 0.9);
    c += sunny * 0.025 * smoothstep(0.6, 1.0, l);                              // sol más brillante
    // atardecer / interior: dorado
    c = mix(c, c * float3(1.06, 0.98, 0.86) + float3(0.02, 0.01, 0.0), warmSc * (1.0 - sunny) * (1.0 - sceneL) * 0.35);
    // noche: luz de luna (levanta sombras con azul frío)
    float lift = night * (1.0 - l) * 0.22;
    c += lift * float3(0.45, 0.62, 1.0) * (0.4 + l);
    c = mix(c, c * float3(0.92, 0.97, 1.10), night * 0.5);

    // mar: azules profundos y destellos
    float maxc = max(c.r, max(c.g, c.b)), minc = min(c.r, min(c.g, c.b));
    float sat = (maxc - minc) / max(maxc, 0.001);
    float water = clamp((c.b - max(c.r, c.g * 0.85)) * 4.0, 0.0, 1.0) * clamp(sat * 2.0, 0.0, 1.0);
    float crest = clamp((l - (lN + lS + lE + lW) * 0.25) * 8.0, 0.0, 1.0);
    c = mix(c, c * float3(0.86, 1.0, 1.12) + crest * 0.22, water * (0.45 + seaSc * 0.35));

    // ---- viveza (más en colores apagados, menos en pieles/saturados) ----
    float lum = Luma(c);
    c = mix(float3(lum), c, 1.0 + VIBRANCE * (1.0 - sat) * (1.0 - night * 0.5) * (1.0 - smoothstep(0.7, 0.95, lum)));   // los blancos siguen blancos

    // ---- 4. Resplandor: dos anillos de muestras de lo que brilla ----
    float3 glow = float3(0.0);
    float thr = mix(0.86, 0.60, night);                                       // solo luces de verdad (de noche, más)
    for (int k = 0; k < 8; k++) {
        float ang = float(k) * 0.785398;
        float2 o = float2(cos(ang), sin(ang)) * px;
        glow += max(SampleLocation(uv + o * 3.0).rgb - thr, 0.0);
        glow += max(SampleLocation(uv + o * 7.0).rgb - thr, 0.0) * 0.6;
    }
    float bloomScene = clamp(1.2 - sceneL * 1.5, 0.15, 1.0);                  // escenas ya luminosas: casi sin bloom
    c += glow * (BLOOM * bloomScene * (0.7 + night * 0.9) / 12.8) * 3.0;

    // ---- contraste en S (más profundidad sin perder blancos) + viñeta ----
    c = clamp(c, 0.0, 1.0);
    c = mix(c, c * c * (3.0 - 2.0 * c), CONTRAST * (1.0 - night * 0.6));
    float2 d = uv - 0.5;
    c *= 1.0 - VIGNETTE * dot(d, d) * 1.7;

    SetOutput(float4(clamp(c, 0.0, 1.0), 1.0));
}
