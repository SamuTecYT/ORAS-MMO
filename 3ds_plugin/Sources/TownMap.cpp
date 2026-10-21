#include "TownMap.hpp"
#include "HoennZones.hpp"
#include "GameText.hpp"
#include "Lang.hpp"
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace TownMap {

bool IsWaterZone(uint16_t zone) {
    if (zone >= hoenn::NUM_ZONES) return true;
    const hoenn::ZoneInfo &z = hoenn::ZONES[zone];
    return z.kind == hoenn::K_SEA_ROUTE || (z.flags & hoenn::F_UNDERWATER) != 0;
}

bool ZoneToImage(uint16_t zone, float x, float y, float &px, float &py) {
    return hoenn::ZoneToTownmap(zone, x, y, px, py);
}

const char *ZoneNameEs(uint16_t zone) {
    if (zone >= hoenn::NUM_ZONES) return "";
    return hoenn::ZoneName(zone).es;
}

const char *ZoneName(uint16_t zone) {
    if (zone >= hoenn::NUM_ZONES) return "";
    const hoenn::LocName &L = hoenn::ZoneName(zone);
    const char *g = GameText::Location(L.location_id);
    if (g) return g;
    return Lang::Shown() == Lang::ES ? L.es : L.en;
}

const char *ZoneNameOsd(uint16_t zone) {
    if (zone >= hoenn::NUM_ZONES) return "";
    const hoenn::LocName &L = hoenn::ZoneName(zone);
    if (Lang::Cjk()) { const char *e = GameText::LocationEn(L.location_id); return e ? e : L.en; }
    return ZoneName(zone);
}

bool SameSpace(uint16_t a, uint16_t b) {
    if (a == b) return true;
    if (a >= hoenn::NUM_ZONES || b >= hoenn::NUM_ZONES) return false;
    const hoenn::ZoneInfo &za = hoenn::ZONES[a], &zb = hoenn::ZONES[b];
    return (za.flags & hoenn::F_OVERWORLD) && (zb.flags & hoenn::F_OVERWORLD) && za.matrix == zb.matrix;
}

void DrawPin(const R3D::Target &t, int cx, int cy, int r, uint8_t R, uint8_t G, uint8_t B) {
    for (int dy = -r - 1; dy <= r + 1; dy++)
        for (int dx = -r - 1; dx <= r + 1; dx++) {
            int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= t.w || y >= t.h) continue;
            int d2 = dx * dx + dy * dy;
            if (d2 <= r * r) {
                bool hl = (dx == -1 && dy == -1);
                R3D::PutPixel(t, x, y, hl ? 255 : R, hl ? 255 : G, hl ? 255 : B);
            } else if (d2 <= (r + 1) * (r + 1)) {
                R3D::PutPixel(t, x, y, 16, 16, 24);
            }
        }
}

} // namespace TownMap
