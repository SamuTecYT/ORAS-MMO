"""Hornea los sprites 3D de los acompañantes: cada Pokémon (forma base) en su pose de reposo de combate (cuadro 0 de la
ranura 0), visto desde la cámara del mapa (41° hacia abajo, medido en la matriz real de Lavacalda) en 5 direcciones
(0 = mirando a la cámara, 45, 90 = derecha, 135, 180 = de espaldas; la izquierda es el espejo).

Escala: el protagonista mide HERO_H = 33.5 unidades del mundo; se hornea a PPU píxeles por unidad (entrenador = REF_PX).
Tamaño del Pokémon (relativo al entrenador): d = clamp((altura_modelo / 140) ** 0.5, 0.5, 1.15) — los pequeños se
ven con detalle (v16.2 r6: antes 0.42), los gigantes no tapan media pantalla — y el largo/ancho nunca pasa de 1.6
entrenadores.
"""
import math
import numpy as np

from pkmodel import Form, species_forms
from pkrender import Renderer

HERO_H = 33.5
REF_PX = 72                       # alto del entrenador en los píxeles horneados
PPU = REF_PX / HERO_H
ELEV = 41.0
YAWS = (0, 45, 90, 135, 180)
TRAINER_CM = 140.0


def world_scale(F):
    """unidades del mundo por unidad del modelo (cm aprox.)"""
    Hm = max(float(F.hi[1] - max(F.lo[1], 0.0)), 1.0)
    d = min(max((Hm / TRAINER_CM) ** 0.5, 0.5), 1.15)
    s = d * HERO_H / Hm
    ext = max(float(F.hi[0] - F.lo[0]), float(F.hi[2] - F.lo[2]), 1.0)
    return min(s, 1.6 * HERO_H / ext), d


def bake_species(r, sp, sf=None, shiny=False, scale_mul=1.0):
    """-> dict(views=[(rgba crop float, (ax, ay) ancla = pies en el crop)], d=tamaño relativo, name)"""
    sf = sf or species_forms()
    F = Form(sf[sp][0], shiny=shiny)
    F.pose(0, 0.0)
    s, d = world_scale(F)
    s *= scale_mul; d *= scale_mul
    views = []
    for yaw in YAWS:
        img, (ox, oy) = r.render(F, yaw, ELEV, PPU, s, ss=4)
        a = img[..., 3]
        ys, xs = np.nonzero(a > 0.02)
        if not len(xs):
            views.append((np.zeros((1, 1, 4), np.float32), (0.0, 0.0))); continue
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        views.append((img[y0:y1, x0:x1].copy(), (ox - x0, oy - y0)))
    return dict(views=views, d=d, scale=s, name=F.name)


# ------------------------------------------------------------------ simulación del dibujo en el juego (igual que C++)
def runtime_draw(rgba, anchor, hpx, edge=0.55, mirror=False):
    """Lo que hará el plugin: alfa duro al 50 % (ya en el horneado), reducción por área al tamaño de pantalla
    (k = hpx / REF_PX), alfa duro al 50 % y borde 1 px oscurecido. Devuelve (rgb uint8, mask bool, ancla)."""
    a = (rgba[..., 3] >= 0.5).astype(np.float32)
    rgb = rgba[..., :3]
    if mirror:
        a = a[:, ::-1]; rgb = rgb[:, ::-1]; anchor = (rgba.shape[1] - anchor[0], anchor[1])
    k = hpx / REF_PX
    h, w = a.shape
    ow, oh = max(1, int(math.ceil(w * k))), max(1, int(math.ceil(h * k)))
    acc = np.zeros((oh, ow, 4))
    # área: cada píxel de origen reparte su peso entre los de destino que toca (exacto para k <= 1)
    xs = np.arange(w + 1) * k; ys = np.arange(h + 1) * k
    for y in range(h):
        oy0, oy1 = ys[y], ys[y + 1]
        for ty in range(int(oy0), min(int(math.ceil(oy1)), oh)):
            wy = min(oy1, ty + 1) - max(oy0, ty)
            if wy <= 0: continue
            for x in range(w):
                if not a[y, x]: continue
                ox0, ox1 = xs[x], xs[x + 1]
                for tx in range(int(ox0), min(int(math.ceil(ox1)), ow)):
                    wx = min(ox1, tx + 1) - max(ox0, tx)
                    if wx <= 0: continue
                    ww = wx * wy
                    acc[ty, tx, :3] += rgb[y, x] * ww; acc[ty, tx, 3] += ww
    cov = acc[..., 3]                                    # fracción cubierta del píxel de destino
    mask = cov >= 0.5
    col = acc[..., :3] / np.maximum(acc[..., 3:4], 1e-6)
    if edge < 1:
        pad = np.pad(mask, 1)
        border = mask & ~(pad[:-2, 1:-1] & pad[2:, 1:-1] & pad[1:-1, :-2] & pad[1:-1, 2:])
        col[border] *= edge
    return (np.clip(col, 0, 1) * 255).astype(np.uint8), mask, (anchor[0] * k, anchor[1] * k)


