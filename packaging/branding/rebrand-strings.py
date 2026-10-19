#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Replaces the "Krita" product name with "IKriate" in user-visible text.

Only translatable text is touched: i18n()/tr() calls in C++, Python and QML,
<string> elements in Qt Designer files, labels in .action/.rc files, and the
matching entries of the translation catalogues in po/. Because the same
rewrite is applied to the sources and to the catalogues, translated strings
keep matching their msgids.

Kept as they are, since they are accurate references rather than branding:
  * "Krita Foundation" (copyright holders, credits)
  * "Krita 4", "Krita 5.2", ... (file-format compatibility notes)
  * "based on Krita"
Identifiers ("KritaShape/KisToolBrush", "kritarc", MIME types, URLs) never
match, because only the capitalised stand-alone word is replaced.

Usage: rebrand-strings.py [--check] [repository root]
  --check   only report how many strings would change
"""

import os
import re
import sys

NEW = "IKriate"
WORD = re.compile(r"(?<![A-Za-z0-9_/\-.:@])Krita(?![A-Za-z0-9_\-])")
KEEP_AFTER = re.compile(r"\s+(Foundation\b|[0-9])")
KEEP_BEFORE = re.compile(r"based on\s+$", re.IGNORECASE)


def rebrand_text(text):
    out = []
    pos = 0
    for m in WORD.finditer(text):
        if KEEP_AFTER.match(text, m.end()) or KEEP_BEFORE.search(text[max(0, m.start() - 12):m.start()]):
            continue
        out.append(text[pos:m.start()])
        out.append(NEW)
        pos = m.end()
    out.append(text[pos:])
    return "".join(out)


# ----------------------------------------------------------------- C++ / QML

CPP_CALLS = re.compile(
    r"\b(?:i18n|i18nc|i18np|i18ncp|ki18n|ki18nc|ki18np|ki18ncp|kli18n|kli18nc|kli18np|kli18ncp|"
    r"xi18n|xi18nc|xi18np|xi18ncp|kxi18n|kxi18nc|I18N_NOOP|I18N_NOOP2|I18N_NOOP2_NOSTRIP|"
    r"tr|qsTr|QT_TR_NOOP|QT_TRANSLATE_NOOP|QT_TRANSLATE_NOOP3)\s*\("
)


def rebrand_call_args(src, start, string_quotes, triple=False, hash_comments=False):
    """Rewrites string literals from @p start up to the call's closing paren.
    Returns (new_text_for_span, end_index)."""
    depth = 1
    i = start
    out = []
    n = len(src)
    while i < n and depth > 0:
        c = src[i]
        if c in string_quotes:
            q = c
            if triple and src.startswith(q * 3, i):
                end = src.find(q * 3, i + 3)
                if end < 0:
                    end = n - 3
                out.append(q * 3 + rebrand_text(src[i + 3:end]) + q * 3)
                i = end + 3
                continue
            j = i + 1
            while j < n and src[j] != q:
                if src[j] == "\\":
                    j += 1
                elif src[j] == "\n" and q == "'" and not triple:
                    break  # C++ char literal or stray quote
                j += 1
            out.append(q + rebrand_text(src[i + 1:j]) + (q if j < n else ""))
            i = j + 1
            continue
        if not hash_comments and src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(src[i:j])
            i = j
            continue
        if not hash_comments and src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(src[i:j])
            i = j
            continue
        if hash_comments and c == "#":
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(src[i:j])
            i = j
            continue
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        out.append(c)
        i += 1
    return "".join(out), i


def rebrand_source(src, string_quotes, triple=False, hash_comments=False):
    out = []
    pos = 0
    for m in CPP_CALLS.finditer(src):
        if m.start() < pos:
            continue  # nested inside a call already rewritten
        out.append(src[pos:m.end()])
        span, end = rebrand_call_args(src, m.end(), string_quotes, triple, hash_comments)
        out.append(span)
        pos = end
    out.append(src[pos:])
    return "".join(out)


# --------------------------------------------------------------------- XML

UI_STRING = re.compile(r"(<string(?![^>]*notr=\"true\")[^>]*>)(.*?)(</string>)", re.S)
ACTION_TEXT = re.compile(r"(<(text|whatsThis|toolTip|iconText|statusTip)\b[^>]*>)(.*?)(</\2>)", re.S)


def rebrand_ui(src):
    return UI_STRING.sub(lambda m: m.group(1) + rebrand_text(m.group(2)) + m.group(3), src)


def rebrand_action(src):
    return ACTION_TEXT.sub(lambda m: m.group(1) + rebrand_text(m.group(3)) + m.group(4), src)


# ----------------------------------------------------------------------- PO

PO_FIELD = re.compile(r'^(msgctxt|msgid|msgid_plural|msgstr(?:\[\d+\])?)\s+"')


def rebrand_po(src):
    lines = src.split("\n")
    out = []
    in_field = False
    for line in lines:
        if PO_FIELD.match(line):
            in_field = True
        elif not line.startswith('"'):
            in_field = False
        if in_field and '"' in line:
            first = line.index('"')
            last = line.rindex('"')
            if last > first:
                line = line[:first + 1] + rebrand_text(line[first + 1:last]) + line[last:]
        out.append(line)
    return "\n".join(out)


# ------------------------------------------------------------------- driver

SKIP_DIRS = {".git", "3rdparty", "3rdparty_vendor", "build", "packaging", "benchmarks"}


def handler_for(path):
    name = os.path.basename(path)
    ext = os.path.splitext(name)[1].lower()
    if "/tests/" in path.replace(os.sep, "/") or name.endswith("_test.cpp") or name.endswith("Test.cpp"):
        return None
    if ext in (".cpp", ".cc", ".cxx", ".h", ".hpp", ".mm", ".c"):
        return lambda s: rebrand_source(s, '"')
    if ext == ".qml":
        return lambda s: rebrand_source(s, "\"'")
    if ext == ".py":
        return lambda s: rebrand_source(s, "\"'", triple=True, hash_comments=True)
    if ext == ".ui":
        return rebrand_ui
    if ext in (".action", ".rc", ".xmlgui"):
        return rebrand_action
    if ext == ".po":
        return rebrand_po
    return None


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    check = "--check" in sys.argv
    root = os.path.abspath(args[0] if args else os.path.join(os.path.dirname(__file__), "..", ".."))
    changed_files = 0
    changed_words = 0
    for top in ("libs", "plugins", "krita", "po"):
        for dirpath, dirnames, filenames in os.walk(os.path.join(root, top)):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for fn in filenames:
                path = os.path.join(dirpath, fn)
                handler = handler_for(path)
                if not handler:
                    continue
                try:
                    with open(path, encoding="utf-8") as f:
                        src = f.read()
                except (UnicodeDecodeError, OSError):
                    continue
                if "Krita" not in src or "IKRIATE-REBRAND: skip" in src:
                    continue
                new = handler(src)
                if new != src:
                    changed_files += 1
                    changed_words += src.count("Krita") - new.count("Krita")
                    if not check:
                        with open(path, "w", encoding="utf-8") as f:
                            f.write(new)
    verb = "would change" if check else "changed"
    print(f"{verb} {changed_words} occurrences in {changed_files} files")


if __name__ == "__main__":
    main()
