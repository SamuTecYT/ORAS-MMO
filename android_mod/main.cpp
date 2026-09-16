#include <3ds.h>
#include <3ds/services/soc.h>
#include <malloc.h>
#include <CTRPluginFramework.hpp>
#include "NetworkClient.hpp"
#include "Entity.hpp"
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <cstring>
#include <malloc.h>
#include <cstdio>

using namespace CTRPluginFramework;

NetworkClient g_client;
EntityManager g_entityManager;

std::string g_roomCode = "1234";
std::string g_playerIdStr = "";
int g_connectedPlayers = 0;
bool g_isRunning = true;
std::mutex g_playerMutex;

struct RemotePlayer {
    bool active = false;
    bool in_battle = false;
    PlayerPacket last_pkt;
    u64 last_update_time;
};

// Slot 1-5 for remote players
std::vector<RemotePlayer> g_remotePlayers(6); 

u64 g_entitySearchWait = 0;
bool g_inBattle = false; 

// Save safety variables
std::atomic<bool> g_isSaving(false);
u32 g_saveStateAddr = 0x08C6FFF0; // Placeholder for ORAS save flag in RAM
u64 g_lastReminderTime = 0;

// Encounter Independence Design:
// - Encounter data is local-only. The plugin only syncs X, Y, MapID, Facing.
// - It does NOT sync encounter tables, Pokemon data, or battle results.
// - Each player's encounters are 100% independent.
// - No shared RNG: The plugin does not touch any RNG seeds or encounter rate memory.
//   The memory scan in find_entity_base() ONLY scans the entity region (0x08C00000-0x08E00000).

// Dynamic pointers
u32 local_player_x = 0;
u32 local_player_y = 0;
u32 local_player_map = 0;
u32 local_player_facing = 0;

u32 g_currentMap = 0;

// Dynamic pointer discovery
u32 find_entity_base() {
    for (u32 addr = 0x08C00000; addr < 0x08E00000; addr += 4) {
        if (!Process::CheckAddress(addr)) continue;
        
        u32 modelID = 0;
        Process::Read32(addr + 0x04, modelID);
        
        // Check for May (171) or Brendan (172)
        if (modelID == 171 || modelID == 172) {
            u32 vis = 0;
            Process::Read32(addr + 0x18, vis);
            if (vis == 1) {
                float x = 0.0f;
                Process::ReadFloat(addr + 0x08, x);
                
                // Handle case where player X = 0.0 (not in overworld yet) gracefully
                if (x != 0.0f) {
                    local_player_x = addr + 0x08;
                    local_player_y = addr + 0x0C;
                    local_player_map = addr - 0x10; // Guessed offset based on initial hardcodes
                    local_player_facing = addr - 0x0E;
                    return addr;
                }
            }
        }
    }
    return 0;
}

void NetworkThread(void* arg) {
    while (g_isRunning) {
        if (!g_client.is_connected()) {
            OSD::Notify("MMO: Conectando al servidor...");
            
            // Read IP from file
            std::string serverIP = "127.0.0.1";
            FILE* f = fopen("sdmc:/mmo_ip.txt", "r");
            if (f) {
                char buf[64] = {0};
                if (fgets(buf, 63, f)) {
                    serverIP = buf;
                    while (!serverIP.empty() && (serverIP.back() == '\r' || serverIP.back() == '\n' || serverIP.back() == ' '))
                        serverIP.pop_back();
                }
                fclose(f);
            }
            
            if (g_client.connect(serverIP, 9000, 5000)) { 
                if (g_client.send_handshake(g_roomCode)) {
                    std::string resp = g_client.recv_handshake();
                    if (resp.find("OK:ROOM:") != std::string::npos) {
                        OSD::Notify("MMO: Conectado a la sala " + g_roomCode);
                        g_playerIdStr = resp.substr(resp.find_last_of(':') + 1);
                    } else {
                        g_client.disconnect();
                    }
                }
            } else {
                Sleep(Seconds(5)); 
                continue;
            }
        }

        // Send local player position
        PlayerPacket out_pkt;
        memset(&out_pkt, 0, sizeof(PlayerPacket));
        out_pkt.type = 0x01;
        
        if (Process::CheckAddress(local_player_x)) {
            Process::ReadFloat(local_player_x, out_pkt.x);
            Process::ReadFloat(local_player_y, out_pkt.y);
            Process::Read32(local_player_map, out_pkt.map_id);
            Process::Read8(local_player_facing, out_pkt.facing);
            out_pkt.model_id = 171; // Default model May

            g_client.send_packet((uint8_t*)&out_pkt, sizeof(PlayerPacket));
        }

        // Detect map change to hide existing ghost entities
        if (out_pkt.map_id != g_currentMap) {
            g_currentMap = out_pkt.map_id;
            std::lock_guard<std::mutex> lock(g_playerMutex);
            for (int i = 1; i <= 5; i++) {
                g_entityManager.hide_player(i);
            }
        }

        // Receive remote players packets
        PlayerPacket in_pkt;
        int bytes = g_client.recv_packet((uint8_t*)&in_pkt, sizeof(PlayerPacket));
        if (bytes == sizeof(PlayerPacket)) {
            std::lock_guard<std::mutex> lock(g_playerMutex);
            if (in_pkt.type == 0x01 && in_pkt.player_id > 0 && in_pkt.player_id <= 5) {
                g_remotePlayers[in_pkt.player_id].active = true;
                g_remotePlayers[in_pkt.player_id].last_pkt = in_pkt;
                g_remotePlayers[in_pkt.player_id].last_update_time = svcGetSystemTick();
            } else if (in_pkt.type == 0x02 && in_pkt.player_id > 0 && in_pkt.player_id <= 5) {
                g_remotePlayers[in_pkt.player_id].in_battle = true;
            } else if (in_pkt.type == 0x03 && in_pkt.player_id > 0 && in_pkt.player_id <= 5) {
                g_remotePlayers[in_pkt.player_id].in_battle = false;
            } else if (in_pkt.type == 0x10 && in_pkt.player_id > 0 && in_pkt.player_id <= 5) {
                g_remotePlayers[in_pkt.player_id].active = false;
                g_entityManager.hide_player(in_pkt.player_id);
            } else if (in_pkt.type == 0x20) {
                OSD::Notify("Jugador " + std::to_string(in_pkt.player_id) + " te desafia a combate!");
            } else if (in_pkt.type == 0x21) {
                OSD::Notify("Jugador " + std::to_string(in_pkt.player_id) + " quiere intercambiar!");
            }
        } else if (bytes < 0) {
            std::lock_guard<std::mutex> lock(g_playerMutex);
            for (auto& p : g_remotePlayers) p.active = false;
        }

        Sleep(Milliseconds(33)); // Approx 30Hz network tick
    }
}

