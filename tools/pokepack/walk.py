"""Paso procedural para cualquier Pokémon a partir de SU esqueleto (nombres estándar de Game Freak, iguales en los 721:
Origin, Waist, Hips, LThigh/LLeg/LFoot/LToe*, Spine*, Neck*, Head, LArm/LForeArm/LHand, Tail*).
Se aplica ENCIMA de una pose de reposo fija: así cada Pokémon conserva su postura y solo mueve lo que camina.

v3:
  - 8 cuadros por ciclo (antes 4) y pasos más marcados: muslo, rodilla que se dobla al levantar el pie, tobillo que
    acompaña; cuadrúpedos al trote (pares en diagonal) con su "rodilla" delantera.
  - el cuerpo sube y baja con cada paso de forma natural: tras mover las patas se baja/sube TODO el esqueleto para que
    el pie de apoyo toque el suelo (sin pies flotando ni hundidos).
  - balanceo: la cadera se inclina hacia el pie de apoyo, los hombros giran al revés que la cadera, los brazos (bípedos)
    van al revés que las piernas, la cabeza compensa y la cola se mece con retraso a lo largo de sus huesos.

walk_extra(phase) -> función para Form.pose(..., extra=...) que recibe las matrices de mundo y devuelve las nuevas.
Fase 0..2π = un ciclo (dos pasos): 0 = pie izquierdo pasando (rodilla doblada), π/2 = izquierdo delante (contacto),
π = derecho pasando, 3π/2 = derecho delante. Ejes en espacio del modelo: mira a +Z, Y arriba, X a su izquierda.
"""
import math, re
import numpy as np

A_LEG = math.radians(34)        # muslos (adelante/atrás)
B_KNEE = math.radians(52)       # rodilla al levantar el pie
A_FRONT = math.radians(30)      # patas delanteras (cuadrúpedos)
B_FRONT = math.radians(42)
A_ARM = math.radians(24)        # brazos (bípedos)
ROLL = math.radians(3.5)        # cadera hacia el pie de apoyo
TWIST = math.radians(5)         # hombros al revés que la cadera (bípedos)
A_TAIL = math.radians(11)       # cola (por hueso, con retraso)
A_HEAD = math.radians(3)

FOOT_RE = re.compile(r"^(End)?[LR](Foot|Toe\w*)$")
HAND_RE = re.compile(r"^(End)?[LR]Hand$")


def _rot(axis, ang):
    x, y, z = axis; c, s = math.cos(ang), math.sin(ang); C = 1 - c
    return np.array([[c + x * x * C, x * y * C - z * s, x * z * C + y * s],
                     [y * x * C + z * s, c + y * y * C, y * z * C - x * s],
                     [z * x * C - y * s, z * y * C + x * s, c + z * z * C]])


def classify(model, W):
    """-> dict con los huesos por función: legs (anda con patas: muslos y pies, y los pies son lo más bajo del
    esqueleto), quad (las manos también pisan el suelo), tails, y medidas (suelo, alto, largo de pierna)."""
    names = {b.name: b.index for b in model.bones}
    def has(n): return n in names
    allY = [float(w[1, 3]) for w in W]
    top, bottom = max(allY), min(allY)
    h = max(top - bottom, 1e-3)
    feet = [names[n] for n in names if FOOT_RE.match(n)]
    hands = [names[n] for n in names if HAND_RE.match(n)]
    foot_y = min(float(W[i][1, 3]) for i in feet) if feet else None
    legs = bool(feet) and has("LThigh") and has("RThigh") and foot_y <= bottom + 0.15 * h
    # cuadrúpedo: las manos pisan el suelo Y van por delante de los pies (Gardevoir baja los brazos a los lados)
    quad = False
    if legs and hands and min(float(W[i][1, 3]) for i in hands) <= bottom + 0.2 * h:
        hz = np.mean([float(W[i][2, 3]) for i in hands]); fz = np.mean([float(W[i][2, 3]) for i in feet])
        quad = hz - fz > 0.12 * h
    hip_y = 0.5 * (float(W[names["LThigh"]][1, 3]) + float(W[names["RThigh"]][1, 3])) if legs else 0.0
    tails = sorted([b.name for b in model.bones if re.match(r"^Tail\d*$", b.name)], key=lambda n: names[n])
    return dict(names=names, legs=legs, quad=quad, arms=has("LArm") and has("RArm"), tails=tails[:8],
                head=has("Head"), feet=feet, hands=hands, foot_y=foot_y, leg_len=max(hip_y - (foot_y or 0.0), 0.0),
                height=h)


