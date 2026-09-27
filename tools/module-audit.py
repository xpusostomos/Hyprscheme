#!/usr/bin/env python3
"""Check the module layout's one structural rule.

    tools/module-audit.py           # exits 1 on a violation

The machinery is modules, and the split is only worth having if it stays true:

  - a FAMILY module imports (hyprscheme kernel) and (hyprscheme core) and
    nothing else of ours — it never reaches into another family;
  - (hyprscheme core) is shared plumbing: it imports the kernel only;
  - (hyprscheme extras) is the one module allowed to compose the raw API, so it
    may import families;
  - (hyprscheme) is the public umbrella: it imports everything and re-exports;
  - nothing calls a name a FAMILY owns from outside a family, except extras;
  - every top-level define in a module file is in that module's own `#:export`,
    and every public name the families and core export reaches the umbrella's
    `#:re-export`.

It also checks the things that have bitten us: an `hl--c-*` referenced but never
registered (bind-audit.py owns that), and a family that calls another family's
function — which is what this tool is for, because that is the rule that decays
silently.

The export check earns its place the same way. A module only reaches a later
importer through its export list (`module-export-all!` does NOT, which is why
every module has an explicit list), so a definition that is missing from that
list is invisible to the whole API with no error anywhere — it just reads as an
unbound variable at the call site, or silently does nothing. The public surface
is deliberately smaller than the exports: `hl--*` is internal, `*-rtd` is a
record type the C++ side resolves by name, and `make-hl-*` is a record
constructor — none of those are API, so none are re-exported.
"""

import glob
import os
import re
import sys

DIR = 'src/config/scheme/hyprscheme'
KERNEL = 'kernel'
CORE = 'core'
EXTRAS = 'extras'
UMBRELLA = 'hyprscheme'
# the umbrella is not in DIR: it is the file the directory is named after
UMBRELLA_PATH = 'src/config/scheme/hyprscheme.scm'
# modules allowed to import beyond the kernel + core
FREE = {KERNEL, CORE, EXTRAS}


def read(path):
    return open(path).read()


def strip_prose(text):
    # a name in a docstring or a comment is not a call
    text = re.sub(r';.*$', '', text, flags=re.M)
    return re.sub(r'"(?:[^"\\]|\\.)*"', '""', text)


def imports_of(text):
    return set(re.findall(r'\(use-modules \(hyprscheme ([\w-]+)\)', text)) | \
           set(re.findall(r'#:use-module \(hyprscheme ([\w-]+)\)', text))


def defines_of(text):
    return set(re.findall(r'^\(define\*? \(?([\w!?*<>=+-]+)', text, re.M))


def exports_of(text, key='#:export'):
    m = re.search(re.escape(key) + r' \((.*?)\)\)', text, re.S)
    return set(m.group(1).split()) if m else set()


def is_public(name):
    return not name.startswith('hl--') and not name.endswith('-rtd') \
        and not name.startswith('make-hl-')


def main():
    files = sorted(glob.glob(os.path.join(DIR, '*.scm')))
    fams = {os.path.basename(f)[:-4]: f for f in files}
    owner = {}
    for name, path in fams.items():
        for d in defines_of(read(path)):
            owner[d] = name

    bad = 0
    for name, path in sorted(fams.items()):
        text = read(path)
        if name not in FREE:
            extra = imports_of(text) - {KERNEL, CORE}
            if extra:
                print('IMPORT:  (hyprscheme %s) imports %s — a family imports only '
                      'the kernel and core' % (name, ', '.join(sorted(extra))))
                bad = 1
        # and does it CALL another family's function?
        code = strip_prose(text)
        for callee in set(re.findall(r'\(([\w!?*<>=+-]+)', code)):
            src = owner.get(callee)
            if src and src not in (name, KERNEL, CORE) and name != EXTRAS:
                print('CALL:    (hyprscheme %s) calls %s, which belongs to '
                      '(hyprscheme %s)' % (name, callee, src))
                bad = 1
        # is every definition reachable?
        if name != EXTRAS:
            for d in sorted(defines_of(text) - exports_of(text)):
                print('EXPORT:  (hyprscheme %s) defines %s but does not export it'
                      % (name, d))
                bad = 1

    # The public surface: everything the families and core export that is API
    # must be re-exported by the umbrella, or the API simply cannot be called.
    # The kernel is not part of that surface — it is the gsubr boundary, and
    # the generation imports it DIRECTLY (Host.cpp buildGeneration), which is
    # how a config's (load "x.scm") reaches the kernel's generation-targeting
    # shadow of load/eval. Re-exporting those through the umbrella would be
    # wrong, not merely unnecessary.
    umbrella = read(UMBRELLA_PATH)
    public = set()
    for name, path in fams.items():
        if name not in (KERNEL, EXTRAS):
            public |= {n for n in exports_of(read(path)) if is_public(n)}
    for n in sorted(public - exports_of(umbrella, '#:re-export')):
        print('PUBLIC:  %s is exported but does not reach (hyprscheme)' % n)
        bad = 1

    print('%s: %d modules — %d families, plus kernel, core and extras'
          % ('FAIL' if bad else 'ok', len(fams), len(fams) - len(FREE)))
    return bad


if __name__ == '__main__':
    sys.exit(main())
