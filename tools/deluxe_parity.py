#!/usr/bin/env python3
"""Is the standalone current with the ImGui archive Online Deluxe links?

Puts this repository's pin (build.rs PINNED_SHA256 and PINNED_SIZE, Build-Standalone.ps1's devkitA64 image, HEAD's
Dear ImGui submodule and patches) and Deluxe's (patches/imgui-smash/archive-pin.json and, given the repository, its
build.rs) through one table of fields. The last row checks that the imgui-smash commit Deluxe names builds from the
same inputs as HEAD: no difference in imgui-smash/ or deterministic-archive.cmake, except imgui-smash/README.md and
imgui-smash/host-tests/, which the archive build does not read.

Usage: python3 -I tools/deluxe_parity.py <Deluxe repository or its archive-pin.json> [--repo <imgui-smash>]
Exits 0 when every row matches, 1 otherwise.
"""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PIN_REL = Path("patches/imgui-smash/archive-pin.json")
SUBMODULE = "imgui-smash/extern/imgui"
NOT_INPUTS = ("imgui-smash/README.md", "imgui-smash/host-tests")


def git(repo, *args):
    done = subprocess.run(["/usr/bin/git", "-C", str(repo), *args], capture_output=True, text=True, check=False)
    return done.returncode, done.stdout.strip()


def rust_const(text, name):
    match = re.search(rf"const {name}: [^=]+= \"?([0-9a-f_]+)\"?;", text)
    return match.group(1).replace("_", "") if match else None


def standalone(repo):
    """This repository's side: the archive pin, the build image, and HEAD's submodule and Dear ImGui patches."""
    build = (repo / "build.rs").read_text(encoding="utf-8")
    script = (repo / "Build-Standalone.ps1").read_text(encoding="utf-8")
    image = re.search(r'\$image = "([^"]+)"', script)
    _, tree = git(repo, "ls-tree", "HEAD", SUBMODULE)
    patches = {}
    for path in sorted((repo / "imgui-smash" / "patches" / "imgui").glob("*.patch")):
        patches[path.relative_to(repo).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()
    size = rust_const(build, "PINNED_SIZE")
    return {"sha256": rust_const(build, "PINNED_SHA256"), "size": int(size) if size else None,
            "image": image.group(1) if image else None,
            "submodule": tree.split()[2] if len(tree.split()) > 2 else None, "patches": patches}


def deluxe(target):
    """Deluxe's side from archive-pin.json, plus its build.rs constants when `target` is the repository."""
    target = Path(target)
    pin_path = target / PIN_REL if target.is_dir() else target
    pin = json.loads(pin_path.read_text(encoding="utf-8"))
    source = pin.get("source", {})
    out = {"sha256": pin.get("sha256"), "size": pin.get("size"), "image": pin.get("image"),
           "submodule": source.get("submodule_commit"), "commit": source.get("commit"),
           "patches": {p["path"]: p["sha256"] for p in source.get("fork_imgui_patches") or []}}
    if target.is_dir() and (target / "build.rs").is_file():
        build = (target / "build.rs").read_text(encoding="utf-8")
        size = rust_const(build, "IMGUI_SMASH_SIZE")
        out["build_rs_sha256"] = rust_const(build, "IMGUI_SMASH_SHA256")
        out["build_rs_size"] = int(size) if size else None
    return out


def rows(mine, theirs):
    """(field, standalone, Deluxe, same) for every field both sides record."""
    table = [("archive sha256", mine["sha256"], theirs["sha256"]),
             ("archive size", mine["size"], theirs["size"]),
             ("devkitA64 image", mine["image"], theirs["image"]),
             ("Dear ImGui submodule", mine["submodule"], theirs["submodule"]),
             ("Dear ImGui patches", mine["patches"], theirs["patches"])]
    if "build_rs_sha256" in theirs:
        table += [("Deluxe build.rs sha256", mine["sha256"], theirs["build_rs_sha256"]),
                  ("Deluxe build.rs size", mine["size"], theirs["build_rs_size"])]
    return [(field, a, b, a is not None and a == b) for field, a, b in table]


def source_row(repo, commit):
    """Whether `commit` and HEAD give the archive build the same inputs."""
    if not commit:
        return ("archive sources", "HEAD", "no commit pinned", False)
    code, _ = git(repo, "cat-file", "-e", f"{commit}^{{commit}}")
    if code != 0:
        return ("archive sources", "HEAD", f"{commit[:12]} not in this clone", False)
    spec = ["imgui-smash", "deterministic-archive.cmake"] + [f":(exclude){p}" for p in NOT_INPUTS]
    _, changed = git(repo, "diff", "--name-only", commit, "HEAD", "--", *spec)
    return ("archive sources", "HEAD", f"{commit[:12]}" + (f" differs: {changed.split()}" if changed else ""),
            not changed)


def report(repo, target):
    mine, theirs = standalone(repo), deluxe(target)
    return rows(mine, theirs) + [source_row(repo, theirs["commit"])]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("deluxe", help="Deluxe repository or its archive-pin.json")
    parser.add_argument("--repo", type=Path, default=ROOT, help="imgui-smash repository (default: this one)")
    args = parser.parse_args(argv)
    table = report(args.repo.resolve(), args.deluxe)
    print("| Field | Standalone | Deluxe | Same |")
    print("| --- | --- | --- | --- |")
    for field, a, b, same in table:
        print(f"| {field} | {a} | {b} | {'yes' if same else 'NO'} |")
    return 0 if all(same for *_, same in table) else 1


if __name__ == "__main__":
    sys.exit(main())