def stride(info):
    """unidades del modelo que avanza en un ciclo (dos pasos) sin que los pies resbalen"""
    return 4.0 * info["leg_len"] * math.sin(A_LEG) if info["legs"] else 0.0


def walk_extra(phase):
    def f(F, W):
        m = F.model
        info = classify(m, W)
        names = info["names"]
        if not info["legs"]:
            return W
        s, c = math.sin(phase), math.cos(phase)
        X, Y, Z = (1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)
        d = {}                                              # hueso -> [(eje, ángulo)]
        def add(n, ax, a):
            if n in names: d.setdefault(n, []).append((ax, a))
        # cadera hacia el pie de apoyo (el de detrás en cada contacto) y hombros al revés
        add("Waist", Z, ROLL * s)
        # piernas: muslo, rodilla (solo la que va por el aire) y tobillo que mantiene el pie casi plano
        lt, rt = -A_LEG * s, A_LEG * s
        lk, rk = B_KNEE * max(0.0, c), B_KNEE * max(0.0, -c)
        add("LThigh", X, lt); add("RThigh", X, rt)
        add("LLeg", X, lk); add("RLeg", X, rk)
        add("LFoot", X, -0.55 * (lt + lk)); add("RFoot", X, -0.55 * (rt + rk))
        if info["quad"]:
            # trote: delantera izquierda con trasera derecha
            lf, rf = A_FRONT * s, -A_FRONT * s
            add("LArm", X, lf); add("RArm", X, rf)
            add("LForeArm", X, B_FRONT * max(0.0, -c)); add("RForeArm", X, B_FRONT * max(0.0, c))
            add("Spine1", Y, -0.5 * TWIST * s)
        else:
            add("Spine1", Y, -TWIST * s)
            if info["arms"]:
                add("LArm", X, A_ARM * s); add("RArm", X, -A_ARM * s)
        for i, t in enumerate(info["tails"]):
            k = 1.0 / (1.0 + 0.25 * i)
            add(t, Y, A_TAIL * k * math.sin(phase - 0.7 * (i + 1)))
        if info["head"]:
            add("Head", X, A_HEAD * math.cos(2 * phase))
            add("Head", Z, -0.7 * ROLL * s)
        # solo se rehacen los huesos afectados (los girados y sus descendientes); el resto conserva su matriz
        affected = [False] * len(m.bones)
        for b in m.bones:
            affected[b.index] = b.name in d or (b.parent >= 0 and affected[b.parent])
        out = []
        for b in m.bones:
            if not affected[b.index]:
                out.append(W[b.index]); continue
            if b.parent < 0:
                Wn = W[b.index]
            else:
                try:
                    Pi = np.linalg.inv(W[b.parent])
                except np.linalg.LinAlgError:                 # hueso con escala 0 (piezas ocultas así)
                    Pi = np.linalg.pinv(W[b.parent])
                Wn = out[b.parent] @ (Pi @ W[b.index])
            for ax, ang in d.get(b.name, ()):
                R = np.eye(4); R[:3, :3] = _rot(ax, ang)
                p = Wn[:3, 3].copy()
                T1 = np.eye(4); T1[:3, 3] = p
                T0 = np.eye(4); T0[:3, 3] = -p
                Wn = T1 @ R @ T0 @ Wn
            out.append(Wn)
        # el pie de apoyo toca el suelo: todo el cuerpo baja en los contactos y sube al pasar la pierna
        ground = info["feet"] + (info["hands"] if info["quad"] else [])
        y0 = min(float(W[i][1, 3]) for i in ground)
        y1 = min(float(out[i][1, 3]) for i in ground)
        dy = y0 - y1
        res = []
        for M in out:
            M2 = M.copy(); M2[1, 3] += dy
            res.append(M2)
        return res
    return f
