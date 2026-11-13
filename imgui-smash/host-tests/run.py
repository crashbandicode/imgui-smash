#!/usr/bin/env python3
"""Builds and runs every host test with the host g++ (C++23). Warnings are errors for the tests and this
repository's sources; Dear ImGui's own files are compiled once without -Werror.

Dear ImGui is built as CMakeLists.txt builds it: the extern/imgui sources with patches/imgui/*.patch applied (git
apply, in name order) to a copy. paged_index_test also builds the unpatched sources, and compares what both answer.

Usage: python3 -I host-tests/run.py [test name ...]
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "extern" / "imgui"
PATCHES = sorted((ROOT / "patches" / "imgui").glob("*.patch"))
CORE = ["imgui.cpp", "imgui_draw.cpp", "imgui_tables.cpp", "imgui_widgets.cpp"]
STRICT = ["-Wall", "-Wextra", "-Werror"]
CONFIG = '-DIMGUI_USER_CONFIG="host_imgui_config.h"'
FIXTURES = ["host-tests/fixtures/label-fixture.ttf", "fonts/JetBrainsMonoNL-Regular.ttf"]

TESTS = {
    "command_memory_reserve_test": {"include": ["src"]},
    "draw_slot_rules_test": {"include": ["src/imgui_backend"]},
    "frame_guard_test": {"include": ["src"], "sources": ["src/imgui_backend/frame_guard.cpp"]},
    "frame_guard_concurrent_test": {"include": ["src"], "sources": ["src/imgui_backend/frame_guard.cpp"],
                                    "flags": ["-pthread"]},
    "texture_rules_test": {"include": ["src"]},
    "font_setup_test": {"sources": ["src/imgui_backend/font_setup.cpp"], "imgui": True, "args": FIXTURES},
    "paged_index_test": {"imgui": True, "compare": True},
    "nav_toggle_test": {"include": ["src"]},
    "overlay_texture_policy_test": {"include": ["src/imgui_backend"]},
}

# paged_index_test lines: "bench " never compared; "mode " never compared; "bytes " compared only between the
# unpatched and the patched dense runs (the paged index differs there by design); everything else in all three.
RUNS = [("upstream", "dense"), ("patched", "dense"), ("patched", "paged")]


def run(argv, **kwargs):
    done = subprocess.run(argv, cwd=ROOT, text=True, capture_output=True, timeout=900, **kwargs)
    sys.stdout.write(done.stdout)
    sys.stderr.write(done.stderr)
    return done.returncode


def imgui_copy(tmp, patched):
    """The ImGui sources the build uses: extern/imgui's top-level files, patched or not."""
    out = Path(tmp) / ("imgui-patched" if patched else "imgui-upstream")
    out.mkdir()
    for path in sorted(UPSTREAM.iterdir()):
        if path.is_file() and path.suffix in (".cpp", ".h"):
            shutil.copy2(path, out / path.name)
    env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(Path(tmp)))
    for patch in PATCHES if patched else []:
        done = subprocess.run(["/usr/bin/git", "apply", "--whitespace=nowarn", str(patch)], cwd=out, env=env,
                              text=True, capture_output=True, timeout=120)
        if done.returncode != 0:
            sys.stderr.write(done.stderr)
            raise SystemExit(f"{patch.name} does not apply to extern/imgui")
    return out


def compile_core(tmp, imgui_dir):
    objs = []
    for source in CORE:
        obj = Path(tmp) / f"{imgui_dir.name}-{source}.o"
        flags = ["-std=c++23", "-O2", CONFIG, "-Ihost-tests", f"-I{imgui_dir}", "-Ifonts/precompiled"]
        if run(["/usr/bin/g++", *flags, "-c", str(imgui_dir / source), "-o", str(obj)]) != 0:
            raise SystemExit(f"FAILED to compile {imgui_dir.name}/{source}")
        objs.append(str(obj))
    return objs


def build(tmp, name, spec, imgui_dir, core, suffix=""):
    exe = Path(tmp) / f"{name}{suffix}"
    includes = spec.get("include", ["host-tests", str(imgui_dir), "fonts/precompiled", "src"])
    flags = ["-std=c++23", "-O2" if spec.get("imgui") else "-O1", "-g", *STRICT, *spec.get("flags", []), CONFIG]
    flags += [f"-I{i}" for i in includes]
    sources = [f"host-tests/{name}.cpp", *spec.get("sources", [])]
    if run(["/usr/bin/g++", *flags, *sources, *core, *spec.get("flags", []), "-o", str(exe)]) != 0:
        return None
    return exe


def comparable(lines, kind):
    drop = ("bench ", "mode ") + (("bytes ",) if kind == "all-but-bytes" else ())
    return [line for line in lines if not line.startswith(drop)]


