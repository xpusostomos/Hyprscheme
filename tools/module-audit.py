#!/usr/bin/env python3
"""Check the module layout's one structural rule.

    tools/module-audit.py           # exits 1 on a violation

The machinery is modules, and the split is only worth having if it stays true:

  - a FAMILY module imports only the SHARED set — (hyprscheme kernel),
    (hyprscheme core) and (hyprscheme enums) — and nothing else of ours: it
    never reaches into another family;
  - (hyprscheme core) and (hyprscheme enums) are LEAF modules: each imports the
    kernel only, and holds no compositor knowledge of its own. That is what
    separates them from a family, and why every family may import them;
  - (hyprscheme extras) and (hyprscheme emacs-keymaps) are the modules allowed
    to compose the raw API, so they may import families;
  - nothing calls a name a FAMILY owns from outside a family, except extras;
  - every top-level define in a module file is in that module's own `#:export`;
  - every module is named in init.scm, the one file the host loads.

There is no umbrella module any more. `(hyprscheme)` used to be a curated
`#:re-export` list and this tool checked that every public name reached it; the
public set is DERIVED now — "what a module #:exports, minus the internals" — and
tests/t-coverage.sh derives it by INTROSPECTION, which is the only source that
cannot disagree with the running code.

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
ENUMS = 'enums'
EXTRAS = 'extras'
EMACS_KEYMAPS = 'emacs-keymaps'
# the ENTRY POINT: the one file the host loads, which loads everything else by
# name. Every module must be named here or it is never loaded at all.
ENTRY_PATH = 'src/config/scheme/init.scm'
# the LEAF modules every family may import: the kernel (the C++ boundary), core
# (shared plumbing) and enums (named sets of values). They hold no compositor
# knowledge of their own, which is what separates them from a family.
SHARED = {KERNEL, CORE, ENUMS}
# the modules that are NOT families — the count in the summary, and the ones
# exempt from the family-only rules below
FREE = {KERNEL, CORE, ENUMS, EXTRAS, EMACS_KEYMAPS}


def read(path):
    return open(path).read()


def strip_prose(text):
    # a name in a docstring or a comment is not a call.
    #
    # STRINGS FIRST, then comments — the order matters. Stripping `;` first
    # truncates any line with a semicolon INSIDE a string ("...; #:prev #t
    # swaps..."), leaving an unbalanced quote; the string regex then mis-pairs
    # and prose that was inside a docstring survives as if it were code. That
    # produced a false "calls hl-key" on (hyprscheme window), whose docstrings
    # are full of semicolons.
    #
    # re.S matters: `\\.` must be able to consume a backslash-NEWLINE, which is
    # how a long Scheme string is continued across lines. Without it the match
    # fails at that backslash, stripping desyncs from there on, and every later
    # docstring in the file survives as if it were code.
    text = re.sub(r'"(?:[^"\\]|\\.)*"', '""', text, flags=re.S)
    return re.sub(r';.*$', '', text, flags=re.M)


def imports_of(text):
    return set(re.findall(r'\(use-modules \(hyprscheme ([\w-]+)\)', text)) | \
           set(re.findall(r'#:use-module \(hyprscheme ([\w-]+)\)', text))


def calls_of(code):
    """Names in call position. A quoted list is data, not a call, so anything
    behind a quote character is dropped — `'(hl-kbd hl-key)` names them, it does
    not call them."""
    return set(re.findall(r"(?<!['`,])\(([\w!?*<>=+-]+)", code))


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

    all_names = set(owner) | {n for path in fams.values()
                              for n in exports_of(read(path))}

    def visible_to(name):
        """What a module may call: its own bindings (defines and its #:export,
        since the host defines some of the kernel's), plus what its imports
        offer. The kernel imports nothing of ours — that is what makes the rule
        a rule."""
        own = defines_of(read(fams[name])) | exports_of(read(fams[name]))
        if name == KERNEL:
            return own
        if name == CORE:
            return own | exports_of(read(fams[KERNEL]))
        if name == ENUMS:
            return own | exports_of(read(fams[KERNEL]))
        if name in (EXTRAS, EMACS_KEYMAPS):
            return own | {n for p in fams.values() for n in exports_of(read(p))}
        return own | exports_of(read(fams[KERNEL])) | exports_of(read(fams[CORE])) \
                   | exports_of(read(fams[ENUMS]))

    bad = 0
    for name, path in sorted(fams.items()):
        text = read(path)
        if name not in FREE:
            extra = imports_of(text) - SHARED
            if extra:
                print('IMPORT:  (hyprscheme %s) imports %s — a family imports only '
                      'the kernel, core and enums' % (name, ', '.join(sorted(extra))))
                bad = 1
        # and does it CALL another family's function?
        code = strip_prose(text)
        for callee in sorted(calls_of(code)):
            src = owner.get(callee)
            if src and src not in ({name} | SHARED) and name not in (EXTRAS, EMACS_KEYMAPS):
                print('CALL:    (hyprscheme %s) calls %s, which belongs to '
                      '(hyprscheme %s)' % (name, callee, src))
                bad = 1
        # is every definition reachable?
        if name not in (EXTRAS, EMACS_KEYMAPS):
            for d in sorted(defines_of(text) - exports_of(text)):
                print('EXPORT:  (hyprscheme %s) defines %s but does not export it'
                      % (name, d))
                bad = 1
        # can it SEE everything it calls? A call to a name the module cannot
        # reach is unbound at runtime, and it is silent until that path runs —
        # `hl--plist-has?` lived in core while the KERNEL called it, so every
        # bind whose callback returned a plist raised, was caught by the guard,
        # and was read as DECLINED (the key was passed through, not consumed).
        for c in sorted(calls_of(code) & set(all_names) - visible_to(name)):
            print('CALL:    (hyprscheme %s) calls %s, which it cannot see'
                  % (name, c))
            bad = 1

    # THE PUBLIC SURFACE IS DERIVED, not read from a list. There used to be an
    # umbrella module `(hyprscheme)` — a curated #:re-export list, and a check
    # here that every public name reached it. Both are gone: the API is now
    # "what each module #:exports, minus the internals", which is what
    # `is_public` above encodes, and what a config actually sees because
    # init.scm imports those modules directly.
    #
    # The check that replaces it lives in tests/t-coverage.sh, which derives the
    # same set BY INTROSPECTION (module-public-interface over the loaded
    # modules) and demands a test for each name. Introspection is the better
    # source: the old list had already drifted, listing hl-bind-thunk and
    # hl-bind-tokens, two internal record accessors no config should see.
    #
    # What is worth keeping here is the shape of the set, since a name that is
    # public by accident is invisible otherwise: a family export starting with
    # `hl-` and not matching the internal patterns IS the API.
    public = set()
    for name, path in fams.items():
        if name not in (KERNEL, EXTRAS):
            public |= {n for n in exports_of(read(path)) if is_public(n)}
    if len(public) < 200:
        print('PUBLIC:  only %d public names derived — the derivation is broken, '
              'not the API' % len(public))
        bad = 1

    # EVERY module must be named in init.scm. That file is the only thing the
    # host loads, and it reaches the rest by name, so a module missing from it
    # is simply never loaded — with no error anywhere: its `hl-*` names are
    # unbound at the call site, and load-check cannot see it, because not
    # loading something is not a load failure. (It is the module-tree analogue
    # of the family-import rule this tool exists for: silent decay.)
    entry = read(ENTRY_PATH)
    # `(hyprscheme x)` names module x. There is no bare `(hyprscheme)` to check
    # for any more — the umbrella is gone, so init.scm's list of `(hyprscheme X)`
    # names IS the whole set of what loads.
    named = {m.group(1) for m in re.finditer(r"\(hyprscheme ([a-z-]+)\)", entry)}
    for name in sorted(fams):
        if name not in named:
            print('ENTRY:   %s is never loaded — init.scm does not name it' % name)
            bad = 1

    print('%s: %d modules — %d families, plus %s'
          % ('FAIL' if bad else 'ok', len(fams), len(fams) - len(FREE),
             ', '.join(sorted(FREE))))
    return bad


if __name__ == '__main__':
    sys.exit(main())