// Executed every frame
void OnFrameCallback(Time time) {
    u64 current_time = svcGetSystemTick();

    // 1. Auto-reminder
    if (g_lastReminderTime == 0) g_lastReminderTime = current_time;
    if (current_time - g_lastReminderTime > (20ULL * 60 * 1000 * 1000 * 268)) {
        OSD::Notify("Recuerda guardar tu partida (START -> Guardar)");
        g_lastReminderTime = current_time;
    }

    // 2. Save Hook Detection
    // Since we don't have the exact ORAS save flag address, we simulate it by checking g_saveStateAddr.
    // In a real release, this pointer would be dynamically resolved.
    u32 saveFlagVal = 0;
    if (Process::CheckAddress(g_saveStateAddr)) {
        Process::Read32(g_saveStateAddr, saveFlagVal);
    }
    
    // If saving started
    if (saveFlagVal != 0 && !g_isSaving) {
        g_isSaving = true;
        // Hide and zero all entities to prevent save corruption
        std::lock_guard<std::mutex> lock(g_playerMutex);
        for (int i = 1; i <= 5; ++i) {
            g_entityManager.clear_player(i);
        }
    }
    // If saving ended
    else if (saveFlagVal == 0 && g_isSaving) {
        g_isSaving = false;
    }

    // If we are saving, pause all entity writes
    if (g_isSaving) {
        return;
    }

    // Check if player is still valid (Battle check fix)
    bool isPlayerValid = false;
    if (local_player_x != 0 && Process::CheckAddress(local_player_x)) {
        float x = 0;
        if (Process::ReadFloat(local_player_x, x)) {
            isPlayerValid = (x != 0.0f);
        }
    }

    if (!isPlayerValid || g_entityManager.get_entity_addr(0) == 0) {
        if (current_time > g_entitySearchWait) {
            u32 new_base = find_entity_base();
            if (new_base == 0) {
                if (!g_inBattle) {
                    g_inBattle = true;
                    PlayerPacket pkt; memset(&pkt, 0, sizeof(pkt)); pkt.type = 0x02;
                    g_client.send_packet((uint8_t*)&pkt, sizeof(pkt));
                }
                g_entitySearchWait = current_time + (268123480ULL * 2); // 2 seconds wait
            } else {
                if (g_inBattle) {
                    g_inBattle = false;
                    PlayerPacket pkt; memset(&pkt, 0, sizeof(pkt)); pkt.type = 0x03;
                    g_client.send_packet((uint8_t*)&pkt, sizeof(pkt));
                }
                g_entityManager.set_base_address(new_base);
            }
        }
    }

    std::lock_guard<std::mutex> lock(g_playerMutex);
    
    int active_players = 0;
    
    for (int i = 1; i <= 5; ++i) {
        if (g_remotePlayers[i].active) {
            // Despawn if no packets received in 3 seconds (timeout)
            if (current_time - g_remotePlayers[i].last_update_time > 3000000ULL * 268) { // Ticks approx
                g_remotePlayers[i].active = false;
                if (!g_inBattle) g_entityManager.hide_player(i);
            } else {
                if (!g_inBattle && g_remotePlayers[i].last_pkt.map_id == g_currentMap) {
                    if (g_remotePlayers[i].in_battle) {
                        PlayerPacket b_pkt = g_remotePlayers[i].last_pkt;
                        b_pkt.facing = 0; // battle pose
                        g_entityManager.update_remote_player(i, b_pkt);
                    } else {
                        g_entityManager.update_remote_player(i, g_remotePlayers[i].last_pkt);
                    }
                }
                active_players++;
            }
        }
    }
    g_connectedPlayers = active_players;
}

