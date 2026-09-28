#!/usr/bin/env python3

# Writes compile_commands.json for clangd, without needing bear.

import os
import json

root = os.path.dirname(os.path.abspath(__file__))

# Only the standalone experiments still use simde; the library doesn't.
SIMDE_FILES = {"str_len_test.c", "str_split_test.c", "ffs_find_index_example.c"}
simde_include = os.popen("brew --prefix simde").read().strip() + "/include"

compile_commands = []

for file in sorted(os.listdir(root)):
    if file.endswith(".c"):
        arguments = ["cc", "-O3", "-I."]
        if file in SIMDE_FILES:
            arguments.append("-I" + simde_include)
        compile_commands.append({
            "directory": root,
            "arguments": arguments + ["-c", file],
            "file": file,
        })

with open(os.path.join(root, "compile_commands.json"), "w") as f:
    f.write(json.dumps(compile_commands, indent=2))
