"""Builds mmo_pokes_3d.bin (the ORAS MMO 3D followers) from YOUR OWN copy of Pokémon Omega Ruby / Alpha Sapphire.
Genera mmo_pokes_3d.bin (los acompañantes 3D del ORAS MMO) a partir de TU PROPIA copia de Rubí Omega / Zafiro Alfa.

    python make_3d_pack.py --rom <game.cxi | game.3ds | romfs_folder> [-o mmo_pokes_3d.bin]
                           [--workers 4] [--only 1,25,373] [--sheet preview.png]

Los 721 Pokémon se dibujan con la tarjeta gráfica a partir de sus MODELOS 3D del juego, en su pose de reposo
(Poké Recreo, o combate si en esa van por el aire), con 5 direcciones y 9 cuadros por dirección (0 = quieto,
1..8 = paso animado con el esqueleto de cada Pokémon, walk.py v3; sin patas: su bucle de reposo). Cada (especie,
dirección) es un bloque comprimido con LZMA. El plugin lo descomprime con 3ds_plugin/Sources/Lzma.cpp.

Formato (little-endian):
  "MMO3D3\\0\\0" | u16 count=722 | u16 refPx | u16 nDirs=5 | u16 nFrames=9
  u32 off[722*5] | u32 len[722*5]         (bloque de la especie s y dirección d en s*5+d; 0 = falta)
  bloque: u32 tamaño descomprimido | u8 propiedades ((pb*5+lp)*9+lc) | flujo LZMA1 crudo (lc=3 lp=0 pb=0); descomprimido:
    u16 nPal | u16 flags | u16 pal[nPal] (RGB565; índice 0 = transparente, i = pal[i-1])
      flags: bit0 anda con patas, bit1 cuadrúpedo, byte alto = zancada (unidades del mundo por ciclo) con patas,
      o duración de su bucle en pasos de 20 ms sin patas
    nFrames x { u8 w, u8 h, s16 ax, s16 ay, u16 0, u32 off, u32 len }   (off desde el inicio del bloque)
    filas RLE de cada cuadro: por fila u8 nTramos y por tramo u8 x, u8 n, n x u8 índice

The .bin contains game graphics: keep it for yourself, never upload or share it.
El .bin contiene gráficos del juego: es solo para ti, no lo subas ni lo compartas.
"""
import argparse, os, sys, struct, time
import lzma
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pkmodel  # noqa: E402

MAXDIM = 128
NDIRS = 5
LC, LP, PB = 3, 0, 0                  # el plugin admite lc + lp <= 3 (modelo fijo de 16 KB)
_FILT = [{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME, "dict_size": 1 << 17, "lc": LC, "lp": LP, "pb": PB}]


def pack_block(raw):
    """bloque descomprimido -> bloque del paquete MMO3D3"""
    return struct.pack("<IB", len(raw), (PB * 5 + LP) * 9 + LC) + lzma.compress(raw, format=lzma.FORMAT_RAW, filters=_FILT)