def compare_runs(tmp, name, exes):
    """paged_index_test: the unpatched dense run, the patched dense run and the patched paged run answer the same."""
    dumps = {}
    for build_name, mode in RUNS:
        out = Path(tmp) / f"{name}-{build_name}-{mode}.txt"
        code = run([str(exes[build_name]), mode, *FIXTURES, str(out)])
        if code != 0:
            print(f"{name}: {build_name} {mode} exited {code}")
            return False
        dumps[(build_name, mode)] = out.read_text().splitlines()
    ok = True
    for a, b, kind in [(RUNS[0], RUNS[1], "all"), (RUNS[0], RUNS[2], "all-but-bytes")]:
        left, right = comparable(dumps[a], kind), comparable(dumps[b], kind)
        same = left == right
        print(f"{name}: {' '.join(a)} vs {' '.join(b)}: {len(left)} lines compared, "
              f"{'identical' if same else 'DIFFERENT'}")
        if not same:
            ok = False
            for i, (x, y) in enumerate(zip(left, right)):
                if x != y:
                    print(f"  first difference at line {i + 1}:\n    {x}\n    {y}")
                    break
            else:
                print(f"  lengths differ: {len(left)} vs {len(right)}")
    for key, lines in dumps.items():
        for line in lines:
            if line.startswith(("mode ", "bytes ")) and " final " in f" {line} ":
                print(f"  [{' '.join(key)}] {line}")
    return ok and bench(tmp, name, exes)


BENCH_RUNS = 5


def bench(tmp, name, exes):
    """CalcTextSize per label: each build's bench mode BENCH_RUNS times, alternating builds. Reports each variant's
    median over the runs with their range, and the paged/dense ratio, paired within each patched run (both modes
    alternate round by round in one process, so they share the machine's load)."""
    taskset = ["/usr/bin/taskset", "-c", "2"] if Path("/usr/bin/taskset").exists() else []
    results, ratios = {}, {}
    for attempt in range(BENCH_RUNS):
        for build_name in ("upstream", "patched"):
            out = Path(tmp) / f"{name}-bench-{build_name}-{attempt}.txt"
            if run([*taskset, str(exes[build_name]), "bench", *FIXTURES, str(out)]) != 0:
                print(f"{name}: bench {build_name} failed")
                return False
            run_values = {}
            for line in out.read_text().splitlines():
                if line.startswith("bench "):
                    _, label, variant, ns = line.split()
                    results.setdefault((label, variant), []).append(float(ns))
                    run_values[(label, variant)] = float(ns)
            for (label, variant), ns in run_values.items():
                if variant == "patched-paged":
                    ratios.setdefault(label, []).append(ns / run_values[(label, "patched-dense")])
    labels = list(dict.fromkeys(label for label, _ in results))
    variants = ["upstream-dense", "patched-dense", "patched-paged"]
    median = lambda values: sorted(values)[len(values) // 2]
    print(f"{name}: CalcTextSize, ns per call: median of {BENCH_RUNS} runs [min-max], each run the median of "
          f"31 rounds x 50,000 calls")
    print(f"  {'label':<11}" + "".join(f"{v:>24}" for v in variants) + f"{'paged/dense (paired)':>28}")
    for label in labels:
        cells = []
        for v in variants:
            values = results[(label, v)]
            cells.append(f"{median(values):.1f} [{min(values):.0f}-{max(values):.0f}]")
        r = ratios[label]
        print(f"  {label:<11}" + "".join(f"{c:>24}" for c in cells) +
              f"{f'{median(r):.3f} [{min(r):.3f}-{max(r):.3f}]':>28}")
    return True


def main(argv):
    names = argv[1:] or list(TESTS)
    failed = []
    with tempfile.TemporaryDirectory(prefix="imgui-host-tests-") as tmp:
        patched = upstream = None
        core = {}
        if any(TESTS[name].get("imgui") for name in names):
            patched = imgui_copy(tmp, True)
            core["patched"] = compile_core(tmp, patched)
        if any(TESTS[name].get("compare") for name in names):
            upstream = imgui_copy(tmp, False)
            core["upstream"] = compile_core(tmp, upstream)
        for name in names:
            spec = TESTS[name]
            if spec.get("compare"):
                exes = {key: build(tmp, name, spec, d, core[key], f"-{key}")
                        for key, d in (("upstream", upstream), ("patched", patched))}
                if not all(exes.values()):
                    print(f"{name}: BUILD FAILED")
                    failed.append(name)
                    continue
                ok = compare_runs(tmp, name, exes)
                print(f"{name}: {'PASS' if ok else 'FAIL'}")
                if not ok:
                    failed.append(name)
                continue
            exe = build(tmp, name, spec, patched, core["patched"] if spec.get("imgui") else [])
            if exe is None:
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
