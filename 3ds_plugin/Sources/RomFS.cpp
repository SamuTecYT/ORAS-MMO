#include "RomFS.hpp"
#include <cstdlib>
#include <cstring>

// RomFS del proceso actual (el juego): ARCHIVE_ROMFS con ruta binaria de 12 bytes a cero.
bool GameRomFS::Open() {
    Close();
    static const u32 zeros[3] = { 0, 0, 0 };
    FS_Path arch = { PATH_EMPTY, 1, "" };
    FS_Path file = { PATH_BINARY, sizeof(zeros), zeros };
    if (R_FAILED(FSUSER_OpenFileDirectly(&m_file, ARCHIVE_ROMFS, arch, file, FS_OPEN_READ, 0))) {
        m_file = 0;
        return false;
    }
    // Localizar el nivel 3 (sistema de archivos). Si el archivo empieza por la cabecera IVFC, se calcula;
    // si no, el nivel 3 suele estar en 0x1000 (como hace libctru).
    u8 ivfc[0x60];
    m_lvl3 = 0x1000;
    if (Read(0, ivfc, sizeof(ivfc)) && !memcmp(ivfc, "IVFC", 4)) {
        u32 masterSize, lvl3Log2;
        memcpy(&masterSize, ivfc + 0x08, 4);
        memcpy(&lvl3Log2, ivfc + 0x4C, 4);
        if (lvl3Log2 >= 9 && lvl3Log2 <= 16) {
            u64 bs = 1ull << lvl3Log2;
            m_lvl3 = ((0x60 + masterSize + bs - 1) / bs) * bs;
        }
    }
    u32 hdr[10];
    if (!Read(m_lvl3, hdr, sizeof(hdr)) || hdr[0] != 0x28) {
        // último intento: nivel 3 al inicio del archivo
        m_lvl3 = 0;
        if (!Read(0, hdr, sizeof(hdr)) || hdr[0] != 0x28) { Close(); return false; }
    }
    m_dirOff = hdr[3]; m_dirLen = hdr[4];
    m_fileOff = hdr[7]; m_fileLen = hdr[8];
    m_dataOff = hdr[9];
    if (m_dirLen < 24 || m_dirLen > 0x100000 || m_fileLen == 0 || m_fileLen > 0x100000) { Close(); return false; }
    m_dirMeta = (u8 *)malloc(m_dirLen);
    m_fileMeta = (u8 *)malloc(m_fileLen);
    if (!m_dirMeta || !m_fileMeta ||
        !Read(m_lvl3 + m_dirOff, m_dirMeta, m_dirLen) || !Read(m_lvl3 + m_fileOff, m_fileMeta, m_fileLen)) {
        Close();
        return false;
    }
    return true;
}

void GameRomFS::Close() {
    if (m_file) FSFILE_Close(m_file);
    m_file = 0;
    free(m_dirMeta); m_dirMeta = nullptr;
    free(m_fileMeta); m_fileMeta = nullptr;
}

bool GameRomFS::Read(uint64_t offset, void *dst, uint32_t size) {
    if (!m_file) return false;
    u32 done = 0;
    while (done < size) {
        u32 got = 0;
        if (R_FAILED(FSFILE_Read(m_file, &got, offset + done, (u8 *)dst + done, size - done)) || got == 0) return false;
        done += got;
    }
    return true;
}

// Compara un nombre UTF-16LE de la tabla con un segmento ASCII
static bool NameEq(const u8 *utf16, u32 lenBytes, const char *ascii, u32 n) {
    if (lenBytes != n * 2) return false;
    for (u32 i = 0; i < n; i++)
        if (utf16[2 * i] != (u8)ascii[i] || utf16[2 * i + 1] != 0) return false;
    return true;
}

bool GameRomFS::Find(const char *path, uint64_t &offset, uint64_t &size) {
    if (!m_dirMeta || !m_fileMeta || path[0] != '/') return false;
    u32 dir = 0;                      // raíz
    const char *p = path + 1;
    while (true) {
        const char *slash = strchr(p, '/');
        u32 n = slash ? (u32)(slash - p) : (u32)strlen(p);
        if (n == 0) return false;
        if (slash) {
            // buscar subdirectorio
            if (dir + 24 > m_dirLen) return false;
            u32 child;
            memcpy(&child, m_dirMeta + dir + 8, 4);
            bool found = false;
            for (int guard = 0; child != 0xFFFFFFFF && guard < 4096; guard++) {
                if ((u64)child + 24 > m_dirLen) return false;
                u32 sib, nameLen;
                memcpy(&sib, m_dirMeta + child + 4, 4);
                memcpy(&nameLen, m_dirMeta + child + 20, 4);
                if ((u64)child + 24 + nameLen > m_dirLen) return false;
                if (NameEq(m_dirMeta + child + 24, nameLen, p, n)) { dir = child; found = true; break; }
                child = sib;
            }
            if (!found) return false;
            p = slash + 1;
        } else {
            // buscar archivo en 'dir'
            u32 f;
            if ((u64)dir + 24 > m_dirLen) return false;
            memcpy(&f, m_dirMeta + dir + 12, 4);
            for (int guard = 0; f != 0xFFFFFFFF && guard < 65536; guard++) {
                if ((u64)f + 32 > m_fileLen) return false;
                u32 sib, nameLen;
                u64 dOff, dSize;
                memcpy(&sib, m_fileMeta + f + 4, 4);
                memcpy(&dOff, m_fileMeta + f + 8, 8);
                memcpy(&dSize, m_fileMeta + f + 16, 8);
                memcpy(&nameLen, m_fileMeta + f + 28, 4);
                if ((u64)f + 32 + nameLen > m_fileLen) return false;
                if (NameEq(m_fileMeta + f + 32, nameLen, p, n)) {
                    offset = m_lvl3 + m_dataOff + dOff;
                    size = dSize;
                    return true;
                }
                f = sib;
            }
            return false;
        }
    }
}
