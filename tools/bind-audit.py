#!/usr/bin/env python3
"""Audit the C entry points the machinery registers against what Scheme calls.

    tools/bind-audit.py            # exits 1 on a problem

Registrations are spread across one translation unit per object family
(§4.7 Step G), so the invariant they have to hold jointly is no longer
visible in any single file:

  - every `hl--c-*` name the .scm machinery calls is registered somewhere;
  - no name is registered twice (the second registration silently wins);
  - nothing is registered that Scheme never calls (dead C entry points —
    this is how hl--c-window-float and hl--c-group-lock-active were found).

Run it after any change to the registration list or the bootstrap.
"""

import glob
import re
import subprocess
import sys

# the machinery as modules: (hyprscheme) plus the kernel and the API
SCM_FILES = ['src/config/scheme/hyprscheme.scm'] + sorted(glob.glob('src/config/scheme/hyprscheme/*.scm'))
CPP_FILES = sorted(glob.glob('src/config/scheme/*.cpp'))
PLUGIN = 'scheme-plugin-guile.so'

REG = re.compile(r'hl::bind<([\w:]+)>\("([^"]+)"\)')


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
    print('%s: %d registered, %d called, %d unbound, %d duplicates, %d undefined in the .so'
          % ('FAIL' if (unbound or duplicates or dangling) else 'ok',
             len(registered), len(called), len(unbound), len(duplicates), len(dangling)))
    return 1 if (unbound or duplicates or dangling) else 0


if __name__ == '__main__':
    sys.exit(main())
