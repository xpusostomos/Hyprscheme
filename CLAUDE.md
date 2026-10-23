# Hyprscheme — notes for working in this repo

Hyprscheme embeds Guile Scheme inside Hyprland as a loadable plugin
(`scheme-plugin-guile.so`). The original Chez backend is DEPRECATED:
frozen in `chez/` (see `chez/README.md`), still buildable there, no
longer developed — don't evolve it. Configs are `hypr/hyprland.scm`, found
by the **same XDG search the compositor uses for its own config**
(`XDG_CONFIG_HOME` base → each `XDG_CONFIG_DIRS` entry → `/etc/xdg`, always
under `hypr/`; `HYPRSCHEME_CONFIG` overrides the whole thing); the
companion Lua config (`hyprland.lua`) loads the plugin. The design
target is parity with upstream Hyprland's Lua scripting API — every
Lua feature has a `hl-*` equivalent.


## Chris's rules (the non-negotiables)

- **Agree a plan before changing anything.** Never start editing code,
  tests or docs without explicit go-ahead. Approval for one task is not
  approval for the next; narrate each pass explicitly (a rename done in
  two sub-steps must be reported as two sub-steps, or it looks like it
  was never done).
- **NEVER touch `~/.config/`.** Test configs go through
  `HYPRSCHEME_CONFIG` env var and/or the compositor's `--config` flag;
  `tests/run.sh` builds its own temp `$XDG_CONFIG_HOME`.
- **DONE.txt is the authoritative done-ledger.** When work is finished
  and verified, move it there. Don't re-propose finished work, and don't
  claim things are done that aren't (this caused a real argument —
  "all you do now days is hallucinate that work needs to be done").
- Tests live in `tests/`, docs in `../Hyprscheme.wiki`; both must be
  updated with any behavior change, in the same pass.
  
-  A piece of work isn't done till you fix most of the warnings. There are a
few warnings that we can't avoid, those are fine, but all the ones we
can fix should be fixed before a piece of work is complete.


## Build environment (in place, no copies/staging)

- @chez is deprecated. Don't work on that code
- `make` builds in place: **Hyprland** (`../Hyprland`, built in
  `<tree>/build`), then the Guile plugin (needs `guile-3.0` dev
  headers). The Chez build lives frozen in `chez/` with its own
  Makefile (and `BUILDCHEZ.md`); its paths point at the SAME
  `../../ChezScheme` / `../../Hyprland` trees.
- The compositor **binary is a make prerequisite** of the plugin — a
  Hyprland rebuild forces a plugin relink automatically (the plugin
  resolves Hyprland symbols at load time).
- `make install` → `$(PREFIX)/lib/hyprscheme/` (**default `PREFIX=/usr/local`,
  so it needs `sudo`**; `PREFIX=$HOME/.local` for a per-user install): the
  `.so`, the machinery `.scm`, the matched compositor as
  `$(PREFIX)/bin/Hyprscheme`, and a session entry in
  `$(PREFIX)/share/wayland-sessions/`, plus the compositor's launcher
  wrapper as `$(PREFIX)/bin/start-hyprscheme`. One target — there is no
  `install-compositor` any more. No boot files — the interpreter is the
  system Guile. `DESTDIR` is a packaging staging prefix and is never baked
  into the binary or the session entry.
- **The session entry launches through `start-hyprscheme`, not the
  compositor directly**: `Exec=uwsm start -g -1 -e -D Hyprscheme --
  start-hyprscheme --path $(PREFIX)/bin/Hyprscheme`. Hyprland started
  without its wrapper logs "WARNING: Hyprland is being launched without
  start-hyprland" and loses the watchdog (`--watchdog-fd`, passed only by
  the wrapper). We ship the wrapper from our own tree under our own name
  so the package neither requires nor collides with a system
  `start-hyprland`. **The `--` is load-bearing**: uwsm passes everything
  after the compositor ID through, and without it uwsm eats `--path` as
  its own argument and refuses to start. Nested runs bypass the wrapper
  and get the warning, which is expected.
- `make install-local` is **config only — it installs no files.** It appends
  a `DESKTOP_SESSION`-guarded `hl.plugin.load(...)` to this user's
  `~/.config/hypr/hyprland.lua`, skipping if the exact path is already there
  and **refusing (exit 1)** if any *other* `hl.plugin.load` is present. It
  cannot be folded into `install`: that runs under sudo where `$HOME` is
  root's, so it would edit `/root/.config`. It must not depend on `$(TARGET)`
  — that would drag in the root-owned Hyprland tree for a plain user.
- **If anything starts crashing weirdly, rebuild all three** (Chez,
  Hyprland, plugin). A mismatched pairing either fails loud (renamed
  symbol) or misbehaves silently (changed class layout). Never run the
  plugin against a Hyprland built from a different tree.
- Packaging: `packaging/PKGBUILD` — Arch package building a pinned
  Hyprland + the Guile plugin (installs via the Makefile's DESTDIR
  rule) and ships the matched compositor as `hyprland-scheme`. The
  frozen Chez variant lives at `chez/packaging/PKGBUILD`.