# ------------------------------------------------------------------ v16.2 r6: pose fija + paso de 8 cuadros
NWALK = 8                         # cuadros por ciclo de paso (dos pasos)
NFRAMES = 1 + NWALK               # 0 = quieto; 1..8 = ciclo (con patas: paso procedural; sin patas: su bucle de reposo)


def choose_pose(F, form):
    """Pose de reposo para hornear: la de Poké Recreo (Amie, la más natural) o, si en esa va por el aire, la de
    combate. Para los que andan, el instante de su bucle con los pies más apoyados (Lucario saltaba en t=0).
    -> (Battle, motion, t, info de walk.classify)"""
    import walk
    from pkanim import Battle, anim
    best = None
    for pri, entry in enumerate((9, 8)):
        bt = Battle(form, entry)
        mot = bt.motion(0)
        if not mot:
            continue
        L = max(mot.frames, 1)
        for k in range(12):
            t = L * k / 12.0
            W = anim.sample_pose(F.model, mot, t)
            info = walk.classify(F.model, W)
            if info["legs"]:
                lift = max(info["foot_y"], 0.0) / info["height"]
                score = (1.0 if lift > 0.12 else 0.0) + 0.2 * pri + lift + 0.001 * k
            else:
                score = 10.0 + pri + 0.001 * k
            if best is None or score < best[0]:
                best = (score, bt, mot, t, info)
    if best is None:
        return None, None, 0.0, None
    return best[1], best[2], best[3], best[4]


def _eye_px(r, F, eyes, yaw, elev, s):
    """píxeles donde se ven los ojos: render normal frente al mismo render sin las mallas de ojos"""
    a, (ax, ay) = r.render(F, yaw, elev, PPU, s, ss=2)
    keep = [F.meshes[i]["visible"] for i in eyes]
    for i in eyes: F.meshes[i]["visible"] = False
    b, (bx, by) = r.render(F, yaw, elev, PPU, s, ss=2)
    for i, v in zip(eyes, keep): F.meshes[i]["visible"] = v
    # alinear por el origen del modelo (el recorte puede cambiar sin los ojos)
    dx, dy = int(round(ax - bx)), int(round(ay - by))
    B = np.zeros_like(a)
    h, w = b.shape[:2]
    y0, x0 = max(0, dy), max(0, dx)
    y1, x1 = min(a.shape[0], h + dy), min(a.shape[1], w + dx)
    if y1 > y0 and x1 > x0:
        B[y0:y1, x0:x1] = b[y0 - dy:y1 - dy, x0 - dx:x1 - dx]
    diff = np.abs(a[..., :3] * a[..., 3:4] - B[..., :3] * B[..., 3:4]).max(-1)
    return int((diff > 0.08).sum())


