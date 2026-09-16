--[[
  Project ORAS MMO — Cheat Engine Scanner v3 (ROBUST)
  =====================================================
  v3 fixes:
  - All scan sections wrapped in pcall() — one crash won't stop the rest
  - X/Y scan uses Unknown Initial Value first (no range = no memory explosion)
  - Result count safety guard before printing (skip if > 2 million)
  - Aligned scan mode (faster, less memory)
  - Reduced address ceiling to 0x3FFFFFFF (covers Azahar emulated RAM)
  - Map ID scan uses DWORD (same as money — proven to work)

  Author: Project ORAS MMO — Lead Architect v3
--]]

local function log(msg)
  print("[ORAS] " .. tostring(msg))
end

local ADDR_MAX = 0x3FFFFFFF   -- Upper limit of Azahar emulated RAM region
local MAX_DISPLAY = 15        -- Max results to print per scan

local function safe_print_dword(ms, label)
  local count = ms:getCount()
  log(label .. ": " .. count .. " resultados")
  if count == 0 or count > 500000 then
    log("  (demasiados o ninguno — continúa al siguiente paso)")
    return
  end
  local fl = ms:getResults()
  local n = math.min(count, MAX_DISPLAY)
  for i = 0, n - 1 do
    local ok, addr = pcall(function() return fl:getAddress(i) end)
    if ok and addr then
      local val = readInteger(addr) or 0
      log(string.format("  %s[%02d] addr=0x%08X  val=%d", label, i, addr, val))
    end
  end
end

local function safe_print_float(ms, label)
  local count = ms:getCount()
  log(label .. ": " .. count .. " resultados")
  if count == 0 or count > 500000 then
    log("  (demasiados o ninguno — continúa al siguiente paso)")
    return
  end
  local fl = ms:getResults()
  local n = math.min(count, MAX_DISPLAY)
  for i = 0, n - 1 do
    local ok, addr = pcall(function() return fl:getAddress(i) end)
    if ok and addr then
      local val = readFloat(addr) or 0
      log(string.format("  %s[%02d] addr=0x%08X  val=%.4f", label, i, addr, val))
    end
  end
end

log("=========================================")
log("  Project ORAS MMO — Scanner v3 ROBUST")
log("=========================================")
log("")

-- ============================================================
-- SCAN A: Player Money (DWORD exact — proven to work)
-- ============================================================
local ok_a = pcall(function()
  log("=== SCAN A: Dinero ===")

  local money_str = inputQuery(
    "Scanner — Dinero",
    "¿Cuántos Pokédólares tienes ahora?\n(Solo el número, sin puntos ni comas)",
    "3000"
  )
  local money_val = tonumber(money_str)
  if not money_val then
    log("Número no válido — saltando Scan A.")
    return
  end

  log("Buscando " .. money_val .. " ...")
  local ms = createMemScan()
  ms:firstScan(0, vtDword, 0,
    tostring(money_val), tostring(money_val),
    0, ADDR_MAX, "?*?W?", fsmAligned, "")
  log("Primer scan dinero: " .. ms:getCount() .. " resultados.")

  showMessage(
    "Ve al juego y GASTA exactamente 100 Pokédólares.\n" ..
    "(Entra a un Pokémart — la tienda azul — y compra cualquier cosa de 100.)\n\n" ..
    "Cuando hayas gastado, haz clic en OK."
  )

  ms:nextScan(0, vtDword, 0,
    tostring(money_val - 100), tostring(money_val - 100))
  safe_print_dword(ms, "MONEY")
end)
if not ok_a then log("Scan A falló — continuando...") end

log("")