- **hyprpm** — the second way to build/load the plugin, for users who want
  it alongside other hyprpm plugins instead of a whole matched compositor.
  `hyprpm.toml` is the manifest; `make hyprpm-plugin` and
  `make hyprpm-machinery` are its two build steps. Three things about it
  are non-obvious and each cost real time:
  - **Never run hyprpm under sudo.** It refuses outright
    (`PluginManager.cpp`, `isSuperuser()` → `Debug::die("Don't run hyprpm
    as a superuser.")`) and escalates internally by polkit only to create
    `/var/cache/hyprpm`. Running it as root is not a workaround; it is the
    error.
  - **The build is against hyprpm's headers, not the tree.** hyprpm clones
    Hyprland at the running commit, builds it, and points `PKG_CONFIG_PATH`
    at its own cache, so `hyprpm-plugin` takes its includes from
    `pkg-config --cflags hyprland` — deliberately a *separate target* from
    plain `make`, whose includes come from `HYPRLAND_SRC`. Mixing the two
    include sources is the mismatch this repo exists to avoid, so they must
    never be merged.
  - **hyprpm installs exactly ONE file per plugin** — always `<name>.so`, to
    a root-owned cache (`/var/cache/hyprpm/<user>/<name>/`), and deletes the
    build tree afterwards. So the machinery cannot go beside the `.so`; that
    is what `hyprpm-machinery` (and `schemeDir()`'s third candidate) exist
    for. `hyprpm enable` also does **not** survive a restart: nothing loads
    hyprpm plugins at compositor startup, so `hyprpm reload` has to run at
    session start.
- **`make` runs the lints** (`all: $(TARGET) check`), so they are not
  optional:
  - `tools/syntax-check.scm` — Guile's own reader over every machinery
    `.scm`, reporting read errors with the reader's `file:line:col`. The
    paren/quote/docstring-corruption class, at the edit site rather than as
    a "prelude failed to load" at startup.
  - `tools/module-audit.py` — the module rule: a family imports only the
    kernel and core and calls nothing another family owns; only extras
    composes. Its whole point is that this rule decays silently. It also
    checks that a module can SEE everything it calls: a call to a name it
    cannot reach is unbound at runtime and silent until that path runs.
    (`hl--plist-has?` sat in core while the **kernel** called it — the
    kernel imports nothing of ours — so every bind whose callback returned
    a plist raised, was caught by the guard, and read as DECLINED: the key
    was passed through instead of consumed.)
  - `tools/load-check.scm` — stubs the C entry points and loads the
    machinery in a plain `guile`: a load-time error with no compositor.
  - `tools/compile-lint.scm` — **compiles** the machinery, in dependency
    order, against a generated stub kernel, so `possibly unbound variable`
    becomes a warning. It is the only check that sees **a name used but never
    defined inside a lambda body**: the reader does not resolve names, and
    load-check only finds one if the form actually runs. (FABLE §4.6 Step F.)
    Two things make it work, both non-obvious: the stub kernel is generated
    from the **real kernel's `#:export` list** (369 names — scanning for
    `hl--c-*` alone misses the ~55 real definitions like `hl--report` and
    `hl--plist-get`, and every caller of one then reads as a false positive),
    and a file given on the command line compiles in an environment that
    **imports the API**, the way the host's generation does, or every `hl-*`
    in a user's config reads as unbound. Verified to fail both ways: a typo in
    a lambda body trips it, and removing a name from the kernel's export list
    reports every caller.
  - `tools/bind-audit.py` — every `hl--c-*` the machinery calls is
    registered, none twice, none dead — and nothing is left undefined in
    the built `.so` (how a family object missing from `COMMON_OBJS` shows
    up: it links fine and fails only when the compositor loads it). It also
    reports a **declared-but-never-called helper in `Guile.hpp`**: five had
    accumulated (`useModule`, `resolveModule`, `loadModule`, `exportAll`,
    `call0`) and nothing could see them — this tool only looked at
    `hl--c-*`, the compiler only warns about unused *static* functions, and a
    non-static definition links happily whether or not anyone calls it. The
    test is an `hl::`-QUALIFIED mention elsewhere, which is exact: every
    definition sits inside `namespace hl {` so it is written unqualified,
    while a call from another file must qualify it. A bare-name search would
    match same-named members all over the tree (there is an `init`, a `lock`,
    a `clear` …) and the check would be switched off as noisy.
  - `tools/api-check.sh` — **what the public API has LOST since `API.snapshot`**.
    NOT part of `make check` (it fails on any removal, and a working tree
    legitimately has removals mid-change); run it before a release or when a
    batch of API work is done. The suite only tests the names IT uses, so a
    rename looks perfectly clean from inside the repo while breaking every
    config outside it — that happened three times in one session
    (`hl-layout-msg` → `hl-layout-msg!`, `hl-is-key-down` → `hl-key-down?`, and
    `hl-curve-add!` split in two), each time with all lints green. The snapshot
    is not a list to maintain: it is `tools/api-names.scm`'s introspection
    output, so it is a photograph of the API. Removals fail the check and are
    named; **additions report but pass** — adding a function cannot break
    anybody, and failing on it would make the tool annoying enough to ignore.
    Verified to fail: removing `hl-key-down?`'s export is reported by name,
    exit 1. `--update` rebaselines (a deliberate act, after saying so in
    DONE.txt and the wiki).
  - `tools/doc-audit.py` — every `hl-*` name the WIKI uses exists (in a
    code block or in prose). Textual, which is the point: a name inside a
    `lambda` is never looked up by evaluating the block, which is how five
    nonexistent names survived in `code-snippets.md`.
  - `tools/doc-exercise.scm` — the wiki's blocks are loaded and then what
    they REGISTERED is called, so a name inside a thunk is looked up. Fails
    on `unbound-variable`; other callback errors are notes
    (`DOC_EXERCISE_NOTES=1` shows them) because the stubs answer `#f` where
    the compositor would answer a real value.
  Each has been **verified to fail**: a deliberate cross-family call trips
  module-audit, an undefined call at load trips load-check, an unbound name
  inside a lambda trips compile-lint, a bad name in the wiki trips doc-audit,
  a bad name inside a thunk trips doc-exercise. A check that cannot fail is
  worse than no check — `t-coverage` passed vacuously for its whole life.

  **`load-check.scm` and the `doc-*` tools build the machinery the way
  `Host.cpp` does, and two things about that are easy to get wrong** (both
  were, and both made the check weaker than it read): the C entry points
  must be stubbed into the **kernel** (where `registerAllBindings()` puts
  the real ones) *before* the kernel is read, and a file loaded form-by-form
  needs `set-current-module` after its `define-module` — evaluating that form
  does **not** switch modules the way the loader's own handling does. Get
  either wrong and every module keeps only its unassigned `#:export`
  placeholders: the forms all evaluate, and every name dereferences to
  unbound.

## How the plugin loads Scheme

The machinery is **modules** (no embedded blobs — the `.scm` were
extracted out of the C++ in Sep 2026), installed next to the `.so`, found
via `dladdr` on a known symbol — `HYPRSCHEME_SCM_DIR` overrides it for a
harness testing the tree. **THREE candidates, in a fixed order, and not a
search in the sense that matters** (`schemeDir()`, Host.cpp): each step is
a definite answer rather than a guess between plausible places, and the one
ambiguity worth avoiding is handled by the first candidate being explicit
and refusing to fall through.

1. `HYPRSCHEME_SCM_DIR` — a harness naming the tree it means to test. Named
   and missing is a **broken harness**: refuse loudly rather than quietly
   run something else (the exact failure the override exists to prevent).
2. **Beside the `.so`** — where `make install` puts it. No compiled-in path,
   because `DESTDIR` is a staging prefix copied out at package time and
   would name a build-machine directory; `pluginDir()` is that same
   directory *as installed*, and follows the `.so`.
3. **`~/.local/share/hyprscheme/scm`** (honouring `XDG_DATA_HOME`, per the
   spec: a relative or empty value is invalid and ignored) — the one case
   the other two cannot serve, a **hyprpm** build: hyprpm installs only the
   `.so` (one file per plugin, into a root-owned cache dir) and deletes its
   build tree, so the machinery cannot go beside it. `make hyprpm-machinery`
   puts it here instead. Absent, the candidate is simply skipped.

A chain would let a test that meant to exercise the tree silently pick up an
installed copy, or the reverse. **`init.scm` is the marker** the probe looks
for, because it is the entry point (below).

**ONE FILE IS LOADED: `init.scm`.** The host loads that and nothing else; it
reaches every other module by name with `use-modules`. Before this, the host
enumerated all nineteen files itself and loaded each through
`scm_primitive_load` — a list in C++ to keep in step with the module tree, and
**no compilation at all**, because the primitive bypasses the Scheme-level
loader (so `GUILE_AUTO_COMPILE` never applied to the machinery). Loading by
name puts every module through Guile's own loader, which uses a shipped `.go`
when there is one and compiles and caches one when there is not. `make
scm-compile` builds them (in dependency order — a module fails to compile with
"no code for module" unless its dependency is already a `.go`), and `install`
ships them **beside** the `.scm`.

**The host names NO module — not even in the generation.** `init.scm` is read
with the **generation** current, so its `use-modules` ARE the generation's
environment, and the standard library a config has always had is imported there
too. `buildGeneration` therefore has no module list at all: kernel (created and
given the gsubrs), then the generation made current, then one `evalFile`. The
order is load-bearing — read with the kernel current, as it once was, the
imports land in the *kernel* and the generation has to be assembled by hand,
which is how a 23-line list in C++ came to duplicate `init.scm`'s and drift from
it. **`tests/t-generation.sh` exists for exactly that failure**, because it is
invisible otherwise: Guile resolves names through a used module's own use-list,
transitively, so the generation still *reaches* everything and the whole rest of
the suite passes. The test asserts the generation's own **use-list**, not that a
call works. Verified to fail by restoring the old order (it names all 18 missing
modules).

**The `.scm` always ship too, and that is not redundancy.** `use-modules` loads a
module by resolving a NAME against `%load-path`, and the `.go` is only ever an
accelerator for that — with no `.scm` beside it there is nothing to resolve. The
`.go` is a first-load accelerator, never a replacement.

**THE MACHINERY IS LOADED ONCE PER PROCESS; the GENERATION is what gets
rebuilt.** This is the thing to understand before touching `init.scm`. The host
throws the Scheme side away on every reload and evaluates `init.scm` and the
config into a fresh generation module — but every `use-modules` in `init.scm` is
a no-op after the first generation, so the module OBJECTS, and everything
top-level in them, persist for the life of the plugin. That reuse is deliberate:
the gsubrs the host registered into the kernel (`registerAllBindings`, before
`init.scm` is read) live in those objects and have to survive, which is what lets
the kernel's `#:export`, naming them, be satisfied on every generation.

