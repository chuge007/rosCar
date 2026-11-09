#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Parse qmake-generated nmake Makefiles (MSVC) and emit compile_commands.json
for clangd at the workspace root.

Usage:
    python gen_compile_commands.py
"""

import json
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

# Priority order (first hit wins for a given source file).
MAKEFILE_DIRS = [
    os.path.join(ROOT, "build", "workbench_release"),
    os.path.join(ROOT, "build", "host_release"),
    ROOT,
    os.path.join(ROOT, "bin"),
    os.path.join(ROOT, "robot_comm_test"),
] + [
    os.path.join(ROOT, "build", d)
    for d in sorted(os.listdir(os.path.join(ROOT, "build")))
    if d.startswith("Desktop_Qt_")
] if os.path.isdir(os.path.join(ROOT, "build")) else []

MAKEFILE_NAMES = ["Makefile.Release", "Makefile.Debug", "Makefile"]
SRC_EXTS = [".cpp", ".cc", ".cxx", ".c"]


def read_makefile(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().replace("\r\n", "\n").split("\n")
    # join continuation lines ending with backslash
    merged, buf = [], None
    for line in lines:
        if buf is not None:
            buf = buf.rstrip("\\") + " " + line.strip()
            if buf.endswith("\\"):
                continue
            merged.append(buf)
            buf = None
        elif line.rstrip().endswith("\\") and " = " in line:
            buf = line
            continue
        else:
            merged.append(line)
    if buf is not None:
        merged.append(buf)
    return merged


def parse_vars(lines):
    vars_ = {}
    for line in lines:
        if line.startswith("\t") or "=" not in line or line.lstrip().startswith("#"):
            continue
        name, _, value = line.partition("=")
        name = name.strip()
        if not name.replace("_", "").isalnum():
            continue
        # strip inline comments like "value #comment"
        if " #" in value:
            value = value.split(" #", 1)[0]
        vars_[name] = value.strip()
    return vars_


def expand(value, vars_):
    for _ in range(8):
        new = value
        for k, v in vars_.items():
            new = new.replace("$(%s)" % k, v)
        if new == value:
            break
        value = new
    return value


def parse_rules_and_objects(lines):
    """Return (inference_rules, explicit_src_map, objects).

    inference_rules: list of (srcdir, ext) from "{dir}.cpp{objdir}.obj::" blocks
    explicit_src_map: {objpath: srcpath} from "obj.obj: src.cpp" rules
    objects: list of obj paths from OBJECTS
    """
    rules, explicit, objects = [], {}, []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        # inference rule header
        if stripped.startswith("{") and ".obj::" in stripped:
            header = stripped.strip(":")
            try:
                left = header.split("}", 1)[0][1:]      # srcdir.cpp
                srcdir, _, ext = left.rpartition(".")
                rules.append((srcdir.strip(), ext.strip()))
            except Exception:
                pass
        # explicit object rule: "<obj>: <src> ..."
        elif ".obj:" in stripped and not stripped.startswith("\t"):
            target = stripped.partition(":")[0].strip()
            if target.endswith(".obj") and " " not in target:
                # prerequisite(s) on the same line
                prereqs = stripped.partition(":")[2].split()
                for p in prereqs:
                    if os.path.splitext(p)[1] in SRC_EXTS:
                        explicit[target] = p
                        break
                # prerequisite(s) on following lines (nmake "obj: \\\n  src")
                j = i + 1
                while j < len(lines) and not explicit.get(target) and lines[j].strip().startswith("\\") is False and ":" not in lines[j] and lines[j].strip() and not lines[j].startswith("\t"):
                    for p in lines[j].strip().split():
                        if os.path.splitext(p)[1] in SRC_EXTS:
                            explicit[target] = p
                            break
                    j += 1
        # OBJECTS list
        elif stripped.startswith("OBJECTS") and "=" in stripped:
            value = stripped.partition("=")[2]
            objects.extend(
                tok for tok in value.split() if tok.endswith(".obj")
            )
            # continuation lines are already merged
        i += 1
    return rules, explicit, objects


def build_entry(mfdir, vars_, cmd_line, src, objdir):
    args = ["cl"] + cmd_line.split()
    out = []
    for a in args:
        if a.startswith("-I") or a.startswith("/I"):
            inc = a[2:]
            out.append("-I" + os.path.normpath(os.path.join(mfdir, inc)) if not os.path.isabs(inc) else "-I" + inc)
        else:
            out.append(a)
    out.append(os.path.normpath(os.path.join(mfdir, src)))
    return {
        "directory": mfdir,
        "arguments": out,
        "file": os.path.normpath(os.path.join(mfdir, src)),
    }


def process_makefile(mfpath):
    mfdir = os.path.dirname(mfpath)
    lines = read_makefile(mfpath)
    vars_ = parse_vars(lines)
    rules, explicit, objects = parse_rules_and_objects(lines)
    if not objects:
        return []

    cxx = expand(vars_.get("CXX", "cl"), vars_)
    cxxflags = expand(vars_.get("CXXFLAGS", ""), vars_)
    cflags = expand(vars_.get("CFLAGS", ""), vars_)
    incpath = expand(vars_.get("INCPATH", ""), vars_)
    # object output dir taken from the rule lines, e.g. "-Fo..\obj\"
    objdir = "..\\obj\\"
    for line in lines:
        if "$(CXX) -c" in line and "-Fo" in line:
            for tok in line.split():
                if tok.startswith("-Fo"):
                    objdir = tok[3:]
                    break
            if objdir:
                break

    entries = []
    for obj in objects:
        base = os.path.splitext(os.path.basename(obj))[0]
        src = explicit.get(obj)
        if src is None:
            for srcdir, ext in rules:
                cand = os.path.join(srcdir, base + ext)
                if os.path.isfile(os.path.normpath(os.path.join(mfdir, cand))):
                    src = cand
                    break
        if src is None:
            continue
        flags = cflags if src.lower().endswith(".c") else cxxflags
        cmd = "-c %s %s -Fo%s" % (flags, incpath, objdir)
        entries.append(build_entry(mfdir, vars_, cmd, src, objdir))
    return entries


def main():
    entries = {}
    for d in MAKEFILE_DIRS:
        if not os.path.isdir(d):
            continue
        for name in MAKEFILE_NAMES:
            mf = os.path.join(d, name)
            if not os.path.isfile(mf):
                continue
            for e in process_makefile(mf):
                key = e["file"].lower()
                if key not in entries:
                    entries[key] = e
            print("[INFO] processed %s" % os.path.relpath(mf, ROOT))

    out_path = os.path.join(ROOT, "compile_commands.json")
    data = list(entries.values())
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=1)
    print("[OK] wrote %s (%d entries)" % (out_path, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
