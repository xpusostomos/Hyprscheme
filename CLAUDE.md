# Hyprscheme — notes for working in this repo

Hyprscheme embeds Guile Scheme inside Hyprland as a loadable plugin
(`scheme-plugin-guile.so`). The original Chez backend is DEPRECATED:
frozen in `chez/` (see `chez/README.md`), still buildable there, no
longer developed — don't evolve it. Configs are `~/.config/hypr/hyprland.scm`; the
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
- `make install` → `~/.local/lib/hyprscheme/`: the `.so` and the two
  `.scm` machinery files (prelude, bootstrap). No boot
  files — the interpreter is the system Guile.
  `make install-compositor` → `~/.local/bin/hyprland-scheme` +
  session `.desktop`. `PREFIX` selects the destination.
- **If anything starts crashing weirdly, rebuild all three** (Chez,
  Hyprland, plugin). A mismatched pairing either fails loud (renamed
  symbol) or misbehaves silently (changed class layout). Never run the
  plugin against a Hyprland built from a different tree.
- Packaging: `packaging/PKGBUILD` — Arch package building a pinned
  Hyprland + the Guile plugin (installs via the Makefile's DESTDIR
  rule) and ships the matched compositor as `hyprland-scheme`. The
  frozen Chez variant lives at `chez/packaging/PKGBUILD`.
- `tools/syntax-check.scm` — after ANY scripted edit to a machinery
  `.scm` file, run `guile -s tools/syntax-check.scm <files>`: it runs
  Guile's own reader over the files and reports read errors with the
  reader's file:line:col (exit 1 on failure). Catches the
  paren/quote/docstring-corruption class of bug at the edit site.

## How the plugin loads Scheme

Two real `.scm` files (no embedded blobs — they were extracted out of
the C++ in Sep 2026), installed next to the `.so`, found
via `dladdr` on a known symbol, with fallbacks to the source tree
(`SOURCE_DIR` compile define) and `HYPRSCHEME_SCM_DIR` env var:

1. `hyprscheme-prelude.scm` — error plumbing, the callback watchdog,
   the fire trampolines and the custom-layout entry points, in plain
   Guile (standard-library imports only, no dialect shims). Loaded
   through the guarded `hl--load`. An error here disables scheme, which
   is why it must stay "verified, static".
2. `hyprscheme-bootstrap.scm` — the API itself, guarded; ends with
   `(set! hl--ready #t)`. If anything fails, `hl--ready` stays `#f`
   and the C++ side disables scheme loudly.

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

Everything evaluates into the **persistent** environment. Each config
reload calls `hl--reset`, which copies the interaction environment into
a fresh generation (user definitions can't leak across reloads, like
upstream's per-generation lua_State). `load`/`eval` are shadowed to
target the current generation. `hl--state` is the one deliberate
cross-generation bridge.

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

- `tests/run.sh` — the 12-file suite against a nested compositor
  (`~/.local/bin/hyprland-scheme`), 12/12 green is the bar.
  `t-coverage` fails if a public API lacks a test.
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
  STEP 3 DONE: SUITE 12/12 ON GUILE (chez 12/12 unchanged). The
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
- doc-name spellcheck for the wiki (balance checker exists).
- Interactive REPL.
- Event self-removal-during-fire test.
- Working tree uncommitted since around `3c0184b` — commit when Chris
  says so, never before (he does the committing).
