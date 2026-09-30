#!/usr/bin/env python3

# Writes compile_commands.json for clangd, without needing bear.

import os
import json

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# Only the standalone experiments still use simde; the library doesn't.
SIMDE_FILES = {"str_len_test.c", "str_split_test.c", "ffs_find_index_example.c"}
simde_include = os.popen("brew --prefix simde").read().strip() + "/include"

# The library needs no include paths; everything else includes it via src/
DIRS = ["src/core", "src/mem", "src/text", "src/batch", "src/kernels",
        "tests", "bench", "tools", "experiments"]

compile_commands = []

for d in DIRS:
    for file in sorted(os.listdir(os.path.join(root, d))):
        if file.endswith(".c"):
            path = os.path.join(d, file)
            arguments = ["cc", "-O3", "-Isrc"]
            if file in SIMDE_FILES:
                arguments.append("-I" + simde_include)
            compile_commands.append({
                "directory": root,
                "arguments": arguments + ["-c", path],
                "file": path,
            })

with open(os.path.join(root, "compile_commands.json"), "w") as f:
    f.write(json.dumps(compile_commands, indent=2))
