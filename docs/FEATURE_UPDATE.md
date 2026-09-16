# Project ORAS MMO - Feature Update

## New Features Added
### 1. Social Interaction Menu
A new CTRPF plugin menu has been implemented and is accessible in-game. It includes the following structure:
- **Jugadores Cercanos**: View a list of players in the same map.
  - Selecting a player allows sending:
    - **Solicitar Combate Amistoso**: Sends a battle request.
    - **Solicitar Intercambio**: Sends a trade request.
- **Configuración**: 
  - Change room code in-game dynamically.
  - View Server IP from config file.
- **Estado**: Shows the count of currently connected players.

### 2. Networking Opcodes for Social Actions
- `PKT_BATTLE_REQUEST (0x20)`: Request an amicable battle.
- `PKT_TRADE_REQUEST (0x21)`: Request a trade.
Both requests utilize the standard server structure, but the python relay server (`server.py`) has been upgraded to route these specific opcodes directly to the intended `target_id` using the `anim_frame` byte, ensuring private targeted delivery instead of global broadcast. When a player receives a request, an on-screen notification is presented via `OSD::Notify`.