-- ============================================================
-- SCAN B+C: Player X and Y (Float — Unknown Initial Value)
-- ============================================================
local ok_bc = pcall(function()
  log("=== SCAN B+C: Coordenadas X e Y ===")
  log("Usando Unknown Initial Value para no saturar la memoria.")

  showMessage(
    "Pon a tu personaje COMPLETAMENTE QUIETO.\n" ..
    "No presiones ningún botón.\n\n" ..
    "Haz clic en OK cuando esté quieto."
  )

  -- Unknown Initial Value scan — no range needed, safest approach
  log("Iniciando scan X (Unknown Initial Value, float)...")
  local ms_x = createMemScan()
  ms_x:firstScan(4, vtSingle, 0, "", "", 0, ADDR_MAX, "?*?W?", fsmAligned, "")
  log("  X inicial: " .. ms_x:getCount() .. " valores.")

  log("Iniciando scan Y (Unknown Initial Value, float)...")
  local ms_y = createMemScan()
  ms_y:firstScan(4, vtSingle, 0, "", "", 0, ADDR_MAX, "?*?W?", fsmAligned, "")
  log("  Y inicial: " .. ms_y:getCount() .. " valores.")

  -- Walk NORTH: Y changes, X stays same
  showMessage(
    "CAMINA HACIA ARRIBA (Norte) por 5 segundos.\n" ..
    "Mantén presionada la tecla de arriba en el teclado o el joystick.\n" ..
    "Luego SUÉLTALA y quédate completamente quieto.\n\n" ..
    "Haz clic en OK cuando estés quieto."
  )
  log("Rescan Norte: X=sin cambio, Y=cambió...")
  ms_x:nextScan(10, vtSingle, 0, "", "")  -- unchanged
  ms_y:nextScan(9,  vtSingle, 0, "", "")  -- changed
  log("  Después Norte — X:" .. ms_x:getCount() .. "  Y:" .. ms_y:getCount())

  -- Walk EAST: X changes, Y stays same
  showMessage(
    "Ahora CAMINA HACIA LA DERECHA (Este) por 5 segundos.\n" ..
    "Mantén presionada la tecla DERECHA.\n" ..
    "Luego SUÉLTALA y quédate quieto.\n\n" ..
    "Haz clic en OK cuando estés quieto."
  )
  log("Rescan Este: X=cambió, Y=sin cambio...")
  ms_x:nextScan(9,  vtSingle, 0, "", "")  -- changed
  ms_y:nextScan(10, vtSingle, 0, "", "")  -- unchanged
  log("  Después Este — X:" .. ms_x:getCount() .. "  Y:" .. ms_y:getCount())

  -- One more north pass to further isolate
  showMessage(
    "Camina HACIA ARRIBA (Norte) de nuevo por 5 segundos.\n" ..
    "Luego para y quédate quieto.\n\n" ..
    "Haz clic en OK."
  )
  ms_x:nextScan(10, vtSingle, 0, "", "")
  ms_y:nextScan(9,  vtSingle, 0, "", "")
  log("  Segunda Norte — X:" .. ms_x:getCount() .. "  Y:" .. ms_y:getCount())

  safe_print_float(ms_x, "POS_X")
  safe_print_float(ms_y, "POS_Y")
end)
if not ok_bc then log("Scan B+C falló — continuando...") end

log("")

-- ============================================================
-- SCAN D: Map ID (DWORD changed/unchanged)
-- ============================================================
local ok_d = pcall(function()
  log("=== SCAN D: ID del Mapa ===")

  showMessage(
    "Escaneo de ID de Mapa.\n\n" ..
    "Quédate donde estás en el juego, no entres a ningún lado.\n\n" ..
    "Haz clic en OK para iniciar."
  )

  log("Scan inicial de mapa (Unknown Initial Value, DWORD)...")
  local ms = createMemScan()
  ms:firstScan(4, vtDword, 0, "", "", 0, ADDR_MAX, "?*?W?", fsmAligned, "")
  log("  Inicial: " .. ms:getCount() .. " valores.")

  showMessage(
    "ENTRA POR UNA PUERTA a otro lugar.\n\n" ..
    "Por ejemplo: entra al Pokémon Center (la casa con la cruz roja).\n\n" ..
    "Espera que cargue el nuevo lugar y haz clic en OK."
  )
  ms:nextScan(9, vtDword, 0, "", "")
  log("  Tras entrar: " .. ms:getCount())

  showMessage(
    "Sal de ese lugar y vuelve adonde estabas antes.\n\n" ..
    "Cuando estés de vuelta en el lugar original, haz clic en OK."
  )
  ms:nextScan(9, vtDword, 0, "", "")
  log("  Tras volver: " .. ms:getCount())

  showMessage(
    "No entres a ningún lugar más.\n" ..
    "Quédate quieto donde estás.\n\n" ..
    "Haz clic en OK para el último escaneo."
  )
  ms:nextScan(10, vtDword, 0, "", "")
  log("  Final estable: " .. ms:getCount())

  safe_print_dword(ms, "MAP_ID")
end)
if not ok_d then log("Scan D falló — continuando...") end

-- ============================================================
-- DONE
-- ============================================================
log("")
log("=========================================")
log("  ESCANEO COMPLETO")
log("  Copia TODO este texto y envialo")
log("  al Lead Architect en el chat.")
log("=========================================")

showMessage(
  "ESCANEO TERMINADO.\n\n" ..
  "Pasos finales:\n" ..
  "1. Haz clic en el cuadro de texto 'Output' de arriba\n" ..
  "2. Presiona Ctrl+A  (selecciona todo)\n" ..
  "3. Presiona Ctrl+C  (copia)\n" ..
  "4. Pega en el chat con el Lead Architect\n\n" ..
  "¡Gracias!"
)