**Three things about the loader that are easy to get wrong, all verified:**

- **`use-modules` loads a module once per PROCESS, ever** — so the consequence is
  not about loading at all, it is that a module's TOP-LEVEL STATE outlives every
  generation. A `set!` from a config on an IMPORTED binding writes into the
  EXPORTING module's cell, so one config's `(set! hl--watchdog-ms 500)` would
  still be in force on the next reload. **A module that keeps state therefore
  defines an unexported `reset`**, and the sweep at the end of `init.scm` calls
  them all, once per generation, before the config is read. `t-api` pins the
  behaviour for the kernel's variables and for emacs-keymaps' three tables, and
  was verified to fail with a `reset` neutered.
- **The reset sweep iterates `module-uses`, which holds PUBLIC INTERFACES, not
  modules.** `use-modules` is `module-use!` applied to `resolve-interface`, and an
  interface carries only the EXPORTED names — so an unexported `reset` cannot be
  found in one. The sweep resolves each use-list entry's NAME back to the module
  first. Verified for `(hyprscheme kernel)`: the use-list entry has `module-kind`
  `interface`, the resolved name has `directory`, they are not `eq?`, and
  `module-local-variable` answers `#f` for the former and the variable for the
  latter. This was the bug that made the first version of the sweep silently do
  nothing at all.
- **`module-local-variable`, NOT `module-variable`,** is the predicate for a
  `reset`: the latter answers for IMPORTED bindings too, so a module that merely
  uses something named `reset` would look as though it had one. Own bindings
  only — which is also why leaving `reset` out of `#:export` is correct rather
  than a forgotten export, and why `tools/module-audit.py` carries a named
  exception for it (`UNEXPORTED`).
- **`#:ensure #f` IS NOT A PROBE — `resolve-module` autoloads.** It has two
  separate switches: the **second positional argument** is `autoload`
  (default `#t`), and `#:ensure` only says whether to invent an empty module
  when the search comes up empty. `(resolve-module name #:ensure #f)` therefore
  still LOADS the file it was asked about, and answers "already in" for a module
  that had never been loaded. The real probe is
  `(resolve-module name #f #:ensure #f)`: autoload off, nothing created, only
  what is already in the tree. That is the spelling the reset sweep uses.
- **`use-modules` is a SYNTAX form** whose arguments must be literal module names
  at expansion time; a runtime `(use-modules name)`, or an `eval` of one, fails
  to expand ("source expression failed to match any pattern"). That is only a
  constraint if something has to BUILD the import list at runtime, and nothing
  does any more — `init.scm`'s imports are plain `use-modules` forms. It does
  constrain the tools, though: the three that read the module list out of
  `init.scm` must match `use-modules` **with a `hyprscheme` head**, because the
  stdlib imports in the same file are plain `use-modules` too.

`tools/module-audit.py` checks that every module is named in `init.scm` — a
module missing from it is simply never loaded, with no error anywhere, and
load-check cannot see that (not loading something is not a load failure).

**The file paths are the module names** — `(hyprscheme kernel)` must be resolvable as
`hyprscheme/kernel.scm` — so the machinery directory goes on `%load-path`
(and `%load-compiled-path`, a SEPARATE list) at build time:

0. `init.scm` → the **entry point**, loaded directly by the host. Names every
   module below, in this order, and reloads them all on each later generation.
   It defines no module of its own.
1. `hyprscheme/kernel.scm` → `(hyprscheme kernel)` — the machinery AND the
   **whole C++ boundary**: error plumbing, the watchdog, the fire trampolines,
   the custom-layout entry points, the record types those read, and every
   `hl--c-*` gsubr (its export list is generated from the C++ `hl::bind<>()`
   registrations). Everything else imports it to reach C++.
2. `hyprscheme/core.scm` → `(hyprscheme core)` — shared plumbing: argument
   coercion, the plist convention, key-spec parsing (`hl-kbd`/`hl-key`), the
   handle predicates, the cross-reload state accessors, the listen helpers.
3. `hyprscheme/enums.scm` → `(hyprscheme enums)` — **the MACHINERY for naming
   a set of values** (`<enum-values>`, `make-enum-values`,
   `define-enum-values`, `enum-value`, `enum-symbol`, `enum-values?`,
   `enum-values->enum`), and nothing else. It defines no concrete enum. A
   **leaf** like core — it imports the kernel only and holds no compositor
   knowledge, which is why every family may import it. See "Enums" under API
   conventions.
   - `hyprscheme/values.scm` → `(hyprscheme values)` — the place for a
     concrete enum shared by MORE THAN ONE family: `direction-enum`,
     `direction-or-none-enum`, `corner-enum`, `key-state-enum`. Also a leaf
     (imports kernel + enums). It exists because a family may not import
     another family, so a set two of them need has nowhere else to live that
     both can reach. A set used by ONE family stays in that family
     (`fullscreen-mode` is in `window.scm`).
4. **One module per object family** — `window.scm`, `monitor.scm`,
   `workspace.scm`, `group.scm`, `layer.scm`, `bind.scm`, `event.scm`,
   `timer.scm`, `rule.scm`, `config.scm`, `notification.scm`, `gesture.scm`,
   `exec.scm`, `query.scm`. Each holds the **raw API**: thin wrappers over
   what the compositor can actually do.
5. `hyprscheme/extras.scm` → `(hyprscheme extras)` — the few conveniences
   that need several calls to compose (e.g. setting a special workspace
   *on/off* when the compositor only offers a *toggle*).
   - `hyprscheme/inotify.scm` → `(hyprscheme inotify)` — the **libc boundary**:
     inotify(7) reached through `(system foreign)`, no C. A leaf that imports
     nothing of ours, and a **boundary module** in `tools/api-names.scm`'s sense
     alongside the kernel, for the same reason — it is a table of foreign
     bindings, not API. Guile has no inotify of its own and the third-party
     `guile-inotify` is GPL-3.0 in a BSD tree, which is why it is here.
   - `hyprscheme/watch.scm` → `(hyprscheme watch)` — **reload when a file the
     config loaded changes**. A **composer** (it calls `hl-config-reload!`, which
     a family owns). `%load-hook` records every file the loader reads, the
     module watches each one's *directory*, and an event matching a recorded
     path reloads. Public surface: `hl-loaded-files`. The one thing it needs
     from C++ is `hl--c-on-readable` — waiting on an fd means the event loop.
6. **THERE IS NO UMBRELLA.** `hyprscheme.scm` → `(hyprscheme)` used to be the
   **public API**: a curated `#:re-export` list over everything, and "the single
   source of truth for what is public". It is **deleted**, and the reason is the
   same lesson as the `Host.cpp` list: a hand-kept list of what is public drifts
   from what is public. That one had — it listed `hl-bind-thunk` and
   `hl-bind-tokens`, two internal record accessors that no config should see,
   and they were the ONLY two names it had that the modules did not.

   **The public set is DERIVED, by asking Guile.** `tools/api-names.scm` loads
   the machinery the way the host does and then reads each module's
   `module-public-interface` — which cannot disagree with the code. What counts
   as API: every module `init.scm` names, **except the kernel** (the C++
   boundary: 368 exports, 313 of them `hl--c-*` gsubrs). **Extras and
   emacs-keymaps ARE API** — extended API rather than base, but a config calls
   `hl-kbd` and `hl-gesture-add!` directly and they follow the same testing
   rule. Internals are excluded by the convention the tree already keeps:
   `hl--*` internal, `*-rtd` a record type C++ resolves by name, `make-hl-*` a
   record constructor. **A name that is none of those and starts with `hl-` is
   API — if it should not be, rename it, don't filter it.** (That is what
   happened to the leaked pair: `hl-bind-thunk`/`hl-bind-tokens` became
   `hl--bind-thunk`/`hl--bind-tokens`.)

   The generation still imports the families, core and the kernel directly, so
   internals stay reachable in a config.

