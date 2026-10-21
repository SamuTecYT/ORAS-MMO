#include "Config.hpp"
#include "Protocol.hpp"
#include "Perf.hpp"
#include <CTRPluginFramework.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace CTRPluginFramework;

namespace Cfg {

bool showHud      = true;
int  hudMode      = 0;
int  mapMode      = 0;
bool showNames    = true;
bool occlusion    = true;
int  character    = CHAR_AUTO;
int  colorVariant = 0;
int  accColor     = 0;
int  gameLang     = 0;
int  tagColor     = 0;
bool showMySkin   = true;
int  followers    = 0;
int  perfMode     = 0;
bool nativeMode   = true;    // v16.1: siempre activas para todos (la opcion se quito del menu)
int  origGender   = -1;
bool hdCanvas     = false;
bool hideMe       = false;

static Mutex       s_mutex;
static std::string s_manualIp;
static int s_manualPort = 0;            // 0 = MMO_DEFAULT_PORT
static std::string s_room = MMO_DEFAULT_ROOM;
static std::string s_name = "Jugador";

static std::string Trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && (unsigned char)s[a] <= ' ') a++;
    while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
    return s.substr(a, b - a);
}

static bool ReadSmall(const char *path, std::string &out) {
    File f(path, File::READ);
    if (!f.IsOpen()) return false;
    char buf[160] = {0};
    f.Read(buf, sizeof(buf) - 1);
    f.Close();
    out = Trim(buf);
    return !out.empty();
}

static void WriteSmall(const char *path, const std::string &v) {
    File f(path, File::RWC | File::TRUNCATE);
    if (f.IsOpen()) f.Write(v.c_str(), v.size());
}

// El servidor solo acepta 1..16 caracteres ASCII imprimibles: limpiar BOM/acentos de archivos editados a mano
static std::string CleanRoom(const std::string &in) {
    std::string out;
    for (unsigned char c : in) if (c >= 0x21 && c <= 0x7E && out.size() < 16) out += (char)c;
    return out;
}

static bool IsIpLike(const std::string &s) {
    int dots = 0;
    for (char c : s) {
        if (c == '.') dots++;
        else if (c < '0' || c > '9') return false;
    }
    return dots == 3 && s.size() >= 7 && s.size() <= 15;
}

// "a.b.c.d" o "a.b.c.d:puerto" (para quien monte su servidor en otro puerto)
static bool ParseServer(const std::string &s, std::string &ip, int &port) {
    size_t c = s.find(':');
    std::string a = c == std::string::npos ? s : s.substr(0, c);
    int p = 0;
    if (c != std::string::npos) {
        std::string ps = s.substr(c + 1);
        if (ps.empty() || ps.size() > 5) return false;
        for (char ch : ps) if (ch < '0' || ch > '9') return false;
        p = atoi(ps.c_str());
        if (p < 1 || p > 65535) return false;
    }
    if (!IsIpLike(a)) return false;
    ip = a;
    port = p == MMO_DEFAULT_PORT ? 0 : p;
    return true;
}

