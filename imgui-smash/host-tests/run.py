#!/usr/bin/env python3
"""Builds and runs every host test with the host g++ (C++23). Warnings are errors for the tests and this
repository's sources; Dear ImGui's own files are compiled once without -Werror.

Usage: python3 -I host-tests/run.py [test name ...]
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ["imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp"]
STRICT = ["-Wall", "-Wextra", "-Werror"]
CONFIG = '-DIMGUI_USER_CONFIG="host_imgui_config.h"'
IMGUI_INCLUDES = ["host-tests", "extern/imgui", "fonts/precompiled", "src"]

TESTS = {
    "command_memory_reserve_test": {"include": ["src"]},
    "draw_slot_rules_test": {"include": ["src/imgui_backend"]},
    "frame_guard_test": {"include": ["src"], "sources": ["src/imgui_backend/frame_guard.cpp"]},
    "frame_guard_concurrent_test": {"include": ["src"], "sources": ["src/imgui_backend/frame_guard.cpp"],
                                    "flags": ["-pthread"]},
    "texture_rules_test": {"include": ["src"]},
    "font_setup_test": {"include": IMGUI_INCLUDES, "sources": ["src/imgui_backend/font_setup.cpp"], "imgui": True,
                        "args": ["host-tests/fixtures/label-fixture.ttf", "fonts/JetBrainsMonoNL-Regular.ttf"]},
}


def run(argv, **kwargs):
    done = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True, timeout=900, **kwargs)
    sys.stdout.write(done.stdout)
    sys.stderr.write(done.stderr)
    return done.returncode


def main(argv):
    names = argv[1:] or list(TESTS)
    failed = []
    with tempfile.TemporaryDirectory(prefix="imgui-host-tests-") as tmp:
        core = []
        if any(TESTS[name].get("imgui") for name in names):
            for source in CORE:
                obj = Path(tmp) / (source + ".o")
                flags = ["-std=c++23", "-O1", CONFIG] + [f"-I{i}" for i in IMGUI_INCLUDES]
                if run(["/usr/bin/g++", *flags, "-c", f"extern/imgui/{source}", "-o", str(obj)]) != 0:
                    print(f"FAILED to compile {source}")
                    return 1
                core.append(str(obj))
        for name in names:
            spec = TESTS[name]
            exe = Path(tmp) / name
            flags = ["-std=c++23", "-O1", "-g", *STRICT, *spec.get("flags", []), CONFIG]
            flags += [f"-I{i}" for i in spec["include"]]
            sources = [f"host-tests/{name}.cpp", *spec.get("sources", [])]
            if run(["/usr/bin/g++", *flags, *sources, *(core if spec.get("imgui") else []),
                    *spec.get("flags", []), "-o", str(exe)]) != 0:
                print(f"{name}: BUILD FAILED")
                failed.append(name)
                continue
            code = run([str(exe), *spec.get("args", [])])
            print(f"{name}: {'PASS' if code == 0 else f'FAIL (exit {code})'}")
            if code != 0:
                failed.append(name)
    print(f"host tests: {len(names) - len(failed)} passed, {len(failed)} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