**The structural rule, checked by `tools/module-audit.py`:** a family imports
`(hyprscheme kernel)` and `(hyprscheme core)` and **nothing else of ours** —
it never calls another family. Only extras may compose families. That is what
makes the split worth having rather than a directory of mutual dependencies,
so the tool enforces it and the suite would catch a violation at runtime.
`hl--ready` is the kernel's flag and the **host** sets it once every module
has loaded; a `set!` of an imported binding is a syntax error in a declarative
module, which is why no `.scm` does it.

Things that bit us and are worth not rediscovering:

- **`#:export` must be explicit.** `module-export-all!` makes bindings
  visible to `module-variable` but does NOT reach a module that later
  `use-modules`/`#:re-export`s the module.
- **`#:re-export` re-exports what the module has already imported** — it does
  not go looking for the names, so the `#:use-module` must come first.
- **A module's bindings must exist before its file is read**, because its
  `#:export` names them. `#:export` is what makes this work: a name in the
  export list is an unassigned placeholder from the moment the module is
  defined, so the file reads and the gsubrs are assigned into those
  placeholders afterwards.
- **`hl::makeModule` READS THE FILE.** It resolves the name and resolve
  AUTOLOADS, so `kernel.scm` is read *before* `registerAllBindings()` runs —
  not after, as its comment claimed. Verified with a C probe. The
  consequence to know: **the kernel's top-level forms run with the gsubrs
  unbound**, so a gsubr called at kernel load time fails. Keep the kernel's
  top level to `define`s. (Nothing in the tree does this today; it is a trap
  for the next person who adds a top-level call there.)
- **NO GSBR AT A MODULE'S TOP LEVEL — and `make check` will not tell you.**
  `guild compile` runs with the machinery dir on `GUILE_LOAD_PATH` (it must:
  modules resolve each other by name from there), and **Guile evaluates
  `<load-path>/init.scm` at startup** — so compiling ANY module evaluates
  init.scm, which imports every module and runs each one's top level. A gsubr
  called there is unbound in a plain `guile`, and the failure is
  `Unbound variable: hl--c-on-readable` → `Aborted (core dumped)`. This is the
  kernel's "keep the top level to `define`s" rule made general: it cost an
  afternoon because `scm-compile` is NOT part of `make check`, so every lint was
  green while `make scm-compile`, `make install` and `make hyprpm-machinery`
  were all dying — and **hyprpm reported the build as successful** (it sees the
  `.so` step pass), leaving the installed machinery silently at the last good
  copy. Verified with a `(display …)` in an init.scm on the load path. Anything
  needing a gsubr at load time must be guarded (`guard` +
  `(when …)`), which is what `watch.scm` does.
- **Guile hoists top-level defines**, so a top-level `(define base load)` in a
  module file that also defines `load` captures that file's own shadow — write
  `(@ (guile) load)` when you mean Guile's. The kernel no longer shadows `load`
  at all (`hl-load`, in core, is an ordinary function), and it still says
  `(@ (guile) load)` where it wants Guile's own, for exactly this reason.
- **`#:declarative? #f` on the kernel is NOT about shadows any more.** It was
  (a declarative module refuses to shadow an imported binding), but the load and
  eval shadows are gone. It stays for the real reason, which is easy to miss:
  the HOST modifies the kernel after it is loaded — it assigns every gsubr into
  the exported placeholders (`registerAllBindings`) and sets `hl--ready`. A
  declarative module says that will not happen.
- **Imports are not transitive**: the API needs its own
  `(use-modules (srfi srfi-1) …)`, and the generation gets the same list so a
  config keeps the environment it always had.
- The **kernel must not depend on a family** (families import the kernel).
  The record types and their accessors therefore live in the kernel.
- **`define-module` is expanded in the CURRENT module**, so a file whose first
  form is one must be loaded from a base module that certainly carries the
  expander — never from one of the machinery's own modules, which import only
  what they were told to. `hl::loadModule` existed for this and is now dead
  (init.scm loads by name), but the insight is live: it is what
  `tools/load-check.scm` implements when it evaluates each file with
  `(guile-user)` current, and why a file loaded form-by-form needs
  `set-current-module` afterwards — evaluating `define-module` does NOT switch
  modules the way the loader's own handling does.
- `add-to-load-path` is a **syntax transformer**, not a procedure — the host
  sets `%load-path` as a variable.
- An **exception escaping into C++ corrupts the compositor heap**
  (`corrupted size vs. prev_size`). Every C++→Scheme call must be contained.
- **Never write `#:key #:allow-other-keys` with no named key between them.**
  Guile 3.0.11 fails to *link* such a `define*` — `Undefined symbol #f` from
  the VM linker, at `guild compile` time, with no hint that the lambda list is
  the cause. A named key first (`#:key x #:allow-other-keys`) links fine, and
  so does `#:key` + `#:rest`. Six functions had the broken adjacency
  (`hl-exec!` and the four `*-rule-add!`s plus `hl-device-add!`) and it went
  unnoticed because the *interpreter* accepts it happily — only compilation
  trips. And the keyword was **doing nothing**: with `#:rest` present, every
  unrecognised keyword pair is already captured by `#:rest`, which is exactly
  what those signatures want. So the rule is `#:key #:rest`, full stop.

(The deprecated Chez tree loaded prelude → defun → bootstrap; that
machinery — including the defun/describe-function file — is frozen in
`chez/`.)

Every C entry point is registered by C++ as a **gsubr** (`Bindings.hpp`:
`bind<Fn>()` wraps the function in a trampoline that unboxes arguments and
boxes the result, validating both), so the boundary is plain SCM in, plain
SCM out — no foreign-procedure, no value smuggling, no libffi. The runtime
side (interpreter startup, contained file loads, contained calls, GC
pinning) is `Guile.hpp`/`Guile.cpp`. The bootstrap's wrappers call those
gsubrs under internal `hl--c-*` names.

### The wipe: the generation is rebuilt on every reload

There is no persistent environment holding the API. `buildGeneration`
(Host.cpp) creates a **fresh generation module** per reload and evaluates
`init.scm` and the config into it — so a definition, a `set!`, or a handler from
the previous config is unreachable afterwards with no per-variable teardown
list to keep in step. Same clean-slate semantics as upstream's
per-generation lua_State, reached by rebuilding rather than by copying.
It is built aside and swapped in only if every step succeeded, so a
broken config leaves the previous generation live.

**What the wipe does NOT cover is the machinery.** `use-modules` loads a module
once per process, so the kernel, core and the families are loaded on the first
generation and reused for the life of the plugin — their top-level state
survives, and a config's `set!` on an exported binding writes into whichever
module exports it. That is what the `reset` protocol is for: a module with state
defines an unexported `reset`, and `init.scm` sweeps the generation's use-list
and calls them all, once per generation, before the config is read. The two
subtleties that make the sweep non-obvious (the use-list holds interfaces, and
the lookup must be `module-local-variable`) are in the loader section above.

**Three things the HOST deliberately keeps of its own**, each for a concrete
reason — do not move them into the machinery:

- the **stderr port** (`Guile.cpp`): a port owns the fd it wraps, so a
  re-created one closes fd 2 when its predecessor is collected. The error
  log dies silently, and enough of them take the process with it.
- the **after-gc hook** (`installGcHook`): registered once, or it stacks
  a fresh closure per reload.
- the **survive list** (`g_schemeState`, behind `hl-state-set!` and
  friends): documented to outlive reloads.

The host owns **`hl--ready`** too: it clears the flag before reading `init.scm`
and sets it once every module has loaded. Clearing it is load-bearing now that
the kernel is loaded once — on the second generation on the flag would still be
`#t` from last time, and the host's "did the API finish loading" check would
pass without the API having loaded at all.

`load` and `eval` are **no longer shadowed**. Loading a file into the generation
is `hl-load`, an ordinary public function in `(hyprscheme core)`, exported and
documented like anything else; a config may still call Guile's own `(load …)`,
which is unguarded and raises. `eval` needed no replacement at all — `(eval x)`
evaluates in `(current-module)`, and the host leaves the GENERATION current after
`buildGeneration` succeeds, so a bare `eval` already lands where the shadow used
to put it. `hl-load` names its target explicitly (`hl--c-generation`) rather than
relying on that.

