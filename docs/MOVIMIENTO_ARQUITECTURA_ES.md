# 🗺️ Arquitectura de Movimiento — Por qué NUNCA te trabarás

## El Principio Fundamental (En español simple)

> **El servidor NUNCA controla tu movimiento. Tú mueves al instante, siempre.**
> El servidor solo avisa a los DEMÁS dónde estás. Si el servidor se cae, tú sigues jugando normal — solo desaparecen los demás jugadores.

## Las 4 Optimizaciones Implementadas en v3

### OPT-1: Filtrado por Zona (Zone-Based Relay)

```
Sin OPT-1:
  Tú estás en Petalburg → envías posición → servidor la manda a los 5 jugadores
  → aunque 3 de ellos están en Mauville (¡no te pueden ver!) = tráfico inútil

Con OPT-1:
  Tú estás en Petalburg → servidor compara tu map_id con el de cada jugador
  → solo manda tu posición a los que TAMBIÉN están en Petalburg
  → ahorro de tráfico: hasta 80% menos paquetes en mapas grandes
```

### OPT-2: Límite de Velocidad de Paquetes (Rate Limiter)

- Cuando **caminas**: máximo 20 paquetes por segundo por jugador
- Cuando estás **quieto** (OPT-4): máximo 1 paquete por segundo ("heartbeat")
- Si un bug envía más, el servidor descarta el exceso en silencio — **el juego NO se traba**

### OPT-3: Velocidad para Dead Reckoning (Interpolación)

El paquete ahora incluye `vel_x` y `vel_y` (velocidad del jugador en ese momento).

```
Sin dead reckoning:
  Recibes posición (X=100, Y=200) → NPC salta de golpe → jitter visual

Con dead reckoning:
  Recibes posición + velocidad → el cliente predice dónde estará el NPC
  en los siguientes 50ms → movimiento suave aunque haya latencia de 100ms
```

### OPT-4: Modo Quieto (Stationary Flag)

```
Jugador quieto → paquete marca bit FLAG_STATIONARY = 1
Servidor detecta → aplica límite de 1 paquete/seg en lugar de 20/seg
Resultado: un jugador AFK consume 95% menos ancho de banda
```

## Diagrama de Flujo Completo

```
[Tú presionas ←↑→↓]
      │
      ▼
[El juego se mueve INMEDIATAMENTE — sin esperar al servidor]
      │
      ▼ (cada 50ms si te mueves, cada 1s si estás quieto)
[Hook ARM11 lee tu X/Y/map_id de la RAM]
      │
      ▼
[Plugin .3gx empaqueta 64 bytes + vel_x/vel_y]
      │
      ▼ (socket TCP via SOC:U de Azahar)
[Servidor recibe paquete]
      │
      ├─► ¿map_id coincide con Jugador 2? → SÍ → reenvía a Jugador 2
      ├─► ¿map_id coincide con Jugador 3? → NO (está en otra zona) → DESCARTA
      ├─► ¿map_id coincide con Jugador 4? → SÍ → reenvía a Jugador 4
      └─► etc.

[Jugadores 2 y 4 reciben paquete]
      │
      ▼
[Plugin .3gx en sus consolas actualiza la posición del NPC "Tú"]
[Dead reckoning interpola el movimiento hasta el próximo paquete]
[= movimiento SUAVE, sin saltos, sin trabas]
```

## Qué pasa en Situaciones Extremas

| Situación | Lo que ocurre |
|---|---|
| Conexión lenta (100-200ms) | Dead reckoning predice posición → movimiento suave para los demás |
| Pérdida de paquetes (4G inestable) | NPC usa última posición conocida + velocidad → se corrige al siguiente paquete |
| Servidor caído | Tu juego sigue normal solo. Los NPCs de otros desaparecen. |
| Jugador en otro mapa | Sus paquetes NO llegan a ti (filtro OPT-1). Cero impacto. |
| Jugador AFK 30 min | Solo 1 paquete/seg desde ese jugador. Carga mínima. |
| Viaje rápido / Fly | PKT_MAP_CHANGE actualiza zona inmediatamente. NPCs se reposicionan en el nuevo mapa. |
| Cambio de zona (ruta → ciudad) | PKT_MAP_CHANGE limpia cualquier lock activo y actualiza zone. |

## Packet Format v3 (64 bytes)

```
Offset  Bytes  Campo           Descripción
0       1      packet_type     0x01=posición, 0x06=cambio de mapa, etc.
1       1      player_id       1-6 (asignado por servidor)
2       1      locale          0=EN, 1=ES
3       1      reservado       siempre 0
4       4      map_id          ID de zona/mapa actual (uint32)
8       4      pos_x           Posición X en el mundo (float32)
12      4      pos_y           Posición Y en el mundo (float32)
16      4      pos_z           Posición Z / altura (float32)
20      1      facing          0=arriba, 1=abajo, 2=izq, 3=der
21      1      anim_frame      0-7 frame de animación de caminar
22      1      battle_type     0=libre, 1=salvaje, 2=entrenador
23      1      flags           bit0=cinemática, bit1=batalla, bit4=quieto ←NUEVO
24      6      pokemon_levels  Nivel de cada Pokémon del equipo (6 slots)
30      4      vel_x           Velocidad X para dead reckoning ←NUEVO
34      4      vel_y           Velocidad Y para dead reckoning ←NUEVO
38      26     reservado       Cero, para uso futuro
```