def unpack_block(blk):
    n, props = struct.unpack_from("<IB", blk, 0)
    lc, lp, pb = props % 9, (props // 9) % 5, props // 45
    f = [{"id": lzma.FILTER_LZMA1, "dict_size": 1 << 17, "lc": lc, "lp": lp, "pb": pb}]
    out = lzma.LZMADecompressor(format=lzma.FORMAT_RAW, filters=f).decompress(blk[5:], max_length=n)
    assert len(out) == n
    return out


def encode_dir(frames, flags):
    """frames: [(rgba float, (ax, ay))] de UNA dirección -> bytes del bloque (sin comprimir)"""
    masks, rgbs = [], []
    for v, a in frames:
        m = v[..., 3] >= 0.5
        q = np.clip(v[..., :3], 0, 1)
        r5 = np.round(q[..., 0] * 31); g6 = np.round(q[..., 1] * 63); b5 = np.round(q[..., 2] * 31)
        masks.append(m)
        rgbs.append(np.stack([r5 * 255 / 31, g6 * 255 / 63, b5 * 255 / 31], -1).astype(np.uint8))
    allpx = np.concatenate([rgb[m] for rgb, m in zip(rgbs, masks)]) if any(m.any() for m in masks) else np.zeros((1, 3), np.uint8)
    n = len(allpx)
    side = int(np.ceil(np.sqrt(n)))
    pad = np.zeros((side * side, 3), np.uint8); pad[:n] = allpx; pad[n:] = allpx[0]
    uniq = np.unique(allpx.view([("", np.uint8)] * 3))
    ncol = max(1, min(255, len(uniq)))
    qi = Image.fromarray(pad.reshape(side, side, 3), "RGB").quantize(colors=ncol, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array(qi.getpalette()[:3 * ncol], np.int32).reshape(-1, 3)
    pal565 = [((int(r) >> 3) << 11) | ((int(g) >> 2) << 5) | (int(b) >> 3) for r, g, b in pal]
    tab, datas = [], []
    for (v, (ax, ay)), rgb, m in zip(frames, rgbs, masks):
        h, w = m.shape
        assert w <= 255 and h <= 255, (w, h)
        idx = np.zeros((h, w), np.uint8)
        if m.any():
            px = rgb[m].astype(np.int32)
            dd = ((px[:, None, :] - pal[None, :, :]) ** 2).sum(-1)
            idx[m] = (dd.argmin(1) + 1).astype(np.uint8)
        rows = bytearray()
        for y in range(h):
            runs = []; x = 0
            while x < w:
                if not idx[y, x]: x += 1; continue
                x0 = x
                while x < w and idx[y, x]: x += 1
                runs.append((x0, idx[y, x0:x].tobytes()))
            rows.append(len(runs))
            for x0, b in runs: rows += bytes([x0, len(b)]) + b
        tab.append((w, h, int(round(ax)), int(round(ay))))
        datas.append(bytes(rows))
    head = struct.pack("<HH", len(pal565), flags) + struct.pack("<%dH" % len(pal565), *pal565)
    off = len(head) + 16 * len(frames)
    t = b""
    for (w, h, ax, ay), dd in zip(tab, datas):
        t += struct.pack("<BBhhHII", w, h, ax, ay, 0, off, len(dd)); off += len(dd)
    return head + t + b"".join(datas)


def decode_block(blob, f):
    npal, flags = struct.unpack_from("<HH", blob, 0)
    pal = struct.unpack_from("<%dH" % npal, blob, 4)
    w, h, ax, ay, _, off, ln = struct.unpack_from("<BBhhHII", blob, 4 + 2 * npal + 16 * f)
    rgb = np.zeros((h, w, 3), np.uint8); mask = np.zeros((h, w), bool)
    p = off
    for y in range(h):
        nr = blob[p]; p += 1
        for _ in range(nr):
            x0, n = blob[p], blob[p + 1]; p += 2
            for k in range(n):
                c = pal[blob[p + k] - 1]
                rgb[y, x0 + k] = ((c >> 11) * 255 // 31, ((c >> 5) & 63) * 255 // 63, (c & 31) * 255 // 31)
                mask[y, x0 + k] = True
            p += n
    return rgb, mask, (ax, ay), flags


def bake_one(args):
    sp, = args
    global _R, _SF
    try:
        _R
    except NameError:
        from pkrender import Renderer
        from pkmodel import species_forms
        _R = Renderer(); _SF = species_forms()
    import bake
    B = bake.bake_species_anim(_R, sp, _SF)
    big = max(max(v.shape[0], v.shape[1]) for dv in B["views"] for v, a in dv)
    if big > MAXDIM - 2:
        B = bake.bake_species_anim(_R, sp, _SF, scale_mul=(MAXDIM - 4) / big)
    # banderas: bit0 anda con patas, bit1 cuadrúpedo; byte alto = con patas: unidades del mundo por ciclo (zancada),
    # sin patas: duración de su bucle en pasos de 20 ms
    hi = int(round(B["stride"])) if B["legs"] else int(round(B["cycle_ms"] / 20.0))
    flags = (1 if B["legs"] else 0) | (2 if B["quad"] else 0) | (max(0, min(255, hi)) << 8)
    blocks = []
    for dv in B["views"]:
        raw = encode_dir(dv, flags)
        blocks.append((pack_block(raw), len(raw)))
    return sp, blocks, B["legs"], (B["name"], B.get("elev", bake.ELEV))


def write_pack(out, blocks_by_sp, ref_px, nframes):
    """blocks_by_sp: {especie: [bloque comprimido de cada dirección (b"" = falta)]} -> escribe el paquete MMO3D3"""
    N = 722 * NDIRS
    offs = [0] * N; lens = [0] * N
    base = 16 + 8 * N
    body = bytearray()
    for sp in sorted(blocks_by_sp):
        for d, c in enumerate(blocks_by_sp[sp]):
            if not c: continue
            offs[sp * NDIRS + d] = base + len(body); lens[sp * NDIRS + d] = len(c)
            body += c
            while len(body) % 4: body.append(0)
    hdr = b"MMO3D3\0\0" + struct.pack("<HHHH", 722, ref_px, NDIRS, nframes)
    data = hdr + struct.pack("<%dI" % N, *offs) + struct.pack("<%dI" % N, *lens) + bytes(body)
    open(out, "wb").write(data)
    return data


def _init_worker(rom):
    pkmodel.set_rom(rom)


def check_inputs(rom):
    """Falla pronto y con un mensaje claro / fail early with a clear message."""
    from romfs import RomFS, RomError, ORAS_TITLE_IDS
    try:
        r = RomFS(rom)
    except RomError as e:
        sys.exit("ERROR: %s" % e)
    if "/a/0/0/8" not in r.files:
        sys.exit("ERROR: a/0/0/8 not found in %s: is this Pokemon Omega Ruby / Alpha Sapphire?" % rom)
    if r.title is not None and r.title not in ORAS_TITLE_IDS:
        print("WARNING: title %016X is not Omega Ruby / Alpha Sapphire" % r.title)
    if r.f: r.f.close()
    try:
        import moderngl
        moderngl.create_standalone_context().release()
    except Exception as e:
        sys.exit("ERROR: could not start OpenGL 3.3 on this PC (%s). Update your graphics driver." % e)


def main():
    ap = argparse.ArgumentParser(description="Build mmo_pokes_3d.bin from your own copy of ORAS (see README.md).")
    ap.add_argument("--rom", required=True, help="decrypted .cxi / .3ds, or a folder with the extracted RomFS")
    ap.add_argument("-o", "--out", default="mmo_pokes_3d.bin", help="output file (default: mmo_pokes_3d.bin)")
    ap.add_argument("--workers", type=int, default=4, help="parallel processes (default: 4)")
    ap.add_argument("--only", help="only these National Dex numbers, e.g. 1,25,373 (default: all 721)")
    ap.add_argument("--sheet", metavar="PNG", help="also save a preview sheet")
    args = ap.parse_args()
    only = [int(x) for x in args.only.split(",")] if args.only else list(range(1, 722))
    if not all(1 <= x <= 721 for x in only):
        ap.error("--only: numbers from 1 to 721")
    check_inputs(args.rom)
    pkmodel.set_rom(args.rom)
    out = args.out
    t0 = time.time(); res = {}
    if args.workers > 1:
        from multiprocessing import Pool
        with Pool(args.workers, initializer=_init_worker, initargs=(args.rom,)) as pool:
            for sp, blocks, legs, name in pool.imap_unordered(bake_one, [(s,) for s in only]):
                res[sp] = (blocks, legs, name)
                if len(res) % 50 == 0: print("%d/%d done, %d s" % (len(res), len(only), time.time() - t0), flush=True)
    else:
        for s in only:
            sp, blocks, legs, name = bake_one((s,)); res[sp] = (blocks, legs, name)
    import bake
    rawmax = max(rawlen for v in res.values() for c, rawlen in v[0])
    data = write_pack(out, {sp: [c for c, rawlen in v[0]] for sp, v in res.items()}, bake.REF_PX, bake.NFRAMES)
    nlegs = sum(1 for v in res.values() if v[1])
    low = sorted((sp, v[2][1]) for sp, v in res.items() if v[2][1] != bake.ELEV)
    print("lower camera (eyes hidden from above):", len(low), low)
    print("ok", out, len(data), "bytes;", len(res), "species (", nlegs, "with legs); largest block", rawmax,
          "bytes;", round(time.time() - t0), "s")
    if args.sheet:
        sheet(res, args.sheet)


def sheet(res, path, d=1, cell=72, cols=4):
    """Hoja de control: por especie, los 5 cuadros de la dirección d (decodificados del .bin)."""
    from PIL import ImageDraw
    import bake
    NFRAMES = bake.NFRAMES
    sps = sorted(res)
    rows = (len(sps) + cols - 1) // cols
    W = cols * (cell * NFRAMES + 8)
    im = Image.new("RGB", (W, rows * (cell + 10)), (110, 140, 100))
    dr = ImageDraw.Draw(im)
    for i, sp in enumerate(sps):
        blob = unpack_block(res[sp][0][d][0])
        x = (i % cols) * (cell * NFRAMES + 8); y = (i // cols) * (cell + 10)
        for f in range(NFRAMES):
            rgb, m, a, fl = decode_block(blob, f)
            t = Image.fromarray(rgb); al = Image.fromarray((m * 255).astype(np.uint8))
            s = min(1.0, (cell - 2) / max(t.size))
            if s < 1: t = t.resize((max(1, int(t.size[0] * s)), max(1, int(t.size[1] * s)))); al = al.resize(t.size)
            im.paste(t, (x + f * cell + (cell - t.size[0]) // 2, y + cell - t.size[1]), al)
        dr.text((x + 1, y + cell), "%d%s" % (sp, " p" if res[sp][1] else ""), fill=(0, 0, 0))
    im.save(path); print("hoja", path)


if __name__ == "__main__":
    main()