## The C++ layout: one translation unit per object family

`src/config/scheme/` is split by object family (FABLE §4.7 Step G), not
by layer. Each family file **owns its entry points and registers them**
(`void registerX()`), and `attachInterp` calls each one — so nothing
outside a family needs its entry points declared:

`Host.cpp` (init, reload, inotify watch, IPC, crash handler, the
watchdog, `attachInterp`, `shutdown`) plus `Window`, `Workspace`,
`Monitor`, `Group`, `Layer`, `Bind`, `Event`, `Timer`, `Rule`, `Config`,
`Notification`, `Gesture`, `Exec`, `Query`, and `SchemeLayout.cpp` for
the custom-layout entry points. `Handles.*` is the object model,
`Bindings.hpp` the gsubr trampoline, `Guile.*` the runtime.

- **`SchemeInternals.hpp` is the one shared header.** It carries
  `g_up`, `g_configError`, `g_eventConnections`, the handle/result
  plumbing (`boolResult`, `windowHandleResult`, `wsGet`, `monGet`), the
  selector/name lookups, and a `registerX()` declaration per family.
  Templates and trivial helpers are `inline` here; anything with real
  weight is declared here and defined in the family that owns it.
- **Adding a family**: a new `.cpp` defining its entry points and
  `registerX()`, a `registerX();` call in `attachInterp` and a
  declaration in `SchemeInternals.hpp`, and **a line in the Makefile's
  `COMMON_OBJS`**. Miss that last one and the plugin still *links* — the
  failure appears only when the compositor loads it
  (`undefined symbol: …registerX`). `tools/bind-audit.py` checks the
  built `.so` for exactly this.
- A family's private state (`g_timerIndex`, the rule engine's maps, the
  gesture makers' singletons) stays `static` in its own file. State the
  host tears down at the reload boundary is exposed as a function
  (`cancelAllTimers`, `clearSchemeRules`, `dropEventHandlers`), never as
  a shared global.
- `tools/bind-audit.py` — run after touching the registration list or
  the bootstrap. It reports the total registered, any `hl--c-*` the
  machinery calls that nobody registers, duplicate names, entries
  registered but never called (dead C code), and undefined family
  symbols in the built `.so`. Exits 1 on a problem.

## Chez-in-C++ cheat sheet (frozen `chez/` tree; things that are NOT obvious)

- Only `Scall0..Scall3` exist. More args → pass ONE list and destructure
  in Scheme (the layout dispatchers do exactly this).
- This Chez's `scheme.h` has `Sfixnump` but **not** `Sintegerp`/`Sfalsep`.
- `foreign-procedure` resolves symbols at **definition time** — the C++
  side must `Sregister_symbol` everything before phase 1 loads.
- **syntax-case gotcha (cost an hour):** pattern variables substitute
  *inside quote* in a template. `(quote args)` with a pattern variable
  named `args` emits the arglist, not the symbol. Hence in
  `hyprscheme-defun.scm` the pattern vars are `formals`/`docstring` so
  `'args`/`'doc` stay literal.
- **parameterize gotcha:** `parameterize` only binds parameters
  (`make-parameter`), never plain variables. `hl--load-source` (current
  file being eval'd, for defun's metadata) is a parameter for this
  reason.
- `eval` of a macro keyword raises "invalid syntax"; unbound symbol →
  a compound condition. `describe-function` guards both → `#f`.
- `copy-environment` gives each binding its own location initialized to
  the current value — mutable objects (e.g. the metadata hashtable) are
  shared across generations, which is what the defun table wants.
- Shell heredocs: an `EOF` line inside heredoc content terminates it
  early and the rest executes as shell commands (this once wrote into
  the real config — see rules above). Prefer the Write tool or very
  careful heredocs.

## API conventions (settle disputes with these)

- Public API: `hl-*`; internal: `hl--*`. Emacs-ism machinery (defun,
  describe-function, later defcustom) is deliberately un-prefixed.

### THE BOUNDARY — a gsubr mirrors C++; policy lives in Scheme

**A `hl--c-*` gsubr is a thin mirror of the C/C++ call it exposes: the same
shape, with just enough added to make it a sensible Scheme call.** It converts
arguments in and the result out, and does nothing else. Every *decision* — which
of several C++ calls to make, what a value means, whether the caller wanted
toggle/set/unset — lives in Scheme, which is where the API is defined.

Two things this forbids, and both had been done:

- **Inventing a policy the C++ does not have.** `hl--c-window-fullscreen-toggle`
  hand-rolled an `isFullscreen` test around Hyprland's action — but that action
  already does the test internally, so the gsubr re-made a decision the C++ had
  already made. Worse, once Scheme has read the current mode (to decide at all),
  the answer is *known*: handing a "toggle" down throws that answer away and
  makes C++ derive it a second time.
- **Naming a gsubr after a policy rather than the call.** A gsubr called
  `-toggle` invites its caller to toggle. If the operation underneath SETS, it
  is `-set`.

**And it is the ONE place a value is converted.** A caller passes what it has;
the boundary makes the C++ type out of it. Two entries were missing for years,
and the cost was a conversion repeated at every call site:

- **`Arg<bool>`** — a C++ `bool` takes a Scheme BOOLEAN, by *truthiness*
  (`scm_is_true`, so anything but `#f`). Without it every boolean parameter was
  declared `int`, `Arg<int>` demanded a number, and **19 gsubrs** had call sites
  writing `(if x 1 0)`. Note the trap when converting one: `0` is TRUTHY, so a
  site left passing `(if x 1 0)` would invert.
- **`Arg<const char*>` takes a SYMBOL as well as a string.** A name-ish
  parameter — a config key, a tag, a prop — is what Scheme has as a symbol
  (`'max_size`), and requiring `symbol->string` at the call site is the
  conversion this layer exists to do. **Numbers are deliberately NOT accepted**:
  a number becomes a name where that is meaningful and nowhere else, which is
  the workspace/monitor argument resolvers. That coercion now lives in exactly
  five consumers (`hl--str` in the two resolvers, `hl--hyprland-name`,
  `hl--special-name`, and the rule loop's key — which compares against string
  literals) instead of forty call sites.

The corollary for a field pair: expose what C++ exposes. `getFullscreenModes`
has two fields, so there are `hl--c-window-fullscreen-mode` and `-client`; the
setter mirrors `setFullscreenMode`, which **sets** — so the Scheme decides the
target mode and the gsubr assigns it.

**A gsubr must convert to the units the C++ actually takes, and the units are
not always the obvious ones.** The one that cost a session: the send-shortcut
and send-key-state gsubrs took a key NAME and resolved it with
`xkb_keysym_from_name`, handing the **keysym** to `Config::Actions::pass`. But
`pass` passes its key straight to `sendKeyboardKey` as `key - 8` — an
XKB/evdev **keycode**, the units of the keybind manager's `m_lastCode`. The two
numberings are unrelated, so every sent key was the wrong number: `"f"` sent 94
(some other key) and the NAMED keys (`Right`, `Next`, `Prior` — the payload of
every chord reported broken, C-f/C-b/C-v/M-v) resolved to ~`0xff00`, far outside
the keycode range, so the press was **dropped in silence**. Nothing raised,
because `pass()` never validates its keycode — the action answered OK while
doing nothing. The fix is `Keybinds::resolver()->resolveKeycode()`, which is
what the scripting API's own dispatchers
(`LuaBindingsDispatchers dsp_sendShortcut`/`dsp_sendKeyState`) call at exactly
that point. Two things to carry forward:

- **check the units where the value is CONSUMED, not where it is named.** The
  header said `pass(ModifierMask, uint32_t key, …)` and told you nothing; the
  body's `key - 8` told you everything.
- **when a value must be validated, validate with the compositor's own
  resolver** rather than a lookalike (`xkb_keysym_from_name` accepts Cyrillic
  and Arabic keysyms, so it "validates" names that are on no loaded keymap;
  `resolveKeycode` checks the keymap).