def choose_elev(r, F, s):
    """v16.2 r6: la cámara del mapa mira desde arriba (41°). Si así no se le ven los ojos (Lampent: el sombrero negro
    tapa el farol, la llama y los ojos y quedaba una mancha negra), se baja la cámara hasta que se vean. Se compara
    con lo que se ve casi de lado (12°): con menos de la mitad están tapados y se baja hasta ver el 80 % (con el
    50 % a Lampent solo se le veía una raya del farol)."""
    eyes = [i for i, x in enumerate(F.meshes) if "eye" in x["mat"].name.lower() and x["visible"]]
    if not eyes:
        return ELEV
    ref = max(_eye_px(r, F, eyes, 0, 12.0, s), _eye_px(r, F, eyes, 45, 12.0, s))
    if ref < 6:
        return ELEV
    def seen(e): return max(_eye_px(r, F, eyes, 0, e, s), _eye_px(r, F, eyes, 45, e, s))
    if seen(ELEV) >= 0.5 * ref:
        return ELEV
    for e in (32.0, 24.0, 16.0):
        if seen(e) >= 0.8 * ref:
            return e
    return 12.0


def bake_species_anim(r, sp, sf=None, scale_mul=1.0):
    """-> dict(views=[dir][frame] = (rgba, (ax, ay)), legs, quad, d, name, stride (unidades del mundo por ciclo, con
    patas), cycle_ms (sin patas: duración de su bucle)). Con patas: pose fija + paso procedural (walk.py v3), sin
    resbalar los pies (stride). Sin patas (flotan, nadan, se arrastran): 8 instantes de su propio bucle de reposo."""
    import walk
    sf = sf or species_forms()
    form = sf[sp][0]
    F = Form(form)
    bt, mot, t0, info = choose_pose(F, form)
    legs = bool(info and info["legs"])
    quad = bool(info and info["quad"])
    F.pose(0, t0, battle=bt)
    s, d = world_scale(F)
    s *= scale_mul; d *= scale_mul
    L = mot.frames if mot else 1
    elev = choose_elev(r, F, s)
    if legs:
        plan = [(t0, None)] + [(t0, 2 * math.pi * k / NWALK) for k in range(NWALK)]
    else:
        plan = [(t0, None)] + [(t0 + L * k / float(NWALK), None) for k in range(NWALK)]
    views = [[None] * NFRAMES for _ in YAWS]
    for fi, (t, ph) in enumerate(plan):
        F.pose(0, t, battle=bt, extra=walk.walk_extra(ph) if ph is not None else None)
        for di, yaw in enumerate(YAWS):
            img, (ox, oy) = r.render(F, yaw, elev, PPU, s, ss=4)
            a = img[..., 3]
            ys, xs = np.nonzero(a > 0.02)
            if not len(xs):
                views[di][fi] = (np.zeros((1, 1, 4), np.float32), (0.0, 0.0)); continue
            x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
            views[di][fi] = (img[y0:y1, x0:x1].copy(), (ox - x0, oy - y0))
    # cuadros anómalos: algún bucle enciende un efecto (llamas de Chandelure/Lampent) que sale como una mancha enorme o
    # deja el dibujo casi vacío. Si el área de un cuadro se aleja mucho de la mediana, se usa el cuadro quieto (0).
    for dv in views:
        areas = [float((v[..., 3] >= 0.5).sum()) for v, a in dv]
        med = float(np.median(areas))
        for fi in range(1, NFRAMES):
            if med > 0 and (areas[fi] > 1.6 * med or areas[fi] < 0.55 * med):
                dv[fi] = dv[0]
    stride_w = walk.stride(info) * s if legs else 0.0
    cycle_ms = (L / 30.0) * 1000.0 if not legs else 0.0
    return dict(views=views, legs=legs, quad=quad, d=d, scale=s, name=F.name, stride=stride_w, cycle_ms=cycle_ms, elev=elev)
