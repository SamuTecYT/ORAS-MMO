// Emotes.cpp -- GENERADO por gen_emotes.py (pixel art 12x12 propio). No editar a mano.
#include "Emotes.hpp"

namespace Emotes {

const char *const kNames[COUNT] = { "Feliz", "Corazón", "Risa", "Calavera", "Pregunta", "Sorpresa", "Enojado", "Llorando", "Fuego", "Sueño", "GG", "Sus", "F", "Pokéball" };

// Paleta: .=None k=(24, 24, 32) w=(255, 255, 255) y=(250, 200, 40) o=(240, 140, 30) r=(220, 40, 60) b=(60, 120, 230) c=(120, 210, 250) G=(40, 170, 80)
static const unsigned char kPal[8][3] = { {24, 24, 32}, {255, 255, 255}, {250, 200, 40}, {240, 140, 30}, {220, 40, 60}, {60, 120, 230}, {120, 210, 250}, {40, 170, 80} };
static const char kCodes[] = "kwyorbcG";

static const char *const kArt[COUNT][12] = {
    { "...kkkkkk...", "..kyyyyyyk..", ".kyyyyyyyyk.", "kyyykyykyyyk", "kyyykyykyyyk", "kyyyyyyyyyyk", "kykyyyyyykyk", "kyykyyyykyyk", ".kyykkkkyyk.", "..kyyyyyyk..", "...kkkkkk...", "............" },
    { "............", ".kkk....kkk.", "krrrk..krrrk", "krwrrkkrrrrk", "krrrrrrrrrrk", "krrrrrrrrrrk", ".krrrrrrrrk.", "..krrrrrrk..", "...krrrrk...", "....krrk....", ".....kk.....", "............" },
    { "...kkkkkk...", "..kyyyyyyk..", ".kyyyyyyyyk.", "kyykyyyykyyk", "kyyykyykyyyk", "kyykyyyykyyk", "kyyyyyyyyyyk", "kykkkkkkkkyk", "kykrrrrrrkyk", ".kykkkkkkyk.", "..kyyyyyyk..", "...kkkkkk..." },
    { "..kkkkkkkk..", ".kwwwwwwwwk.", "kwwwwwwwwwwk", "kwkkwwwwkkwk", "kwkkwwwwkkwk", "kwwwwkkwwwwk", ".kwwwkkwwwk.", "..kwwwwwwk..", "..kwkwkwkk..", "..kkwkwkwk..", "...kkkkkk...", "............" },
    { "...kkkkkk...", "..kbbbbbbk..", ".kbbkkkkbbk.", ".kkk...kbbk.", ".....kkbbk..", "....kbbbk...", "....kbbk....", "....kkkk....", "....kbbk....", "....kbbk....", "....kkkk....", "............" },
    { "....kkkk....", "....krrk....", "....krrk....", "....krrk....", "....krrk....", "....krrk....", "....krrk....", "....kkkk....", "....kkkk....", "....krrk....", "....krrk....", "....kkkk...." },
    { "...kkkkkk...", "..krrrrrrk..", ".krkrrrrkrk.", "krrrkrrkrrrk", "krrkkrrkkrrk", "krrrrrrrrrrk", "krrrrrrrrrrk", "krrrkkkkrrrk", "krrkrrrrkrrk", ".krrrrrrrrk.", "..krrrrrrk..", "...kkkkkk..." },
    { "...kkkkkk...", "..kyyyyyyk..", ".kyyyyyyyyk.", "kyykkyykkyyk", "kycyyyyyycyk", "kycyyyyyycyk", "kycyyyyyycyk", "kyyykkkkyyyk", "kyykyyyykyyk", ".kyyyyyyyyk.", "..kyyyyyyk..", "...kkkkkk..." },
    { ".....kk.....", "....koork...", "...koorrk...", "..kooorrrk..", "..koorryrk..", ".koorryyrrk.", ".korryyyyrk.", "korryyyyyyrk", "korryyyyyyrk", ".korryyyyrk.", "..kkorrrrkk.", "....kkkk...." },
    { "............", "bbbbbb......", "....bb......", "...bb.......", "..bb........", ".bb.........", "bbbbbb......", ".......bbbb.", ".........b..", "........b...", ".......bbbb.", "............" },
    { "............", "............", ".GGG...GGG..", "G...G.G...G.", "G.....G.....", "G.GGG.G.GGG.", "G...G.G...G.", "G...G.G...G.", ".GGG...GGG..", "............", "............", "............" },
    { "...kkkkk....", "..krrrrrk...", ".krrkkkkkk..", ".krkcccccck.", ".krkcwcccck.", ".krrkkkkkk..", "kkrrrrrrrk..", "krkrrrrrrk..", "krkrrrrrrk..", "kkkrrkrrrk..", "..krrkkrrk..", "..kkk.kkk..." },
    { "............", "..kkkkkkk...", "..kkkkkkk...", "..kk........", "..kk........", "..kkkkkk....", "..kkkkkk....", "..kk........", "..kk........", "..kk........", "..kk........", "............" },
    { "...kkkkkk...", "..krrrrrrk..", ".krrrrrrrrk.", "krwrrrrrrrrk", "krrrrkkrrrrk", "kkkkkwwkkkkk", "kwwwwkkwwwwk", "kwwwwwwwwwwk", ".kwwwwwwwwk.", "..kwwwwwwk..", "...kkkkkk...", "............" },
};

void Draw(int idx, int x, int y, int scale, PixelFn put, void *user) {
    if (idx < 0 || idx >= COUNT || !put) return;
    for (int r = 0; r < 12; r++)
        for (int c = 0; c < 12; c++) {
            char ch = kArt[idx][r][c];
            if (ch == '.') continue;
            int p = 0;
            while (kCodes[p] && kCodes[p] != ch) p++;
            if (!kCodes[p]) continue;
            put(user, x + c * scale, y + r * scale, scale, kPal[p][0], kPal[p][1], kPal[p][2]);
        }
}

} // namespace Emotes
