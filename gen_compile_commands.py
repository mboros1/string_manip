#!python

import os
import json

template = {
    "directory": "/Users/martinboros/git/string_manip",
    "arguments": [
        "gcc-14", 
        "-O3", 
        "-I" + os.popen("brew --prefix simde").read().strip() + "/include", 
    ],
    "file": "{{file}}"
}

compile_commands = []

for file in os.listdir("."):
    if file.endswith(".c"):
        compile_command = json.dumps(template).replace('{{file}}', file)
        compile_commands.append(json.loads(compile_command))

with open("compile_commands.json", "w") as f:
    f.write(json.dumps(compile_commands, indent=2))

