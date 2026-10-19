#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: CC0-1.0
"""Prints text files as GitHub notice annotations (readable through the API
without downloading logs). Usage: annotate.py TITLE FILE [FILE...]"""
import sys

title = sys.argv[1]
lines = []
for path in sys.argv[2:]:
    try:
        lines += open(path, encoding="utf8", errors="replace").read().splitlines()
    except OSError:
        pass
chunk, n = [], 1
def flush():
    global chunk, n
    if chunk:
        msg = "%0A".join(l.replace("%", "%25").replace("\r", "") for l in chunk)
        print(f"::notice title={title} ({n})::{msg}")
        n += 1
        chunk = []
for l in lines:
    chunk.append(l)
    if len(chunk) == 45:
        flush()
flush()
