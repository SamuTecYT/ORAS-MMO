/*
 * RomFS.hpp — ORAS MMO v13
 * Lector mínimo del RomFS del PROPIO juego en ejecución (ARCHIVE_ROMFS del proceso).
 * Así los modelos 3D se cargan de la copia del juego de cada jugador: el plugin no
 * incluye ni distribuye ningún archivo de Nintendo.
 */
#pragma once
#include <3ds.h>
#include "HeroAssets.hpp"

class GameRomFS : public Hero::ByteSource {
public:
    ~GameRomFS() { Close(); }
    bool Open();                                         // false si el juego no expone RomFS
    void Close();
    bool Find(const char *path, uint64_t &offset, uint64_t &size) override; // ruta tipo "/a/0/2/1"
    bool Read(uint64_t offset, void *dst, uint32_t size) override;          // offset absoluto dentro del RomFS
private:
    Handle m_file = 0;
    u64    m_lvl3 = 0;        // inicio del nivel 3 (sistema de archivos)
    u32    m_dirOff = 0, m_dirLen = 0, m_fileOff = 0, m_fileLen = 0;
    u64    m_dataOff = 0;
    u8    *m_dirMeta = nullptr;
    u8    *m_fileMeta = nullptr;
};
