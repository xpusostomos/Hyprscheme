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
  - `tools/bind-audit.py` — every `hl--c-*` the machinery calls is
    registered, none twice, none dead — and nothing is left undefined in
    the built `.so` (how a family object missing from `COMMON_OBJS` shows
    up: it links fine and fails only when the compositor loads it).
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
  module-audit, an undefined call at load trips load-check, a bad name in
  the wiki trips doc-audit, a bad name inside a thunk trips doc-exercise.
  A check that cannot fail is worse than no check — `t-coverage` passed
  vacuously for its whole life.

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

The machinery is **three modules** (no embedded blobs — the `.scm` were
extracted out of the C++ in Sep 2026), installed next to the `.so`, found
via `dladdr` on a known symbol, with fallbacks to the source tree
(`SOURCE_DIR` compile define) and `HYPRSCHEME_SCM_DIR` env var. **The file
paths are the module names** — `(hyprscheme kernel)` must be resolvable as
`hyprscheme/kernel.scm` — so the machinery directory goes on `%load-path`
at build time:

1. `hyprscheme/kernel.scm` → `(hyprscheme kernel)` — the machinery AND the
   **whole C++ boundary**: error plumbing, the watchdog, the fire trampolines,
   the custom-layout entry points, the record types those read, and every
   `hl--c-*` gsubr (its export list is generated from the C++ `hl::bind<>()`
   registrations). Everything else imports it to reach C++.
2. `hyprscheme/core.scm` → `(hyprscheme core)` — shared plumbing: argument
   coercion, the plist convention, key-spec parsing (`hl-kbd`/`hl-key`), the
   handle predicates, the cross-reload state accessors, the listen helpers.
3. **One module per object family** — `window.scm`, `monitor.scm`,
   `workspace.scm`, `group.scm`, `layer.scm`, `bind.scm`, `event.scm`,
   `timer.scm`, `rule.scm`, `config.scm`, `notification.scm`, `gesture.scm`,
   `exec.scm`, `query.scm`. Each holds the **raw API**: thin wrappers over
   what the compositor can actually do.
4. `hyprscheme/extras.scm` → `(hyprscheme extras)` — the few conveniences
   that need several calls to compose (e.g. setting a special workspace
   *on/off* when the compositor only offers a *toggle*).
5. `hyprscheme.scm` → `(hyprscheme)` — the **public API**: a curated
   `#:re-export` list over all of them. **This list is the single source of
   truth for "what is public"** (`t-coverage` reads it). The generation a
   config runs in does NOT import it — it imports the families, core and the
   kernel directly, so internals stay reachable.

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
- **`#:re-export` re-exports what the module has already imported** — the
  umbrella needs `#:use-module (hyprscheme api)` (and the kernel) first.
- **A module's bindings must exist before its file is read**, because its
  `#:export` names them. The host defines the kernel's two gsubrs
  (`hl--c-generation`, `hl--c-run-finalizers`) before reading `kernel.scm`.
- **Guile hoists top-level defines**, so `(define hl--base-load load)` in a
  module file captures that file's own `load` shadow — use `(@ (guile) load)`.
- A module that shadows `load`/`eval` needs **`#:declarative? #f`**.
- **Imports are not transitive**: the API needs its own
  `(use-modules (srfi srfi-1) …)`, and the generation gets the same list so a
  config keeps the environment it always had.
- The **kernel must not depend on a family** (families import the kernel).
  The record types and their accessors therefore live in the kernel.
- **`loadModule` loads from a base module**, not from whatever is current:
  `define-module` is expanded in the current module, and one of the
  machinery's own modules need not carry its expander.
- `add-to-load-path` is a **syntax transformer**, not a procedure — the host
  sets `%load-path` as a variable.
- An **exception escaping into C++ corrupts the compositor heap**
  (`corrupted size vs. prev_size`). Every C++→Scheme call must be contained.

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

### The wipe: the Scheme side is rebuilt on every reload

There is no persistent environment holding the API. `buildGeneration`
(Host.cpp) creates each module per reload, defines its bindings into it,
reads its file, and gives a **fresh generation module** the API and the
kernel to import — then the config is read into that generation — so a definition, a `set!`, or a handler from the
previous config is unreachable afterwards with no per-variable teardown
list to keep in step. Same clean-slate semantics as upstream's
per-generation lua_State, reached by rebuilding rather than by copying.
It is built aside and swapped in only if every step succeeded, so a
broken config leaves the previous generation live.

**Exactly three things live outside the wipe**, each for a concrete
reason — do not move them back into the machinery:

- the **stderr port** (`Guile.cpp`): a port owns the fd it wraps, so a
  re-created one closes fd 2 when its predecessor is collected. The error
  log dies silently, and enough of them take the process with it.
- the **after-gc hook** (`installGcHook`): registered once, or it stacks
  a fresh closure per reload.
- the **survive list** (`g_schemeState`, behind `hl-state-set!` and
  friends): documented to outlive reloads.

`hl--watchdog-ms` needs no such treatment: the reload rebuilds it from
source, so a config's override resets on its own. `load`/`eval` are
shadowed to target the current generation (`hl--c-generation`).

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
  `hl-group-window-move-next!`, `hl-window-fullscreen-state`,
  `hl-window-swap-next!`), and a **choice from a set is `#:mode 'x`** — not
  three booleans — as the gesture makers now take it. The old positional
  spellings are refused, not ignored: a stale config fails loudly instead of
  meaning the opposite. What remains of §2.11 is a separate question — the
  genuine OPTIONAL VALUES (`hl-state-ref`'s default, `hl-submap`'s NEXT,
  `hl-group-add!`'s index, `hl-curve-add!`'s variadic data) and the
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
- `tests/run.sh` — the 14-file suite against a nested compositor
  (`~/.local/bin/hyprland-scheme`), 14/14 green is the bar.
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
  the suite is 14 now, see Testing). The
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
- Working tree uncommitted since around `3c0184b` — commit when Chris
  says so, never before (he does the committing).