// Draws the OSD
bool OnOverlayDraw(const Screen& screen) {
    if (screen.IsTop) {
        screen.Draw("MMO: " + std::to_string(g_connectedPlayers) + " jugadores conectados", 10, 10, Color::White, Color::Black);
        screen.Draw("Sala: " + g_roomCode, 10, 20, Color::White, Color::Black);
    }
    return true;
}

// Menu callbacks
void MenuPlayersNearby(MenuEntry *entry) {
    std::vector<std::string> player_names;
    std::vector<int> player_ids;
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        for (int i = 1; i <= 5; ++i) {
            if (g_remotePlayers[i].active && g_remotePlayers[i].last_pkt.map_id == g_currentMap) {
                player_names.push_back("Jugador " + std::to_string(i));
                player_ids.push_back(i);
            }
        }
    }
    
    if (player_names.empty()) {
        MessageBox("No hay jugadores cercanos en tu mapa.")();
        return;
    }
    
    Keyboard kb("Selecciona un jugador:");
    kb.Populate(player_names);
    int choice = kb.Open();
    if (choice >= 0 && choice < player_names.size()) {
        int target = player_ids[choice];
        std::vector<std::string> actions = {"Solicitar Combate Amistoso", "Solicitar Intercambio"};
        Keyboard kb_act("Selecciona una accion:");
        kb_act.Populate(actions);
        int act = kb_act.Open();
        if (act == 0) {
            PlayerPacket pkt;
            memset(&pkt, 0, sizeof(pkt));
            pkt.type = 0x20;
            pkt.anim = target; // use anim byte to pass target_id
            g_client.send_packet((uint8_t*)&pkt, sizeof(pkt));
            MessageBox("Solicitud enviada.\nSi acepta, usa la conexion Inalambrica.")();
        } else if (act == 1) {
            PlayerPacket pkt;
            memset(&pkt, 0, sizeof(pkt));
            pkt.type = 0x21;
            pkt.anim = target;
            g_client.send_packet((uint8_t*)&pkt, sizeof(pkt));
            MessageBox("Solicitud enviada.\nSi acepta, usa el PSS para el Intercambio.")();
        }
    }
}

void MenuConfig(MenuEntry *entry) {
    std::vector<std::string> opts = {"Cambiar Sala", "Ver IP del Servidor"};
    Keyboard kb("Configuracion");
    kb.Populate(opts);
    int choice = kb.Open();
    if (choice == 0) {
        Keyboard kb_room("Introduce nueva sala (4 chars):");
        std::string new_room;
        if (kb_room.Open(new_room) == 0) {
            g_roomCode = new_room;
            g_client.disconnect(); // force reconnect
        }
    } else if (choice == 1) {
        MessageBox("IP del Servidor:\nLee de sdmc:/mmo_ip.txt")();
    }
}

void MenuStatus(MenuEntry *entry) {
    MessageBox("Estado:\n" + std::to_string(g_connectedPlayers) + " jugadores conectados.")();
}

int main(void) {
    u32 *soc_buffer = (u32*)memalign(0x1000, 0x100000);
    if (soc_buffer) {
        socInit(soc_buffer, 0x100000);
    }

    PluginMenu *menu = new PluginMenu("ORAS MMO", 1, 0, 0);
    menu->SynchronizeWithFrame(true);
    
    MenuFolder *folder_players = new MenuFolder("Jugadores Cercanos");
    folder_players->Append(new MenuEntry("Ver lista de jugadores", nullptr, MenuPlayersNearby));
    menu->Append(folder_players);
    
    MenuFolder *folder_cfg = new MenuFolder("Configuracion");
    folder_cfg->Append(new MenuEntry("Opciones", nullptr, MenuConfig));
    menu->Append(folder_cfg);
    
    menu->Append(new MenuEntry("Estado del Servidor", nullptr, MenuStatus));
    
    OSD::Run(OnOverlayDraw);
    
    // Create standard CTRPluginFramework Thread
    Thread networkThread = threadCreate(NetworkThread, nullptr, 1024 * 4, 0x18, -1, false);
    // networkThread.Start(); (started by threadCreate)

    // Attach loop
    menu->OnNewFrame = OnFrameCallback;

    menu->Run();

    // Cleanup
    g_isRunning = false;
    if (networkThread) { threadJoin(networkThread, U64_MAX); threadFree(networkThread); }
    
    // SAFE SHUTDOWN: clear all injected entities before exiting
    {
        std::lock_guard<std::mutex> lock(g_playerMutex);
        for (int i = 1; i <= 5; ++i) {
            g_entityManager.clear_player(i);
        }
    }

    delete menu;
    
    if (soc_buffer) {
        socExit();
        free(soc_buffer);
    }
    
    return 0;
}
