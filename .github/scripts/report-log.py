#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: CC0-1.0
"""
Surface the end of build logs as GitHub annotations when a job fails.

Annotations show up on the run page and are readable through the public
checks API, which is handy when raw logs cannot be downloaded.

    python3 .github/scripts/report-log.py build.log [more.log ...]
"""
import re
import sys
from pathlib import Path

LINES = 40           # lines from the end of each log
CHUNK = 40           # lines per annotation
# GitHub keeps only 10 error annotations per step: pass the most relevant log first.
MAX_ANNOTATIONS = 10
ERROR_RE = re.compile(r"(error[:\s]|FAILED|CMake Error|fatal|Traceback|Exception|undefined reference)", re.I)


def escape(text: str) -> str:
    return text.replace("%", "%25").replace("\r", "").replace("\n", "%0A")


def main(paths):
    emitted = 0
    for p in paths:
        if emitted >= MAX_ANNOTATIONS:
            break
        path = Path(p)
        if not path.is_file():
            continue
        lines = path.read_text(errors="replace").splitlines()
        # First error-looking lines anywhere in the log: usually the real cause.
        first_errors = [l for l in lines if ERROR_RE.search(l)][:20]
        if first_errors:
            print(f"::error title={path.name}: first errors::{escape(chr(10).join(first_errors))}")
            emitted += 1
        tail = lines[-LINES:]
        for i in range(0, len(tail), CHUNK):
            part = tail[i:i + CHUNK]
            print(f"::error title={path.name}: tail {i + 1}-{i + len(part)}::{escape(chr(10).join(part))}")
            emitted += 1


if __name__ == "__main__":
    main(sys.argv[1:])
