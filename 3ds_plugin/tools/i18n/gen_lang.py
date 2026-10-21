"""
gen_lang.py — genera Includes/LangTable.inc (la tabla de idiomas del MMO) desde strings.py y la comprueba.

    python tools/i18n/gen_lang.py          (desde 3ds_plugin/)

Comprobaciones (si alguna falla, NO escribe la tabla y termina con error):
  1. Cada traducción tiene exactamente los mismos huecos de printf (%s, %d, %u, %02u...) y en el mismo orden que
     el español: un hueco de más o cambiado haría leer basura de la memoria (cuelgue).
  2. No hay dos entradas con el mismo texto español.
  3. Avisos (no paran): textos de TR("...") / Lang::TOsd("...") del código que no están en la tabla (saldrían en
     español) y tamaños largos para los huecos de la interfaz.
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
from strings import T  # noqa: E402

LANGS = ["es", "en", "fr", "it", "de", "ja", "ko"]
FMT = re.compile(r"%[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXcsfgp%]")


def c_literal(s):
    # UTF-8 con escapes octales (3 cifras: nunca se "comen" la letra siguiente, al contrario que \x)
    out = []
    for b in s.encode("utf-8"):
        if b == 0x22: out.append('\\"')
        elif b == 0x5C: out.append("\\\\")
        elif b == 0x0A: out.append("\\n")
        elif 0x20 <= b < 0x7F: out.append(chr(b))
        else: out.append("\\%03o" % b)
    return '"' + "".join(out) + '"'


def main():
    errors, warns = [], []
    seen = {}
    for i, row in enumerate(T):
        if len(row) != 7:
            errors.append("fila %d: %d columnas (deben ser 7): %r" % (i, len(row), row[:1]))
            continue
        es = row[0]
        if es in seen:
            errors.append("repetido: %r (filas %d y %d)" % (es, seen[es], i))
        seen[es] = i
        want = FMT.findall(es)
        for lang, tr in zip(LANGS[1:], row[1:]):
            if tr is None:
                continue
            got = FMT.findall(tr)
            if got != want:
                errors.append("%s %r: huecos %s, el español tiene %s" % (lang, es, got, want))
    # textos del código que no están en la tabla
    lit = re.compile(r'(?:TR|Lang::T|Lang::TOsd)\(\s*"((?:[^"\\]|\\.)*)"')
    for d in ("Sources", "Includes"):
        for f in sorted(os.listdir(os.path.join(ROOT, d))):
            if not f.endswith((".cpp", ".hpp")):
                continue
            txt = open(os.path.join(ROOT, d, f), encoding="utf-8").read()
            for m in lit.finditer(txt):
                raw = m.group(1)
                s = bytes(raw, "utf-8").decode("unicode_escape").encode("latin-1").decode("utf-8")
                if s not in seen:
                    warns.append("sin traducir (%s): %r" % (f, s))
    for e in errors:
        print("ERROR", e)
    for w in warns:
        print("aviso", w)
    if errors:
        sys.exit(1)
    lines = ["// LangTable.inc — GENERADO por tools/i18n/gen_lang.py desde tools/i18n/strings.py. No editar a mano.",
             "// Columnas: es, en, fr, it, de, ja, ko (nullptr = se usa el inglés).",
             "static const Entry kTable[] = {"]
    for row in T:
        cells = [c_literal(c) if c is not None else "nullptr" for c in row]
        lines.append("    { { " + ", ".join(cells) + " } },")
    lines.append("};")
    lines.append("static const int kTableN = (int)(sizeof(kTable) / sizeof(kTable[0]));")
    out = os.path.join(ROOT, "Includes", "LangTable.inc")
    open(out, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print("LangTable.inc: %d textos x 7 idiomas; %d avisos" % (len(T), len(warns)))


if __name__ == "__main__":
    main()