### ERRORS — plain `(error …)`, the function names itself, and the user SEES it

**There is no `hl--error` and no `hl--record-field`.** Both were helpers for jobs
Guile already does, and both are gone:

- **Raise with `(error MESSAGE ARGUMENTS …)`.** Guile builds the message by
  appending one `~S` per argument, so `(error "hl-bind-add!: tokens must be
  strings, got:" tokens)` renders as that sentence with the value written at the
  end. Two consequences, and they are the whole style:
  - **the function names itself in the message** (`"hl-bind-add!: …"`) — no
    origin field is set any more, and the rendered log line is the same as it
    was;
  - **arguments go LAST.** A parameter cannot sit mid-sentence, so word the
    message to put it at the end. Where a value genuinely has to be
    interpolated — or where the *caller's* name is a runtime argument rather
    than a literal, which happens in the `hl--*-listen` helpers — build the
    string: `(error (format #f "~a: listener rejected" who))`. Rare.
  - Arguments are `write`n, so a **string argument renders quoted** (`"foo"`).
    Deliberate: usually the complaint IS about a value.
  - A `~s` written *by hand* inside an `error` message is a **trap** — Guile's
    own `~S` already consumed the argument, so it survives as literal text and
    the value is appended after it. Two messages were mangled this way in the
    pass that removed `hl--error`.
- **Read a record field with Guile's `record-accessor`**, bound once per field.
  It type-checks and reports `Wrong type argument (want `hl-box'): 5` — naming
  the record type — which is why the wrapper existed and why it was not needed.
  It is anonymous, though, so a public accessor's docstring is attached:
  ```scheme
  (define hl-box-x (record-accessor hl-box-rtd 'x))
  (set-procedure-property! hl-box-x 'documentation "The box's left edge.")
  ```
  (`procedure-documentation` reads that back; verified.)

**A tool that prints an exception must FORMAT it.** `exception-message` hands
back the raw `"~A: ~S"` template with the parts as irritants — printing it
unformatted says `~A: ~S` and names nothing. `tools/syntax-check.scm` did
exactly that, which is how a real read error once came out as noise.

**AN ERROR IS NOT JUST A LOG LINE — and the split is the compositor's own.**
Errors went only to stderr, and the compositor log does not carry `[scheme]`
lines in every session (measured: zero in a live one), so a raising bind thunk
looked exactly like a key that does nothing. A scratchpad key raised on every
press for weeks; the only trace was a line nobody was reading.

Hyprland answers two different questions with two different surfaces
(`CConfigManager::addError`, ConfigManager.cpp:829), and we route the same way:

| the error | surface | our path |
|---|---|---|
| at **runtime**, in a callback — "that key press failed" | a **notification**, transient | `hl--report e "bind"` → `hl--notify-error` |
| during **config load** — "your config is broken, go fix the file" | the persistent full-width **ErrorOverlay** banner | `hl--report-config` → `hl--c-error-overlay` |

- **RUNTIME errors are DEDUPLICATED PER (WHO, MESSAGE)**, which is the
  load-bearing part — a broken callback raises on EVERY event, so an unthrottled
  toast would be worse than the silence. `markReported` in Lua's `LuaCaller.cpp`
  is the model; ours is `hl--reported`, keyed by the WHO each call site passes
  ("bind", "event handler", "timer", "layout", "gesture", "module reset"). A
  *different* message from the same callback still gets through.
- **WHO is what the user reads**, so name the thing that failed, not the
  internal function.
- **A CONFIG error is the BANNER, and it is cleared by a successful load**
  (`hl--report-config-ok`, called by the HOST after `evalFile` succeeds — the
  Scheme side cannot know the whole config finished, since a config is many
  files). The banner ACCUMULATES across failing files and is capped at 12 lines
  like the compositor's own (~15), so a config that fails on every form cannot
  build a banner taller than the screen.
- **The notification call is GUARDED AND SILENT ON FAILURE.** It runs inside
  error handlers, and a raise there would replace the error being reported with a
  new one — the one thing an error reporter must never do.
- **It calls the GSUBR, not `hl-notification-add!`** — that name lives in
  `(hyprscheme notification)`, a FAMILY, and the kernel may not reach into one.
- **`hl--print-exception` renders a plain STRING as itself.** The watchdog
  reports a finished message, not a condition; `~s`-quoting it would change a log
  line `t-zzzz-exit` matches on.
- **No max-recursion guard, deliberately.** Lua needs one (a C `lua_State` stack,
  `MAX_CALL_DEPTH`); Scheme recursion is bounded by the VM's own stack and
  re-entering a callback is ordinary, so a depth cap would break correct configs.
  The timeout half IS implemented — `hl--report-watchdog`.

`t-api` asserts all of it (notification appears; the SAME message in the same
callback is suppressed; a DIFFERENT message is not) and was **verified to fail**
with `hl--notify-error` neutered. `t-zzzz-exit` needed the deliberate probe
messages added to its allowance list — which is that check working.

### COLOURS — a number, and alpha is FIRST

**A colour is a 0xAARRGGBB INTEGER, everywhere in this API.** Alpha first, then
red, green, blue — the packed-ARGB32 layout `CHyprColor`'s `uint64_t`
constructor reads (`Color.cpp`: `ALPHA >>24`, `RED >>16`, `GREEN >>8`, `BLUE`).
Opaque orange is `#xffffa500`; the natural-looking `#xffa500ff` is a blue.

Scheme has hex literals, so nothing has to be parsed and no string format has to
be learned: `(hl-notify! "hi" 5000 #:color #xffffa500)`. The gsubrs therefore take
`SCM` and convert with `colorFromNumber` (integer only, `scm_to_uint64`).

**A string is NOT accepted by a colour argument, deliberately.** Config-file
spellings exist (`#rgb`, `#rrggbbaa`, `rgba(...)`) and a config that has a colour
as TEXT converts it once, by name: **`string->color`** (in core, so every module
and every config can reach it) answers the integer or `#f`. Underneath it is the
compositor's own `Config::ParserUtils::parseColor` — the same parser the config
file, the rule engine, the IPC commands and the scripting API use — so our
acceptance set is Hyprland's by construction. That is the same argument as
`resolveKeycode` for keysyms: validate with the compositor's parser, never a
lookalike.

**What this replaced, and why:** the gsubrs used to take a string and parse it
with a hand-rolled `schemeColorFromStr` that accepted bare hex digits
(`"FFA500FF"`) that Hyprland's parser REJECTS — an invented spelling — while
refusing `rgba(...)` that Hyprland accepts. It was both stricter and more
permissive than the format it claimed to implement. And the reader returned an
integer while the setter demanded a string, so
`(hl-notification-color-set! n (hl-notification-color n))` was impossible.

**The two orderings are the trap, and both are real.** The integer is
alpha-first; `#RRGGBBAA` and `rgba(RRGGBBAA)` are RGB-first. `string->color`
reorders, which is why `"#ffa500ff"` and `"0xffffa500"` are the SAME colour.
`tests/t-api.sh` pins seven spellings against expected decimal integers measured
from the compositor's own parser (verified by linking `ParserUtils.cpp.o` and
printing them), including the two refusals (`"red"`, `"FFA500FF"`) and
`"#00ff00ff"` → `#xff00ff00` — the assertion that fails if anyone "simplifies"
the string path into a hex passthrough.

**A colour argument's absent value is `#f`, not 0**, in `hl-notify!`: 0 is a
legal colour (transparent black).

### ENUMS — a rule, not a preference
**A function that takes or returns ONE VALUE OUT OF A FIXED SET speaks in
symbols, and that set is a named enum.** It does not take a bare integer and it
does not return a string. `(hl-window-fullscreen-set! w #:mode 'maximized)`,
not `#:mode 1`; `(hl-window-fullscreen w)` answers `'maximized`, not `1`.

