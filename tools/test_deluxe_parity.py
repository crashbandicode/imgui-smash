#!/usr/bin/env python3
"""Host tests for tools/deluxe_parity.py on throwaway git repositories (no network, no Deluxe checkout).

Usage: python3 -I tools/test_deluxe_parity.py
"""
import copy
import importlib.util
import json
import subprocess
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parent / "deluxe_parity.py"
SPEC = importlib.util.spec_from_file_location("deluxe_parity", TOOL)
parity = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(parity)

SHA = "c1" * 32
IMAGE = "devkitpro/devkita64@sha256:" + "1f" * 32
SUBMODULE = "2b" * 20
PATCH = "imgui-smash/patches/imgui/0001-paged-codepoint-index.patch"


def git(repo, *args):
    subprocess.run(["/usr/bin/git", "-C", str(repo), *args], check=True, capture_output=True,
                   env={"GIT_AUTHOR_NAME": "t", "GIT_AUTHOR_EMAIL": "t@t", "GIT_COMMITTER_NAME": "t",
                        "GIT_COMMITTER_EMAIL": "t@t", "GIT_CONFIG_GLOBAL": "/dev/null",
                        "GIT_CONFIG_NOSYSTEM": "1"})


def write(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")


def commit(repo, message, *paths):
    # Named paths only: `add -A` would stage the submodule's gitlink (no checkout here) as deleted.
    if paths:
        git(repo, "add", "--", *paths)
    git(repo, "commit", "-q", "-m", message)
    return subprocess.run(["/usr/bin/git", "-C", str(repo), "rev-parse", "HEAD"], check=True, capture_output=True,
                          text=True).stdout.strip()


class Parity(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.repo = Path(self.tmp.name) / "imgui-smash"
        self.repo.mkdir()
        git(self.repo, "init", "-q")
        write(self.repo / "build.rs", f'const PINNED_SHA256: &str = "{SHA}";\nconst PINNED_SIZE: u64 = 14_820_452;\n')
        write(self.repo / "Build-Standalone.ps1", f'$image = "{IMAGE}"\n')
        write(self.repo / "deterministic-archive.cmake", "set(X qcD)\n")
        write(self.repo / "imgui-smash" / "src" / "a.cpp", "int a;\n")
        write(self.repo / "imgui-smash" / "README.md", "upstream\n")
        write(self.repo / PATCH, "patch\n")
        git(self.repo, "add", "-A")
        git(self.repo, "update-index", "--add", "--cacheinfo", f"160000,{SUBMODULE},{parity.SUBMODULE}")
        self.base = commit(self.repo, "base")
        patch_sha = parity.standalone(self.repo)["patches"][PATCH]
        self.pin = {"sha256": SHA, "size": 14820452, "image": IMAGE,
                    "source": {"commit": self.base, "submodule_commit": SUBMODULE,
                               "fork_imgui_patches": [{"path": PATCH, "sha256": patch_sha}]}}
        self.deluxe = Path(self.tmp.name) / "deluxe"
        self.write_deluxe(self.pin, SHA, "14_820_452")

    def tearDown(self):
        self.tmp.cleanup()

    def write_deluxe(self, pin, build_sha, build_size):
        write(self.deluxe / parity.PIN_REL, json.dumps(pin))
        write(self.deluxe / "build.rs", f'const IMGUI_SMASH_SHA256: &str = "{build_sha}";\n'
                                        f'const IMGUI_SMASH_SIZE: u64 = {build_size};\n')

    def failing(self):
        return [field for field, _, _, same in parity.report(self.repo, self.deluxe) if not same]

    def test_identical_pins_pass(self):
        table = parity.report(self.repo, self.deluxe)
        self.assertEqual(len(table), 8)
        self.assertEqual(self.failing(), [])
        # The pin file alone (no Deluxe build.rs) checks the other six rows.
        self.assertEqual(len(parity.report(self.repo, self.deluxe / parity.PIN_REL)), 6)
        self.assertEqual(parity.main([str(self.deluxe), "--repo", str(self.repo)]), 0)

    def test_each_field_difference_fails(self):
        cases = [("archive sha256", lambda p: p.update(sha256="26" * 32)),
                 ("archive size", lambda p: p.update(size=14788410)),
                 ("devkitA64 image", lambda p: p.update(image=IMAGE[:-1] + "0")),
                 ("Dear ImGui submodule", lambda p: p["source"].update(submodule_commit="aa" * 20)),
                 ("Dear ImGui patches", lambda p: p["source"].update(fork_imgui_patches=[])),
                 ("archive sources", lambda p: p["source"].update(commit=None))]
        for field, change in cases:
            with self.subTest(field=field):
                pin = copy.deepcopy(self.pin)
                change(pin)
                self.write_deluxe(pin, SHA, "14_820_452")
                self.assertEqual(self.failing(), [field])
        for field, build_sha, build_size in (("Deluxe build.rs sha256", "26" * 32, "14_820_452"),
                                             ("Deluxe build.rs size", SHA, "14_788_410")):
            with self.subTest(field=field):
                self.write_deluxe(self.pin, build_sha, build_size)
                self.assertEqual(self.failing(), [field])
        self.assertEqual(parity.main([str(self.deluxe), "--repo", str(self.repo)]), 1)

    def test_source_tree_difference_fails(self):
        write(self.repo / "imgui-smash" / "README.md", "edited\n")
        write(self.repo / "imgui-smash" / "host-tests" / "t.cpp", "int t;\n")
        commit(self.repo, "not archive inputs", "imgui-smash/README.md", "imgui-smash/host-tests")
        self.assertEqual(self.failing(), [])
        write(self.repo / "imgui-smash" / "src" / "a.cpp", "int b;\n")
        commit(self.repo, "an archive input", "imgui-smash/src/a.cpp")
        self.assertEqual(self.failing(), ["archive sources"])
        pin = copy.deepcopy(self.pin)
        pin["source"]["commit"] = "0" * 40
        self.write_deluxe(pin, SHA, "14_820_452")
        row = parity.report(self.repo, self.deluxe)[-1]
        self.assertIn("not in this clone", row[2])


if __name__ == "__main__":
    unittest.main()
