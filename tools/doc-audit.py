#!/usr/bin/env python3
"""Check that every `hl-*` name the wiki USES actually exists.

    tools/doc-audit.py        # exits 1 on a name that is not defined

The doc test (tests/t-zzz-docs.sh) evaluates every ```scheme block in the
wiki, so a block that names a function which no longer exists SHOULD be
caught there. It is not, and the reason is worth stating: evaluating

    (hl-bind-add! (hl-kbd "s-G") (lambda () ... (hl-window-into-group #f "l") ...))

cannot fail. The lambda is registered and never called, so nothing inside it
is ever looked up — and a rename that misses the docs therefore passes the
whole suite while every copy-paste user hits `Unbound variable`. Those names
sat in code-snippets.md for exactly that reason.

So this tool checks the code TEXTUALLY, which is the one thing evaluation
cannot do for a thunk: strip the strings and comments (the doc test's own
balance walk already ignores them), pull out every `hl-*` token, and require
it to be visible in the generation a config runs in — the public umbrella's
re-exports plus the exports of the families, core and the kernel.

It complements the two other checks rather than replacing them:
tests/wiki-examples.awk catches unbalanced code, t-zzz-docs catches a block
that raises when evaluated, and this catches a name that would only ever be
looked up when the callback RUNS.

Scope, honestly: it proves a name exists, not that calling it does anything
sensible — arity, keyword and behaviour are the suite's business. It checks
```scheme blocks only; a stale name in prose is not visible to it."""

import os
import re
import shutil
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WIKI = os.path.normpath(os.path.join(REPO, '..', 'Hyprscheme.wiki'))
AWK = os.path.join(REPO, 'tests', 'wiki-examples.awk')
UMBRELLA = os.path.join(REPO, 'src/config/scheme/hyprscheme.scm')
MODULES = os.path.join(REPO, 'src/config/scheme/hyprscheme')

# a maximal run of name characters, so hl-group is NOT pulled out of
# hl-group-cycle!: `-` is in the class, and the lookbehind stops a match
# starting mid-name
NAME = re.compile(r'(?<![A-Za-z0-9!?*<>=+-])hl-[A-Za-z0-9!?*<>=+-]*')

EXPORT = re.compile(r'#:(?:re-)?export\s*\((.*?)\)\)', re.S)

# Mentions that are not uses. Each needs a REASON, because the whole point of
# this list is that it stays short enough to read: an entry is a place where the
# wiki is right to name something that is not API.
ALLOWED = {
    'hl-virtual-keyboard-fcitx5':
        'a DEVICE name the compositor generates from the process path '
        '(/usr/bin/fcitx5 -> hl-virtual-keyboard-fcitx5), not a function',
    'hl-workspace-info':
        'named deliberately, as one of the functions it was replaced by',
    'hl-monitor-info':
        'named deliberately, as one of the functions it was replaced by',
}


def visible_names():
    """Every hl-*/hl--* name a config can see: the umbrella's re-exports plus
    every module's own #:export (the generation imports the families and the
    kernel directly, so their exports count)."""
    names = set()
    for path in [UMBRELLA] + [os.path.join(MODULES, f)
                              for f in os.listdir(MODULES) if f.endswith('.scm')]:
        for body in EXPORT.findall(open(path).read()):
            names |= set(body.split())
    return {n for n in names if n.startswith('hl-')}


def strip_code(text):
    """Drop what the reader drops: strings, ; comments, #| |# blocks and #\\
    character literals. Without this a name in a comment reads as a use."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == ';':
            while i < n and text[i] != '\n':
                i += 1
        elif text.startswith('#|', i):
            depth = 1
            i += 2
            while i < n and depth:
                if text.startswith('#|', i):
                    depth += 1
                    i += 2
                elif text.startswith('|#', i):
                    depth -= 1
                    i += 2
                else:
                    i += 1
        elif text.startswith('#\\', i):
            i += 2
            while i < n and (text[i].isalnum()):
                i += 1
        elif c == '"':
            i += 1
            while i < n:
                if text[i] == '\\':
                    i += 2
                elif text[i] == '"':
                    i += 1
                    break
                else:
                    i += 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def blocks():
    """The same extraction the doc test uses, so there is one definition of
    what a block is. Yields (path, first-line, text)."""
    tmp = tempfile.mkdtemp(prefix='doc-audit.')
    try:
        subprocess.run(['awk', '-v', 'out=' + tmp, '-v',
                        'manifest=' + os.path.join(tmp, 'MANIFEST'),
                        '-f', AWK] + sorted(
                            os.path.join(WIKI, f) for f in os.listdir(WIKI)
                            if f.endswith('.md')),
                       check=True)
        for line in open(os.path.join(tmp, 'MANIFEST')):
            num, path, first, _bal = line.split()
            yield path, int(first), open(os.path.join(tmp, num + '.scheme')).read()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def prose_names(path):
    """hl-* names mentioned in the PROSE, with line numbers. A name in a code
    block is a use; a name in prose is a promise to the reader, and it rots the
    same way (bind-globals.md's `hl-global` did)."""
    text = open(path).read()
    # blank the blocks rather than deleting them: a reported line number has to
    # be the line in the FILE, or the person reading it looks in the wrong place
    text = re.sub(r'^```.*?^```', lambda m: '\n' * m.group(0).count('\n'),
                  text, flags=re.S | re.M)
    for i, line in enumerate(text.split('\n'), 1):
        for m in NAME.finditer(line):
            if m.group(0) != 'hl-':
                yield i, m.group(0)


def main():
    if not os.path.isdir(WIKI):
        print('doc-audit: %s not found — skipped' % WIKI)
        return 0

    known = visible_names()
    if len(known) < 200:                      # the same floor t-coverage uses
        print('doc-audit: only %d names extracted — the extraction is broken, '
              'not the API' % len(known))
        return 1

    # (file, line, name) -> how many times, so one bad name in one block is one
    # line of output however often it is used (the rename that motivates this
    # tool usually misses every call site in a snippet, not one)
    uses, checked = {}, 0
    for path, first, text in blocks():
        for m in NAME.finditer(strip_code(text)):
            name = m.group(0)
            if name == 'hl-':
                continue
            checked += 1
            if name not in known:
                key = (os.path.basename(path), first, name)
                uses[key] = uses.get(key, 0) + 1

    bad = 0
    for (f, line, name), count in sorted(uses.items()):
        print('block: %s:%d: %s is not defined%s'
              % (f, line, name, ' (x%d)' % count if count > 1 else ''))
        bad = 1

    # Prose mentions: the same rule, but the prose ways of naming the API are
    # NOT rot, and two of them are common — a wildcard (`hl-*`,
    # `hl-window-*`, `hl-make-*-gesture`) and a FAMILY referred to by prefix
    # (`hl-event`, `hl-window-rule`, `hl-config`). Both are skipped: a strict
    # prefix of a name that exists is a family mention, and a real rename
    # (hl-window-into-group) is a prefix of nothing, so it is still caught.
    prose = 0
    for f in sorted(os.listdir(WIKI)):
        if not f.endswith('.md'):
            continue
        for line, name in prose_names(os.path.join(WIKI, f)):
            if name in known or name in ALLOWED:
                prose += 1
            elif '*' in name or any(k.startswith(name) for k in known):
                prose += 1
            else:
                print('prose: %s:%d: %s is not defined' % (f, line, name))
                bad = 1

    print('%s: %d hl-* uses in blocks, %d in prose, %d names of API'
          % ('FAIL' if bad else 'ok', checked, prose, len(known)))
    return bad


if __name__ == '__main__':
    sys.exit(main())