The machinery is `(hyprscheme enums)`: `make-enum-values` /
`define-enum-values` build an `<enum-values>` from an ass list of
`symbol -> integer`, `enum-value` takes **either spelling** and returns the
integer (so an API function accepts a symbol or an integer with no conversion
code of its own, and a value outside the set is an ERROR naming the set rather
than being silently misread), and `enum-symbol` is the inverse — what a getter
returns. `enum-values->enum` hands back a real **rnrs enum** built from the same
symbols, so the whole `(rnrs enums)` toolkit (`enum-set-member?`,
`enum-set-union`, `enum-set-indexer`) works on it; `(rnrs enums)` is imported
into the generation in `init.scm` so a config can actually reach that toolkit.

Two things about the design that are easy to get wrong:

- **the two numberings are independent and both are kept.** The rnrs enum
  numbers its members `0..n-1` in declaration order; the integers the API speaks
  are whatever the compositor uses (`fullscreen-mode` is `-1/0/1/2`, where `-1`
  is the write-side "leave it"). Neither is derivable from the other.
- **the error names the set in the caller's own currency** — a bad symbol is
  answered with the symbols, a bad integer with the integers.

Where a concrete enum GOES, which is the thing to get right when adding one —
`(hyprscheme enums)` holds the machinery only and defines no enum of its own:

- **used by ONE family** → that family. `fullscreen-mode`
  (`keep none maximized fullscreen`) is defined in `window.scm`.
