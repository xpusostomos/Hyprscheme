#!/usr/bin/env python3
"""Audit the C entry points the machinery registers against what Scheme calls,
and the runtime's own helper declarations against their callers.

    tools/bind-audit.py            # exits 1 on a problem

Registrations are spread across one translation unit per object family
(§4.7 Step G), so the invariant they have to hold jointly is no longer
visible in any single file:

  - every `hl--c-*` name the .scm machinery calls is registered somewhere;
  - no name is registered twice (the second registration silently wins);
  - nothing is registered that Scheme never calls (dead C entry points —
    this is how hl--c-window-float and hl--c-group-lock-active were found).

It also reports a declared-but-never-called helper in Guile.hpp, which is
the same failure one level up: `hl::useModule` and four others were left
behind when the module list moved into init.scm, and NOTHING could see them
— this tool only looked at `hl--c-*`, the compiler only warns about unused
STATIC functions, and a non-static definition in a translation unit is
linked happily whether or not anyone calls it. Five dead helpers is enough
to say the class is real.

Run it after any change to the registration list, the bootstrap, or the
runtime's declared surface.
"""

import glob
import re
import subprocess
import sys

# the machinery as modules: the kernel and the API. There is no umbrella file —
# the public set is derived by introspection, not read from a list.
SCM_FILES = sorted(glob.glob('src/config/scheme/hyprscheme/*.scm'))
CPP_FILES = sorted(glob.glob('src/config/scheme/*.cpp'))
HPP_FILES = sorted(glob.glob('src/config/scheme/*.hpp'))
PLUGIN = 'scheme-plugin-guile.so'

REG = re.compile(r'hl::bind<([\w:]+)>\("([^"]+)"\)')

# The runtime header, and the shape of a free-function declaration in it: four
# spaces of indent, a return type, a name, a parameter list, a semicolon — and
# no body. `virtual`/`explicit`/`inline`/`static`/`template` are excluded
# because those are members, constructors, or definitions with other rules.
RUNTIME_HPP = 'src/config/scheme/Guile.hpp'
DECL = re.compile(r'^    (?!virtual|explicit|inline|static|template)'
                  r'([A-Za-z_][\w:<>,\*& ]*?)\s+([a-zA-Z_]\w*)\s*\([^;{]*\)\s*;\s*$', re.M)


def uncommented(text):
    """Drop comments, so a name in prose is not read as a call site."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.S)
    return re.sub(r'//.*$', '', text, flags=re.M)


def dead_declarations():
    """Declarations in Guile.hpp that nothing outside it names.

    The test is an `hl::`-QUALIFIED mention anywhere else. That is exact rather
    than approximate: every definition sits inside `namespace hl {`, so it is
    written unqualified, and every call from another file must qualify it. A
    bare-name search would instead match same-named members and locals all over
    the tree (there is an `init`, a `lock`, a `clear` …), which is how a check
    like this earns false positives and gets switched off.
    """
    files = CPP_FILES + HPP_FILES
    texts = {p: uncommented(open(p).read()) for p in files}
    header = texts[RUNTIME_HPP]
    out = []
    for _type, name in DECL.findall(header):
        pat = r'hl::%s\b' % re.escape(name)
        if not any(re.search(pat, texts[p]) for p in files if p != RUNTIME_HPP):
            out.append(name)
    return sorted(out)


def main():
    registered = {}                      # scheme name -> (function, file)
    duplicates = []
    for path in CPP_FILES:
        for fn, name in REG.findall(open(path).read()):
            if name in registered:
                duplicates.append((name, registered[name][1], path))
            registered[name] = (fn, path)

    # a name is "called" if the machinery mentions it OR the host does: the
    # after-gc hook is installed from C++ with the finalizer gsubr as its
    # thunk, so that gsubr appears in no .scm file at all
    called = set()
    for path in SCM_FILES + CPP_FILES:
        text = open(path).read()
        # Prose must not count: a comment saying "hl--c-*" is not a call site.
        # Nor may a DECLARATION: the kernel's export list is generated from the
        # registrations themselves, so counting it would mark every entry point
        # "called" and the dead-code report would go silent.
        if path.endswith('.scm'):
            text = re.sub(r';.*$', '', text, flags=re.M)
            text = re.sub(r'#:re-export \([^()]*\)', '', text)
            text = re.sub(r'#:export \([^()]*\)', '', text)
        # a registration must not count as a use of itself
        text = REG.sub('', text)
        called |= set(re.findall(r'\bhl--c-[\w?!<>=*+-]+', text))
        called |= set(re.findall(r'"(hl--c-[\w?!<>=*+-]+)"', text))
    called = {n.rstrip('.') for n in called}

    unbound = sorted(n for n in called if n not in registered)
    dead = sorted(n for n in registered if n not in called)

    # A family's `registerX()` is called from attachInterp, so a family file
    # left out of the Makefile's object list still LINKS — the .so builds, and
    # the failure appears only when the compositor loads the plugin
    # ("undefined symbol: _ZN6Config6Scheme13registerEventEv"). Ask the binary.
    dangling = []
    try:
        nm = subprocess.run(['nm', '-D', '--undefined-only', PLUGIN],
                            capture_output=True, text=True, check=True).stdout
        dangling = sorted({l.split()[-1] for l in nm.splitlines()
                           if 'Config6Scheme' in l})
    except (OSError, subprocess.CalledProcessError):
        print('note: %s not built or nm unavailable — skipped the link check' % PLUGIN)

    # helpers declared in the runtime header that nothing calls any more
    dead_hl = dead_declarations()

    print('registered: %d across %d translation units' % (len(registered), len(CPP_FILES)))
    print('called by the machinery: %d' % len(called))
    for sym in dangling:
        print('UNDEFINED: %s — a family object is missing from COMMON_OBJS' % sym)
    for name, a, b in duplicates:
        print('DUPLICATE: %s in %s and %s' % (name, a, b))
    for name in unbound:
        print('UNBOUND:   %s is called by Scheme but nobody registers it' % name)
    for name in dead:
        print('dead:      %s (%s) is registered but never called' % (name, registered[name][0]))
    for name in dead_hl:
        print('dead decl: hl::%s (%s) is declared and defined but never called'
              % (name, RUNTIME_HPP))
    print('%s: %d registered, %d called, %d unbound, %d duplicates, %d undefined in the .so, '
          '%d dead helper declarations'
          % ('FAIL' if (unbound or duplicates or dangling or dead_hl) else 'ok',
             len(registered), len(called), len(unbound), len(duplicates), len(dangling),
             len(dead_hl)))
    return 1 if (unbound or duplicates or dangling or dead_hl) else 0


if __name__ == '__main__':
    sys.exit(main())
