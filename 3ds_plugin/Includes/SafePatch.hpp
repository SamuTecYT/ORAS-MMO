#ifndef SAFE_PATCH_HPP
#define SAFE_PATCH_HPP

// Parche de CÓDIGO del juego que de verdad surte efecto en Azahar.
//
// Process::Patch de CTRPF solo hace memcpy + svcFlush/InvalidateProcessDataCache, y en
// Azahar 2126.2 esas llamadas no hacen nada: el JIT seguiría ejecutando la versión vieja
// ya traducida. Tras escribir hay que invalidar ese rango con svc 0x93 (KB §12.2).
// Solo para código de code.bin: el código de los .cro se recarga y pierde el parche.
// Para escribir DATOS (heap, GameData) no hace falta nada de esto.
//
// Uso: llamarlo una vez (por ejemplo al arrancar), nunca en cada frame.

#include <3ds/types.h>
#include <CTRPluginFramework.hpp>

extern "C" Result svcInvalidateInstructionCacheRange(u32 addr, u32 size); // svc 0x93 (libctrpf csvc.o)

namespace SafePatch {

// Escribe `size` bytes de `data` en el código del juego y refresca el JIT de ese rango.
// Devuelve false si la dirección no es válida o la escritura falla.
inline bool Code(u32 addr, const void *data, u32 size, void *original = nullptr) {
    if (!CTRPluginFramework::Process::CheckAddress(addr, MEMPERM_READ) ||
        !CTRPluginFramework::Process::CheckAddress(addr + size - 1, MEMPERM_READ))
        return false;
    if (!CTRPluginFramework::Process::Patch(addr, const_cast<void *>(data), size, original))
        return false;
    svcInvalidateInstructionCacheRange(addr, size);
    return true;
}

inline bool Code32(u32 addr, u32 value, u32 *original = nullptr) {
    return Code(addr, &value, sizeof(value), original);
}

} // namespace SafePatch

#endif