- **used by MORE THAN ONE family** → `(hyprscheme values)`. A family may not
  import another family, so a shared set has nowhere else both can reach.
  `direction-enum` (`up right down left`), `direction-or-none-enum` (the same
  plus `none`, which is `-1` — the compositor's `DIRECTION_DEFAULT`, the
  directionless form `CGroup::remove` guards on), `corner-enum`
  (`bottom-left bottom-right top-right top-left` — NOT reading order; it
  mirrors `Actions::moveCursorToCorner`'s switch) and `key-state-enum`
  (`up down repeat` → `0 1 2`, the integers
  `Actions::sendKeyState` takes) all live there.

(This note previously claimed both of those lived in `(hyprscheme enums)`, and
that `fullscreen-handler` was one of them. Neither was true:
`fullscreen-handler` is not an enum in this tree at all.)
- **A workspace or monitor argument is a handle, a number, or a selector
  string** — one shape, accepted by every function that takes one
  (`workspaceArg`/`monitorArg` in the C++). A **handle is used directly**;
  only a string is resolved, with the compositor's own grammar
  (`getWorkspaceTargetFromString` / `monitorState()->query().configString`).
  Two rules that follow, and that were got wrong once:
  - **never stringify a handle** to re-derive the object — it is waste, and it
    destroys the grammar (a handle becomes a name, and a name lookup cannot
    express `"+1"` or `"previous"`);
  - **`hl-workspace-focus!` passes its string straight through** to
    `Config::Actions::changeWorkspace(const std::string&)`, which resolves *and
    creates* and honours `previous` / `workspace_back_and_forth`. Resolving it
    first would be a regression, not a tidy-up.
- **Mods are token lists everywhere.** `hl-kbd`/`hl-key` (Emacs- and
  Hyprland-style key spec parsers) generate them; no API splits strings
  and no raw modifier mask ints. The terminal slot is always the KEY;
  a trailing dash makes the whole spec a pure modifier list:
  `(hl-kbd "C-M-")` → `("CTRL" "ALT")`, while `(hl-kbd "s-M")` →
  `("SUPER" "M")`.
- Handles are opaque records with identity and staleness (`alive?`),
  no global registries; destruction releases locks via guardians.
- Exec is ONE function: `(hl-exec! cmd . effects)`; no shell/raw split.
  Plist rule objects, values coerced like `hl-window-rule-add!`.
- Gestures: typed action values built by `hl-make-*-gesture`
  constructors → opaque `(maker . args)` recipes; C++ side is
  `IGestureMaker` virtual dispatch with 11 stateless singletons and
  one-line accessors — **no name-string dispatch tables, no big IF
  chains** (Chris hates those; several were ripped out on request).
- Recipe plists are symbol-keyed with typed values; string conversion
  happens at one adapter point inside C++ `make()`.
- Custom layouts: five direct Scheme entry points
  (`hl--layout-window-open/close/msg/recalculate/resize`), fixed-shape
  payload lists destructured positionally — no event strings, no
  dispatching cond.
  The geometry callback is `(area placements)` → `((window . box) …)`:
  `area` is an `hl-box` (the work area), `placements` one
  `(window . box)` pair per window to place, each box being where that
  window is NOW; `resize` appends `(dx dy corner)`. **Boxes are whole
  pixels** — exact integers in, rounded on return. **Keyed by window,
  matched by window**: a partial or reordered return is either applied
  correctly or rejected, never silently mis-placed; a window not in the
  layout is refused and logged. A target with no window is skipped.
  Coordinates are GLOBAL (a second monitor's x is not 0). The returned
  box is the **cell** — the compositor insets the window inside it by
  the border and `gaps_in`, as for the built-in layouts. The callbacks
  run under the watchdog, and a failed or aborted pass falls back to a
  default grid for that pass only. `hl-box`/`hl-box-x/y/w/h` build and
  read boxes; `hl-window-group` turns a window into its group handle.
  (The old positional `(count W H windows)` → `((x y w h) …)` shape is
  gone from the main tree; `chez/` and FABLE.md still describe it,
  which is history.)
- **A dispatcher's boolean flag is a `#:key` named after the flag it turns
  on**, not a positional symbol: `(hl-window-swap-next! w #:prev #t)`, since the
  Lua dispatcher it mirrors is `swap({prev = true})`. The positional `'prev`
  form it replaced was a trap in both directions — its docstring promised
  `"'prev or #t"` while the code took ANY truthy value as backwards, and a
  "fix" that made `'prev` the only backwards spelling quietly removed `#t`
  (Chris caught that one). One documented spelling, and the option names the
  flag.
  **§2.11's option sweep is DONE for the flags** (`hl-window-cycle!`,
  `hl-window-size-set!`, `hl-window-position-set!`, `hl-group-cycle!`,
  `hl-group-window-move-next!`, `hl-window-fullscreen-sync!`,
  `hl-window-swap-next!`), and a **choice from a set is `#:mode 'x`** — not
  three booleans — as the gesture makers now take it. The old positional
  spellings are refused, not ignored: a stale config fails loudly instead of
  meaning the opposite. What remains of §2.11 is a separate question — the
  genuine OPTIONAL VALUES (`hl-state-ref`'s default, `hl-submap`'s NEXT,
  `hl-group-add!`'s index) and the
  optional-*window* pair (`hl-window-send-shortcut!`, `-send-key-state!`),
  neither of which is a flag wearing the wrong clothes.
- Binds validate exclusivity (long-press/release vs repeat conflicts)
  at the Scheme level; hyprland does not.
- Documentation: every public function is a plain `define` whose body
  starts with a Guile docstring (leading string literal; Guile stores
  it as the procedure's documentation and introspects formals — the
  REPL's `,describe` / `procedure-documentation` surface it). The defun
  machinery (arglist capture + describe-function) exists only in the
  frozen `chez/` tree.

## Docs (wiki lives in `../Hyprscheme.wiki`)

- Structure mirrors upstream's Lua docs layout. Fields table FIRST,
  actions last; directions quoted as strings (`"swipe"`); examples at
  top level (not under remove!/etc.); directions tables must match the
  original's row count exactly.
- **The word "upstream" must not appear in prose** — docs stand alone.
  Allowed only in `*Upstream page:*` footers and `*from lua ...*`
  annotations.
- Directions accept ONLY the documented spellings; no undocumented
  aliases (l/r/u/d) — Chris explicitly rejected "code to support this".
- `tests/t-zzz-docs` runs every ```scheme block in the wiki.

## Testing

- **Every harness and tool points the config environment at a directory it
  OWNS** — `XDG_CONFIG_HOME` *and* `HYPRSCHEME_CONFIG`, at a temp path, before
  anything runs. `run.sh` and `soak.sh` do it for the suite; the `doc-*` tools
  do it for themselves, because the docs reference files under
  `$XDG_CONFIG_HOME` (core.md's split-config example loads `keybinds.scm`) and
  a lint must never resolve — let alone read or run — the real config. This is
  the same rule as never touching `~/.config/`, one level down: a tool that
  inherits the ambient environment is a tool that pokes the user's config.
- `tests/run.sh` — the 18-file suite against a nested compositor, 18/18
  green is the bar. It tests the **tree, pre-install**: the compositor is
  `../Hyprland/build/Hyprland` (never an installed one — testing what is
  installed is a different question), the plugin is this directory's
  `scheme-plugin-guile.so`, and `HYPRSCHEME_SCM_DIR` is exported at the
  tree's `src/config/scheme` so the machinery that runs is the machinery
  under test. `BIN=`/`PLUGIN=` override; both harnesses fail loudly rather
  than silently exec a missing binary.
  `t-coverage` fails if a public API lacks a test.
  `t-zzz-snippets.sh` runs the wiki's snippets for real: it extracts a block,
  pulls the thunk out of the form and calls it (`tests/doc-snippets.scm`), so a
  snippet whose composition is wrong fails here rather than in a reader's
  config. The doc tiers are cumulative — `t-zzz-docs` evaluates every block,
  `doc-audit` checks every name exists, `doc-exercise` calls every registered
  callback, and this one checks what a callback DOES. Beyond them all,
  `t-zzzz-exit.sh` (which must sort last anyway) asserts that the **compositor
  log carries no `[scheme] error:` line except the ones the tests ask for** —
  three, measured against a green run. That is the only check that sees a bug
  whose names all exist and which only fails when it is actually CALLED with
  real data; two were found that way in one pass. Verified to fail by injecting
  a window-open callback that closes its window with a stray argument: every
  other tier passed it, and the log check named the error.
  `t-keydeliver.sh` exists because the opposite blind spot kept a real bug
  alive for three weeks: **an assertion on a RETURN VALUE cannot see a wrong
  PAYLOAD.** t-crashdir asserted `(hl-window-send-shortcut! …)` answers `#t`,
  which it did — while sending a nonsense keycode (see the keysym note under
  API conventions). Every lint was green throughout. What a harness can and
  cannot see is worth measuring before writing the test rather than after:
  delivered bytes are NOT observable here, because `pass()` stamps the event
  with `state()->m_timeLastMs`, which only a real keypress sets — a correct,
  explicitly-keycoded send still delivers 0 bytes to a focused `cat` in a
  nested compositor, with a keyboard present and no error. So this file pins
  the RESOLUTION (keycode tokens accepted, off-keymap keysyms refused — the two
  rows where the two implementations differ) and leaves delivery to a live
  session. It was verified to fail on the old code: 7 failures naming the bug.
  `t-zzz-configpath.sh` is the one test that **launches its own compositors**,
  because the config path is resolved once at plugin load and the suite's
  compositor cannot be reconfigured for it. It unsets `HYPRSCHEME_CONFIG`
  (which run.sh exports) so the default XDG search is actually exercised, and
  covers the three cases that matter: the `XDG_CONFIG_HOME` config, a config
  found only through `XDG_CONFIG_DIRS` (with invalid entries skipped), and a
  user config shadowing a system one. Verified to fail by disabling the
  `XDG_CONFIG_DIRS` walk in `userConfigPath`.
  `t-zzz-monitors.sh` is the one test that changes the *monitor topology*:
  it creates a headless output and moves the primary off the origin, because
  every other test runs on a single output at (0,0) where a layout's
  coordinates and the work area's origin are indistinguishable (FABLE §2.5).
  It sorts late — after the doc test — and restores the topology itself,
  asserting that it did. A test that needs a second output anywhere else
  should join it rather than create one earlier.
- `tests/soak.sh [SECONDS]` — sustained-load soak (live window pool,
  custom layout under load, event/timer/bind churn), logs to
  `/tmp/hyprscheme-soak-last/`, `SOAK_TRACE=1` for per-eval tracing.
- Nested instance discovery accepts ONLY directories that did not
  exist before launch (a stale-dir match once made a test drive the
  REAL session — the harnesses are written defensively about this).
- **NEVER delete anything under `$XDG_RUNTIME_DIR/hypr/`** — that
  includes the user's LIVE session IPC dir (doing so broke the real
  session's hyprctl until the session restarted). Stray nested
  instances are killed with `pkill -f hyprland-scheme` only.
- When a nested compositor misbehaves, suspect a stale plugin/compositor
  pairing first; rebuild all three.

## Hyprland sync reality (from the git-history survey)

~80 plugin-included headers, but the danger is *declaration* churn
(fields/virtuals), which happens roughly **every couple of months**
in long-quiet-stretch + periodic-refactor patterns (layouts #12890,
fullscreen #14705, views #15779, keybinds #15568, workspace #16140).
After each refactor pass a stale plugin is dead — rebuild. Documented
in the wiki's building-the-plugin page.

## Closed decisions (don't reopen without Chris)

- No `.lua`-style completion stub: no consumer for Scheme symbols in
  Lua tooling; closed.
- No 4400-line Customize widget editor equivalent; defcustom, if it
  comes, keeps `:type :set :get` semantics only (assign-if-unbound).
- describe output is text (via hyprctl eval); no in-process GUI — Chez
  has none.
- The old-build framebuffer crash (IFramebuffer::alloc ←
  CPropRefresher) did not reproduce after a Hyprland pull+rebuild; no
  bug report filed by agreement.

## Still open (see also DONE.txt / TODO.txt)

- Chez → Guile migration: DONE, and the migration scaffolding is gone.
  The findings + step plan live in `GUILE-CONVERSION.txt` (probed live on
  Guile 3.0.11) as the historical record. `SchemeHost.hpp` /
  `SchemeHostGuile.cpp` (the two-backend abstraction) were replaced by
  `Bindings.hpp` + `Guile.hpp`/`Guile.cpp`; `SchemeValue`, the word
  smuggling, the marshalling and `hyprscheme-compat-guile.scm` no longer
  exist. The Chez artifact is not built by this tree at all (frozen in
  `chez/`).
  STEP 3 DONE: SUITE 12/12 ON GUILE (chez 12/12 unchanged — 12 files then;
  the suite is 18 now, see Testing). The
  gotcha list lives in GUILE-CONVERSION.txt step 3 — headline items:
  boot-9's error template-wraps messages ("~A" + irritants — display
  code must render them); srfi-9 constructor/accessors are syntax
  transformers (records must expand onto core record primitives so
  forward references late-bind); the watchdog is sigaction SIGALRM +
  asyncs (the old FFI shim also had to wrap every call in
  call-with-blocked-asyncs — a watchdog escape inside compositor C++
  crashed it once; the gsubr boundary needs no such wrapper, since asyncs
  are not delivered inside a C function);
  stderr goes through (fdopen 2 "w") — never open-file "/dev/stderr"
  (fresh offset overwrites the log). Dead-handle drain DONE (after-gc-hook runs hl--drain-handles!,
  guarded), crash-reporter interplay VERIFIED (kill -SEGV → proper
  report; guile's init doesn't displace our handlers), 5-minute soak
  on guile PASS. NOTE: plugin LOG() is swallowed by an
  upstream logger refactor (inline header var — two copies); Guile
  call errors ride fd 2 prints in the host.
- defun is fully retired from the main tree: every public function is a
  plain `define` with a Guile docstring (private `--` functions too).
  The defun/describe-function machinery is frozen in `chez/` only.
- doc-name spellcheck for the wiki: DONE — `tools/doc-audit.py` checks every
  `hl-*` name the wiki uses, in blocks and in prose, against the API's export
  lists. (The balance checker is the reader check in `wiki-examples.awk`.)
- Interactive REPL.
- Event self-removal-during-fire test.
- Chris does the committing — never commit unprompted.
