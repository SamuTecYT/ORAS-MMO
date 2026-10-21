"""Shared helpers for tools/prepublish_check.py and tools/package_public.py.

Private patterns live in `.private_patterns.txt` at the repository root (git-ignored, never published): one regular
expression per line (case-sensitive; `#` starts a comment). Put there anything that identifies you or your community:
your server IP, real names, e-mails, hosting paths, passwords...
"""
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PATTERNS_FILE = os.path.join(ROOT, ".private_patterns.txt")

# Files that must never be published, whatever their content
FORBIDDEN_NAMES = [
    (re.compile(r"(^|/)mmo_pokes_[^/]*\.bin$"), "game-derived graphics pack"),
    (re.compile(r"\.(3gx|zip|cia|3ds|cxi|cci|sav)$", re.I), "binary / ROM / save / release archive (publish builds as GitHub Releases)"),
    (re.compile(r"(^|/)ranking_[^/]*\.json$"), "server ranking (players' nicknames and stats)"),
    (re.compile(r"(^|/)\.env$"), "environment file with secrets (only .env.example may be published)"),
    (re.compile(r"(^|/)ServerConfig\.local\.hpp$"), "your private default server"),
    (re.compile(r"(^|/)\.private_patterns\.txt$"), "the private patterns list itself"),
    (re.compile(r"(^|/)mmo_(server|room|name|settings|log|ip)\.txt$"), "plugin runtime file from a real install"),
]

# Secret-looking content, checked in every text file
GENERIC_SECRETS = [
    (re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----"), "private key"),
    (re.compile(r"\bgh[pousr]_[A-Za-z0-9]{30,}"), "GitHub token"),
    (re.compile(r"\bAKIA[0-9A-Z]{16}\b"), "AWS access key"),
    (re.compile(r"discord(?:app)?\.com/api/webhooks/\d+/[\w-]+"), "Discord webhook URL"),
    (re.compile(r"\bxox[abpr]-[A-Za-z0-9-]{10,}"), "Slack token"),
    (re.compile(r"\bsk-[A-Za-z0-9]{32,}"), "API secret key"),
]


def load_private_patterns():
    pats = []
    if not os.path.exists(PATTERNS_FILE):
        return pats
    with open(PATTERNS_FILE, encoding="utf-8") as f:
        for n, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            try:
                pats.append((re.compile(line), "private pattern line %d" % n))
            except re.error as e:
                raise SystemExit("%s:%d: invalid pattern (%s)" % (PATTERNS_FILE, n, e))
    return pats


def scan_bytes(data, patterns):
    """Matches of the patterns in raw bytes (decoded as UTF-8 and as Latin-1, so binaries are covered too)."""
    hits = []
    texts = {data.decode("utf-8", "replace"), data.decode("latin-1")}
    for rx, why in patterns:
        for t in texts:
            if rx.search(t):
                hits.append(why)
                break
    return hits