void Load() {
    std::string v;
    Lock l(s_mutex);
    { std::string ip; int port = 0; if (ReadSmall("mmo_server.txt", v) && ParseServer(v, ip, port)) { s_manualIp = ip; s_manualPort = port; } }
    if (ReadSmall("mmo_room.txt", v)) { std::string r = CleanRoom(v); if (!r.empty()) s_room = r; }
    if (ReadSmall("mmo_name.txt", v)) s_name = v.substr(0, 13);

    if (ReadSmall("mmo_settings.txt", v)) {
        // formato: clave=valor;clave=valor
        char buf[160];
        snprintf(buf, sizeof(buf), "%s", v.c_str());
        for (char *tok = strtok(buf, ";\n"); tok; tok = strtok(nullptr, ";\n")) {
            char *eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = 0;
            int val = atoi(eq + 1);
            if (!strcmp(tok, "hud")) { showHud = val != 0; if (!showHud) hudMode = 2; }
            else if (!strcmp(tok, "hudm")) hudMode = (val >= 0 && val <= 2) ? val : 0;
            else if (!strcmp(tok, "map")) mapMode = (val >= 0 && val <= 2) ? val : 0;
            else if (!strcmp(tok, "names")) showNames = val != 0;
            else if (!strcmp(tok, "occl")) occlusion = val != 0;
            else if (!strcmp(tok, "char")) character = (val >= 0 && val <= 2) ? val : 0;
            else if (!strcmp(tok, "color")) colorVariant = (val >= 0 && val < NUM_COLORS) ? val : 0;
            else if (!strcmp(tok, "acc")) accColor = (val >= 0 && val < NUM_ACC) ? val : 0;
            else if (!strcmp(tok, "glang")) gameLang = (val == 1 || val == 2 || val == 3 || val == 4 || val == 5 || val == 7 || val == 8 || val == 9) ? val : 0;
            else if (!strcmp(tok, "tag")) tagColor = (val >= 0 && val < NUM_TAGS) ? val : 0;
            else if (!strcmp(tok, "myskin")) showMySkin = val != 0;
            else if (!strcmp(tok, "follow")) followers = (val >= 0 && val <= 2) ? val : 0;
            else if (!strcmp(tok, "perf")) perfMode = (val >= 0 && val <= 2) ? val : 0;
            else if (!strcmp(tok, "hd")) hdCanvas = val != 0;
            else if (!strcmp(tok, "og")) origGender = (val == 0 || val == 1) ? val : -1;
        }
    }
    ApplyPerf();
}

void ApplyPerf() { Perf::SetForced(perfMode == 1 ? 0 : perfMode == 2 ? 2 : -1); }

void Save() {
    char buf[176];
    showHud = hudMode != 2;
    snprintf(buf, sizeof(buf), "hud=%d;hudm=%d;map=%d;names=%d;char=%d;color=%d;acc=%d;tag=%d;myskin=%d;follow=%d;og=%d;hd=%d;perf=%d;native=%d;glang=%d",
             showHud ? 1 : 0, hudMode, mapMode, showNames ? 1 : 0, character, colorVariant, accColor, tagColor, showMySkin ? 1 : 0, followers, origGender,
             hdCanvas ? 1 : 0, perfMode, nativeMode ? 1 : 0, gameLang);
    ApplyPerf();
    WriteSmall("mmo_settings.txt", buf);
}

std::string ServerIp() {
    Lock l(s_mutex);
    return s_manualIp.empty() ? std::string(MMO_DEFAULT_SERVER) : s_manualIp;
}

int ServerPort() {
    Lock l(s_mutex);
    return (!s_manualIp.empty() && s_manualPort) ? s_manualPort : MMO_DEFAULT_PORT;
}

bool HasManualIp() {
    Lock l(s_mutex);
    return !s_manualIp.empty();
}

void SetManualIp(const std::string &ip) {
    std::string v = Trim(ip);
    Lock l(s_mutex);
    std::string addr;
    int port = 0;
    if (v.empty() || v == MMO_DEFAULT_SERVER) {
        s_manualIp.clear();
        s_manualPort = 0;
        File::Remove("mmo_server.txt");
    } else if (ParseServer(v, addr, port)) {
        s_manualIp = addr;
        s_manualPort = port;
        WriteSmall("mmo_server.txt", v);
    }
}

std::string Room() {
    Lock l(s_mutex);
    return s_room;
}

void SetRoom(const std::string &room) {
    std::string v = CleanRoom(room);
    if (v.empty()) return;
    Lock l(s_mutex);
    s_room = v;
    WriteSmall("mmo_room.txt", v);
}

std::string Name() {
    Lock l(s_mutex);
    return s_name;
}

void SetName(const std::string &name) {
    std::string v = Trim(name).substr(0, 13);
    if (v.empty()) return;
    Lock l(s_mutex);
    s_name = v;
    WriteSmall("mmo_name.txt", v);
}

} // namespace Cfg
