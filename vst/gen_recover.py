#!/usr/bin/env python3
"""Regenerates src/recover_sh.h from src/recover.sh (the cut-off apply recovery, run at start and by apply.sh)."""
import os
H = os.path.dirname(os.path.abspath(__file__))
sh = open(os.path.join(H, "..", "src", "recover.sh")).read()
body = "\n".join('    "%s\\n"' % l.replace("\\", "\\\\").replace('"', '\\"').replace("??", "?\\?") for l in sh.rstrip("\n").split("\n"))
open(os.path.join(H, "..", "src", "recover_sh.h"), "w").write(
    "/* src/recover.sh (finish or undo a cut-off apply), run at start and by apply.sh; regenerate with vst/gen_recover.py */\n"
    "static const char RECOVER_SH[] =\n%s;\n" % body)
