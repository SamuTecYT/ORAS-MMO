"""Checks everything git would publish before you push.

    python tools/prepublish_check.py

It lists the files git would publish (tracked + new files that are not ignored) and fails if any of them
  - is a file that must never be shared (game graphics pack, builds/ZIPs, rankings, .env, your private server config),
  - contains one of your private patterns (`.private_patterns.txt`, git-ignored: your IP, names, e-mails...),
  - looks like a secret (private keys, tokens, webhooks).
Exit code 0 = OK to publish, 1 = something to fix first. It never prints the matched private text.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from privacy import ROOT, FORBIDDEN_NAMES, GENERIC_SECRETS, PATTERNS_FILE, load_private_patterns, scan_bytes  # noqa: E402


def git_files():
    def run(*args):
        out = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
        if out.returncode != 0:
            raise SystemExit("git failed: " + out.stderr.strip())
        return [l for l in out.stdout.splitlines() if l]
    tracked = set(run("ls-files", "-c"))
    deleted = set(run("ls-files", "-d"))
    new = set(run("ls-files", "-o", "--exclude-standard"))
    ignored_tracked = set(run("ls-files", "-c", "-i", "--exclude-standard"))
    return sorted((tracked - deleted) | new), sorted(ignored_tracked - deleted)


def main():
    files, ignored_tracked = git_files()
    private = load_private_patterns()
    if not private:
        print("[!] No %s: only generic checks. Create it with your IP, names, e-mails... (one regex per line)."
              % os.path.basename(PATTERNS_FILE))
    problems = 0
    for f in ignored_tracked:
        print("[X] %s - tracked but now ignored: run  git rm --cached \"%s\"" % (f, f))
        problems += 1
    for f in files:
        for rx, why in FORBIDDEN_NAMES:
            if rx.search(f):
                print("[X] %s - %s" % (f, why))
                problems += 1
                break
        path = os.path.join(ROOT, f)
        if not os.path.isfile(path):
            continue
        with open(path, "rb") as fh:
            data = fh.read()
        for why in scan_bytes(data, private):
            print("[X] %s - contains %s" % (f, why))
            problems += 1
        if b"\0" not in data[:4096]:
            for why in scan_bytes(data, GENERIC_SECRETS):
                print("[X] %s - looks like a %s" % (f, why))
                problems += 1
    print("%d files checked, %d private patterns, %d problem(s)." % (len(files), len(private), problems))
    if problems:
        print("NOT ready to publish.")
        return 1
    print("OK: nothing private found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
