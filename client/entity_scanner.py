import struct
import psutil

OFFSET_PLAYER_X    = 0x08C6E894
OFFSET_PLAYER_Y    = 0x08C6E89C
OFFSET_MAP_ID      = 0x08C6E884

print("=============================")
print("Iniciando Escaner de Memoria...")

try:
    import pymem
    import pymem.memory
    import re
    
    proc_name = None
    for proc in psutil.process_iter(['name']):
        if proc.info['name'] and 'azahar' in proc.info['name'].lower():
            proc_name = proc.info['name']
            break
            
    if not proc_name:
        print("[-] No se encontro el emulador Azahar abierto.")
        input()
        exit()
        
    pm = pymem.Pymem(proc_name)
    fcram_base = 0
    address = 0
    print("[*] Buscando memoria del juego (FCRAM)...")
    while address < 0x7FFFFFFFFFFF:
        try:
            mbi = pymem.memory.virtual_query(pm.process_handle, address)
            if mbi.RegionSize in [0x8000000, 0x10000000]:
                fcram_base = mbi.BaseAddress
                break
            address += mbi.RegionSize
        except Exception:
            break

    if not fcram_base:
        print("[-] No se pudo encontrar la memoria FCRAM. Asegurate de estar dentro del juego.")
        input()
        exit()
        
    print(f"[+] FCRAM Encontrada en: 0x{fcram_base:X}")
    
    # Calcular direccion en Windows
    win_x = fcram_base + (OFFSET_PLAYER_X - 0x08000000)
    win_y = fcram_base + (OFFSET_PLAYER_Y - 0x08000000)
    
    px = pm.read_float(win_x)
    py = pm.read_float(win_y)
    
    print(f"[+] Coordenadas Exactas Leidas: X={px:.4f}, Y={py:.4f}")
    
    if px == 0.0 and py == 0.0:
        print("[-] Tus coordenadas son 0.0. ¿Seguro que estas jugando o estas en el menu?")
        input()
        exit()
        
    print(f"[*] Escaneando los 128MB de RAM buscando Entidades 3D...")
    
    target_bytes = struct.pack('<f', px)
    pattern = b"".join([re.escape(bytes([b])) for b in target_bytes])
    
    addresses = pymem.pattern.pattern_scan_all(pm.process_handle, pattern, return_multiple=True)
    
    print(f"[+] Se encontraron {len(addresses)} posibles coincidencias en memoria.")
    for addr in addresses:
        try:
            v3 = pm.read_bytes(addr, 12)
            x, y, z = struct.unpack('<fff', v3)
            print(f"--> POTENCIAL ENTIDAD en: 0x{addr:016X} | X={x:.2f}, Y={y:.2f}, Z={z:.2f}")
        except:
            pass
            
except Exception as e:
    print(f"[-] Error de Memoria: {e}")

print("=============================")
print("Escaneo finalizado.")
