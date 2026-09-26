# FABLE.md — a whole-project review of Hyprscheme (Guile era)

Written 2026-09-26 by Claude Fable 5 at Chris's request: "review the
whole thing … find flaws, style problems, parity gaps, reorganisation
ideas, what would make it more Scheme-like / Guile-friendly / Emacs-like
/ easier to use and program, what external tools to build." Nothing in
the repo was changed for this file. The `chez/` tree was not reviewed
(deprecated, frozen).

Everything here is a finding or a *proposal*. Per the house rules
(CLAUDE.md), none of it is to be acted on without an agreed plan; the
plans below are written so each can be lifted out and agreed on its
own. File:line references are to the tree as of commit `c3debe8`.


The short version:

The project is in good shape where it matters: the Lua surface is
essentially all mirrored (all 34 events, every object field bar two
monitor items, every dispatcher), errors are contained, and the core
design decisions — opaque handles with identity and staleness,
token-list mods, keyword options, typed gesture recipes, the
nested-compositor suite and doc-example test — are the right ones. The
one-sentence criticism is that the code still thinks it is Chez: a
compat layer emulates Chez for every user config, the FFI is
foreign-procedure over libffi with Scheme values smuggled through as
machine words, handles are guardian-managed cells around raw heap
addresses, errors carry no backtrace, and nothing is compiled. The
biggest available win is to invert that and go Guile-native (gsubrs,
modules, foreign object types, compiled .go, real exceptions) — §4 is
the step plan.

Concrete bugs found (§2), the ones I'd fix first:

1. tests/t-coverage.sh has passed vacuously since the bootstrap left
   SchemeManager.cpp — it extracts zero APIs. Run by hand it finds 13
   untested public functions.
2. hl-monitor=? was mangled into hl-monitor-rule-add!=? by the rename
   sweep; t-api.sh:285 tests the broken name, and the wiki never
   mentions monitor equality.
3. Two leftover debug writes to /tmp/hs-wd-probe (every callback) and
   /tmp/hs-sel-debug (every hl-window-class).
4. The compat define-record-type accessors have no type check, so
   (hl-window-title (hl-active-workspace)) reinterprets a workspace
   handle as a window and segfaults the compositor.
5. Custom layouts receive only W H and boxes are applied as global
   coordinates — wrong monitor / offset by gaps and bars on anything
   but the single nested test output (confirmed against
   Space::workArea()).
6. Stale wiki examples (submaps.md still uses hl-window-resize and
   'repeat #t, code-snippets.md uses hl-window-into-group with "l"/"d"
   directions, others) — the suite is probably red on t-zzz-docs;
   worth running.
7. Two plugin-main.cpps (the tracked one in src/ is dead), a hardcoded
   /home/chris/GITE/chez-pic, dead fireSchemeBind(int),
   README/Home/core still describing Chez, and errorf throwing away
   the function name in every error.

Real parity gaps (§5): workspace/monitor selector strings ("+1",
"e+1", "m-1", "l", "current", on_current_monitor) aren't resolved for
focus/move; no emergency binds when a Scheme config fails (Lua's
emergency mode never trips because the Lua side has no errors); no
runtime-error notification or traceback; no keybind enable/disable or
introspection; hl.env; plugin custom events; hl-windows filters.

Proposals you'll want to weigh (§6–7): Guile hooks (add-hook!
hl-window-open-hook …) instead of the -notification-add! family —
flagged because hl-notification-remove! currently means "unsubscribe
an event" while hl-notification-dismiss! means "close a bubble"; and
an in-process cooperative REPL server ((system repl coop-server),
polled from Hyprland's event loop) which gives a full Guile REPL and
Emacs Geiser connected to the live compositor for almost no client
code. §12 lists the six decisions only you can make.

---------------------------------------------------------------------

## 0. TL;DR

**The good news.** The hard part is done and done well: a Scheme
interpreter lives inside Hyprland, the *whole* Lua surface (binds,
events, timers, rules, queries, notifications, gestures, groups, layers,
devices, monitors, custom layouts) has an `hl-*` twin, errors are
contained, the design decisions that matter (handles with identity and
staleness, no registries, token-list mods, plist/keyword options, typed
gesture recipes, a live eval channel, a real nested-compositor test
suite plus a soak harness, a doc-example test) are the right ones and
are written down. The Chez→Guile migration was executed cleanly through
a host abstraction and the suite carried it. For a one-person project
this is a lot of correct work.

**The bad news, in one sentence:** the code still *thinks* it is Chez.
The Guile host emulates Chez (a compat layer redefines `format`, `load`,
`eval`, `define-record-type`, `current-error-port`, `void` … for every
user config), the FFI is Chez's `foreign-procedure` re-implemented over
libffi with Scheme values smuggled through as raw machine words, handles
are guardian-managed cons cells around raw C++ heap addresses, error
messages carry no backtrace or source location, and nothing is compiled.
Now that Chez is frozen, the biggest single improvement available is to
**invert the compat layer**: make the machinery idiomatic Guile (modules,
gsubrs, foreign object types, compiled `.go`, real exceptions with
backtraces) and delete the emulation. Section 4 is the plan.

**Concrete bugs found** (section 2 has the details): a dead coverage
meta-test that has passed vacuously since the bootstrap left
`SchemeManager.cpp`; a botched rename (`hl-monitor-rule-add!=?` where
`hl-monitor=?` should be — and the test suite locks the broken name in);
two leftover debug writes to `/tmp` on hot paths (every callback, every
`hl-window-class`); record accessors with no type check so a user typo
(passing a workspace where a window is expected) segfaults the
compositor; custom layouts that ignore the work area's x/y and so place
windows on the wrong monitor in multi-monitor setups; a handful of stale
wiki examples that call functions that no longer exist; a duplicate
stale `plugin-main.cpp`; `errorf` dropping the function name from every
error message; and a dozen naming inconsistencies.

**Top ten actions**, in the order I would do them:

1. Fix `tests/t-coverage.sh` (it extracts zero APIs) and the 13 untested
   public functions it would have flagged. §2.1
2. Fix `hl-monitor-rule-add!=?` → `hl-monitor=?` (bootstrap, t-api, wiki). §2.2
3. Remove the two `/tmp/hs-*` debug `ofstream`s. §2.3
4. Type-check handle accessors (or move to foreign object types) so a
   typo can't crash the compositor. §2.4
5. Pass the work-area origin to custom layouts (and reconsider the
   layout API against Lua's `ctx`/target model). §2.5, §5
6. Sweep the wiki for stale names/option syntax; make `t-zzz-docs`
   invoke registered thunks so lambdas get exercised. §2.6, §8
7. Backtraces + source locations in every error report; route errors to
   a place users can find (`hyprctl rollinglog` or a notification). §4.5
8. Compile the machinery with `guild compile -Wunbound-variable` at
   build time (would have caught the rename class of bug). §4.6
9. The Guile-native rewrite of the host: gsubrs instead of
   `foreign-procedure`+libffi, modules instead of `copy-environment`,
   delete the compat layer. §4.1–4.4
10. An in-process cooperative REPL server → Geiser/Emacs connects to the
    live compositor. §7.1

---------------------------------------------------------------------

## 1. What is good (keep these)

- **The containment story.** Every entry from C++ into Scheme goes
  through a guarded trampoline; every entry from Scheme into C++ goes
  through a tiny host vocabulary (`SchemeHost.hpp`, ~20 ops). Errors do
  not unwind through compositor frames. The compositor has not been
  taken down by a config error in the whole recorded history.
- **The handle model.** Opaque records, identity via `=?`, staleness via
  `alive?`, every getter on a dead handle returns `#f`, no registries.
  This mirrors upstream's userdata model exactly and is the right shape.
- **Token-list mods and `hl-kbd`.** Emacs key syntax that emits the same
  token list Hyprland syntax does, with the "terminal slot is the key"
  rule, is a genuinely nice piece of API design.
- **Keyword options** (`#:release #t`, `#:match '(class "^foo$")`). The
  recent conversion to `define*`/`#:key` is the single most
  Guile-friendly change made so far and it pays off in every docstring.
- **Typed gesture recipes** with virtual dispatch on the C++ side and
  one one-line accessor per maker. No string tables. Adding a gesture is
  three small edits.
- **Docstrings on every public define.** Guile stores them; `,describe`
  in a REPL works today for free. This is the foundation for §7.
- **The test posture.** A nested-compositor harness, a meta-test that
  (is meant to) force tests for new APIs, a doc-example runner, a soak
  test with tracing, defensive instance discovery. Most plugin projects
  have none of this.
- **The written record.** CLAUDE.md, DONE.txt, TODO.txt,
  GUILE-CONVERSION.txt are unusually honest ("PROCESS NOTE (an honest
  record)…", "CORRECTION: … I repeated the error across two status
  reports"). Keep doing that; this file joins them.
- **The build coupling.** Compositor binary as a make prerequisite of
  the plugin, the "rebuild all three" rule, the `hyprland-scheme`
  session entry, the pinned-commit PKGBUILD. Correct for a plugin that
  resolves compositor symbols at load time.

---------------------------------------------------------------------

## 2. Bugs and defects found

Ordered roughly by severity. Each has a fix sketch.

### 2.1 `tests/t-coverage.sh` is a no-op (HIGH — it is one of the "12/12")

`tests/t-coverage.sh:4` extracts the API list with
`awk '/^static constexpr const char\* SCHEME_BOOTSTRAP/,/^\)scm";/' src/config/scheme/SchemeManager.cpp`.
That marker has not existed since the `.scm` files were extracted out of
`SchemeManager.cpp` (Sep 2026, per CLAUDE.md). `grep -c SCHEME_BOOTSTRAP
SchemeManager.cpp` → 0. So `apis` is empty, the loop runs zero times and
the test passes vacuously. Running the intended check by hand against
`hyprscheme-bootstrap.scm` finds **13 public APIs with no test
reference**: `hl-group-alive?`, `hl-group-cycle!`, `hl-group-id`,
`hl-layer=?`, `hl-layer-id`, `hl-layer-pid`, `hl-monitor-id`,
`hl-notification-color-set!`, `hl-notification-id`, `hl-plist-get`,
`hl-timer-cancel!`, `hl-windows-from`, `hl-window-swallow-toggle!`.

*Fix:* point the extraction at
`src/config/scheme/hyprscheme-bootstrap.scm` with a regex that accepts
`(define` and `(define*` and the full name alphabet
(`[a-z0-9=!?*-]`); add tests for the 13. Then also make the test fail
if the extraction yields fewer than, say, 200 names — a vacuity guard so
this cannot silently happen again.

### 2.2 `hl-monitor-rule-add!=?` — a boundary-unsafe rename (HIGH)

`hyprscheme-bootstrap.scm:1610` defines `(hl-monitor-rule-add!=? a b)`.
That is `hl-monitor=?` after the `hl-monitor → hl-monitor-rule-add!`
rename (TODO.txt "RENAMES + DOC RESTRUCTURE", noted as "boundary-safe" —
it wasn't for this one). The sibling `hl-workspace=?`, `hl-window=?`,
`hl-group=?`, `hl-layer=?`, `hl-notification=?` all exist under the
right names. Worse: `tests/t-api.sh:285` asserts
`(hl-monitor-rule-add!=? am am)` — the test was renamed along with the
bug, so it "verifies" the broken name — and `hl-monitor=?` appears
nowhere in the wiki (monitor equality is undocumented).

*Fix:* rename in the bootstrap and the test, add `hl-monitor=?` to
`monitors.md`. Then §4.6 (compile-time lint) and a "wiki names ⊆
defined names" check (§8) to make this class of bug impossible to
ship. A simple cross-check today:
`comm -23 <(wiki hl- names) <(defined hl- names)` lists eight stale
wiki names (see §2.6) and `comm -13` lists twelve defined names the
wiki never mentions (`hl-gesture?`, `hl-gesture-action?`,
`hl-gesture-remove!` is mentioned but only in prose, `hl-*-id`
accessors, `hl-timer-cancel!`, `hl-workspace=?`, `hl-monitor-rule-add!=?`).

### 2.3 Debug leftovers writing to `/tmp` on hot paths (HIGH)

- `SchemeManager.cpp:203-210` `watchdogEnter()` opens
  `/tmp/hs-wd-probe` in append mode and writes a line on **every**
  callback, timer tick, bind press, event and eval. That is an
  `open/write/close` syscall triple per callback, an unbounded
  world-readable file in `/tmp` that grows for the life of the session,
  and a predictable-path `/tmp` write (a classic symlink hazard).
- `SchemeManager.cpp:764-768` `hlSchemeWindowClass()` does the same to
  `/tmp/hs-sel-debug` on every `hl-window-class` call — and resolves the
  window twice.

*Fix:* delete both blocks. Consider a `HYPRSCHEME_TRACE` env var gating
any future tracing, routed through the logger (§4.5).

### 2.4 Handle type confusion can crash the compositor (HIGH)

The Guile compat `define-record-type` (`hyprscheme-compat-guile.scm:189-229`)
expands accessors to `(define (acc x) (struct-ref x idx))` — **no vtable
check**. Chez's record accessors type-check; these don't. Consequences:

- `(hl-window-title (hl-active-workspace))` — a plain user typo — calls
  `hl-window-cell` on an `hl-workspace` struct, gets the workspace
  handle's C++ address, and `windowFromId()`
  (`SchemeManager.cpp:699-701`) `reinterpret_cast`s an
  `SHandle<PHLWORKSPACEREF>` to `SHandle<PHLWINDOWREF>`, locks it, and
  dereferences a `CWorkspace` as a `CWindow`. UB; in practice a segfault
  inside the compositor.
- `make-hl-window`, `hl-window-cell` etc. are ordinary public bindings
  in the user's environment, so `(hl-window-title (make-hl-window (cons
  12345 0)))` dereferences address 12345.
- `hl-window-id` and friends are documented public API ("rarely
  needed") and expose the raw heap address.

The C++ side has *no* way to validate an incoming handle: it is a raw
pointer, and "no registry" was a deliberate decision. But a registry of
*handles* (not of compositor objects) does not conflict with that
decision: an `std::unordered_set<IHandle*>` of live handles, inserted at
mint and erased at `hlHandleFree`, gives an O(1) validity check plus a
`kind` tag on `IHandle` for type checks.

*Fix options, cheapest first:*
1. Type-checked accessors in the compat record macro (`(unless (and
   (struct? x) (eq? (struct-vtable x) name)) (scm-error …))`). Cheap,
   catches the typo class.
2. `IHandle::kind()` + a live-handle set on the C++ side; every
   `*FromId()` validates and returns null on mismatch. Catches forged
   integers too.
3. The Guile-native answer (§4.3): handles become **foreign object
   types** (`make-foreign-object-type`) with finalizers. `hl-window?` is
   then a real type predicate, accessors are type-checked by Guile,
   the guardian/cell/drain machinery disappears, and the raw address
   never becomes a Scheme-visible integer at all.

### 2.5 Custom layouts ignore the work-area origin (HIGH for multi-monitor)

`SchemeLayout.cpp:267-276` and `:285-295` pass `AREA.w, AREA.h` to
Scheme and `applyBoxes()` (`:298-307`) calls
`targets[i]->setPositionGlobal(boxes[i])` with the boxes exactly as
returned. Every documented example (README, `custom-layouts.md`,
`examples/hyprland.scm`) returns boxes starting at `(0 0 …)`. On a
monitor whose work area does not start at the global origin — any
second monitor, or any monitor with a top bar reserving space — the
windows land at the wrong place. The test harness is a single nested
output at (0,0), so it never sees this.

*Fix:* pass `x y` (and ideally the reserved area / gaps) to recalculate
and resize, or add the offset in `applyBoxes`. Either is a behaviour
change for existing configs; the "add the offset C++-side" variant is
backward compatible (boxes stay work-area-relative, as the wiki already
says "in the work area"). See §5 for the broader layout-API question.

Also in this file: layout callbacks are the only Scheme entry that is
not wrapped in `watchdogEnter/Exit` (`callScheme1`, `:54-56`); and the
`schemeIntList` helper is a copy of the one in `SchemeManager.cpp`.

### 2.6 Stale wiki examples (MEDIUM — the doc test is probably red)

Found by grepping the wiki for names not defined in the bootstrap:

- `submaps.md:31-38`: `hl-window-resize` (now `hl-window-size-set!`)
  and the pre-keyword `'repeat #t` option form. The block is a
  ```` ```scheme ```` fence, so `t-zzz-docs` should evaluate it; on the
  current `define*` signature `'repeat #t` raises
  `keyword-argument-error "Invalid keyword"`. Either the suite has not
  been run since commit `15d6c55`, or the block is skipped for a reason
  I could not find. **Run the suite.**
- `code-snippets.md:214-219`: `hl-window-into-group` /
  `hl-window-out-of-group` (now `hl-window-group-move-in!` /
  `-move-out!`) with the rejected direction spellings `"l"/"d"/"u"/"r"`.
- `bind-globals.md:40`: `(hl-global …)` → `hl-global!`.
- `binds.md:141`: `(hl-window-cycle)` → `hl-window-cycle!`.
- `events.md:14-16`: the second example passes a workspace **handle** to
  `string-append` as if it were a name — it registers fine and would
  error at fire time, which the doc test never exercises.
- `naming-conventions.md:13`: still describes options as
  `'release #t` pairs.
- `Home.md`, `core.md`: say "Chez Scheme" throughout; `core.md` claims
  callback errors are "shown as a notification" (they are not — only
  layout errors notify; `hl--report` writes to fd 2 only) and that
  `start-hyprland -- --config …/hyprland.scm` works (TODO.txt already
  established that `--config` selects the *Lua* file).
- `Home.md` pins "commit `c26dbf93`" — fine, but nothing checks it
  against `Makefile:21` `HYPR_COMMIT` and `packaging/PKGBUILD`.

The reason these survive the doc test: a ```` ```scheme ```` block is
evaluated, but the lambdas *inside* it (bind thunks, handlers) are
never called, so an unbound name inside a thunk is invisible to the
interpreter. §8 proposes fixes.

### 2.7 Two `plugin-main.cpp`s; the tracked one in `src/` is dead (MEDIUM)

`Makefile:77` builds `src/plugin-main.o` from the **root**
`plugin-main.cpp`. `src/plugin-main.cpp` is tracked, differs (`"Chez
Scheme scripting for Hyprland"`), and is never compiled. Both carry a
`schemeCrashHandler` that duplicates the one in `SchemeManager.cpp:4871`.
Keep one file, in `src/`, without the duplicate handler.

Related housekeeping that git tracks: `emacs-keys.txt~`, `rename.txt~`
(the `.gitignore` `*~` rule post-dates them), `rename.txt`,
`emacs-keys.txt`, `build-chez.sh` (Chez), `TODO2.txt` (superseded by
DONE.txt/TODO.txt). Untracked strays in the tree: `#SchemeManager.cpp#`,
`TODO2.txt~`, `.gitignore~`, `rename.txt~`, `.o` files, two `.so`s.

### 2.8 Dead and Chez-only code in the live C++ (MEDIUM)

- `SchemeManager.cpp:243-280` `fireSchemeBind(int)` calls
  `hl--bind-fire`, which no longer exists in the prelude (the id path
  was deleted in the "RECORD CALLBACKS EVERYWHERE" batch). Unused.
- The `SBindResult` plist walk is copy-pasted three times
  (`:255-279`, `:396-418`, `:564-585`). One `bindResultFromPlist()`.
- `SchemeManager.cpp:5412-5432`: boot-file discovery with a hardcoded
  `"/home/chris/GITE/chez-pic"`, `registerBootFile`, `buildHeap` — all
  no-ops on Guile. `SchemeHost.hpp:106-108` still declares them.
- `schemeIntList`'s lock-every-cons dance (`:285-296`) and the
  `marshRoot`/`marshRelease` pattern exist for Chez's *moving*
  collector. Boehm does not move, and the C++ locals are conservatively
  scanned anyway. With gsubrs (§4.2) SCM values are simply live on the
  C stack and none of this is needed.
- Comments: the file header (`:107-130`) still describes a three-phase
  load including `hyprscheme-defun.scm`; `SchemeManager.hpp` says "Embeds
  the Chez interpreter"; `hyprscheme-compat-guile.scm:3-4` says the
  compat file loads "between the prelude and the defun machinery" (it
  loads *first*); dozens of "Chez's GC runs at allocation points"
  notes. Roughly a fifth of the comments in the tree describe a runtime
  that is no longer there.
- `plugin-main.cpp` declares `PLUGIN_INIT` description via
  `SchemeHost::backendName()` — fine — but `SchemeManager.hpp` and the
  `README.md` still say Chez (README: "Chez Scheme scripting", "statically
  embeds a position-independent Chez Scheme kernel", `CHEZ_DIR`,
  `BUILDCHEZ.md`, `make guild`…). The README is a Chez README.
- `startWatchdog()` (`:216-238`) detaches a thread that is never
  stopped; `shutdown()` does not clear `g_watchdogRun`.

### 2.9 `errorf` throws away `who` (MEDIUM — hurts every error message)

`hyprscheme-compat-guile.scm:30-31`:
`(define (errorf who fmt . args) (error (apply format #f fmt args)))`.
Every one of the ~90 `errorf` sites passes a function name that is then
discarded, so the user sees `error: ~a must be a list of modifier
tokens` with no indication of which of the five functions said it.
Guile's `(error who fmt . args)` convention, or better
`(raise-exception (make-exception (make-error) (make-exception-with-origin who) (make-exception-with-message …) (make-exception-with-irritants …)))`,
keeps it. Also `display-condition` (`:80-94`) should print the origin.

### 2.10 Compat-layer shadows leak Chez-isms into every user config (MEDIUM)

Because `copy-environment` copies every *local* binding of the working
module into each generation, the following redefinitions are what a
config author gets when they write ordinary Guile:

| binding | what the user gets | standard Guile |
|---|---|---|
| `define-record-type` | Chez `(fields …)` shorthand **only**; a SRFI-9 / R6RS record definition is a syntax error | SRFI-9 form |
| `format` | Chez arity: a string first arg is the template | `(format #f …)`; a string destination is an error in 3.0 |
| `load` | evaluates into the generation via `read`+`eval` (fine) but loses `%load-path` search, `load-compiled`, source props | `primitive-load` semantics |
| `eval` | 1-arg form allowed | 2-arg |
| `current-error-port` | a plain procedure returning fd 2; `with-error-to-port`/`parameterize` silently ignored | a parameter |
| `void`, `exact`, `errorf`, `andmap`, `list*`, `collect`, `exists`, `for-all`, `time-difference`, `time-nanosecond`, `top-level-value`, `copy-environment`, `collect-request-handler`, `set-timer`, `timer-interrupt-handler` | Chez names polluting the namespace | — |

`(define (current-error-port) …)` in particular should be
`(set-current-error-port hl--stderr-port)`. The real fix is §4.1:
stop emulating Chez.

### 2.11 Inconsistent argument conventions inside the API (MEDIUM, style)

Two option styles coexist. `#:key` (the house rule now):
`hl-bind-add!`, all `-set!` toggles (`#:on?`), rules, `hl-notify!`,
`hl-gesture-add!`, `hl-layers`, `hl-animation-add!`, `hl-device-add!`,
`hl-exec!`. Positional rest-arg symbols (the old style):
`hl-window-cycle! . opt` (`'prev 'tiled 'floating`), `hl-window-size-set!
w width height . opt` (`'relative`/`'rel`), `hl-window-position-set!`,
`hl-window-swap-next! . opt`, `hl-group-cycle! . opt`,
`hl-group-window-move-next! . opt`, `hl-window-send-shortcut! mods key
. w`, `hl-window-send-key-state!`, `hl-submap name fn . reset`,
`hl-make-float-gesture . mode`, `hl-make-fullscreen-gesture . mode`,
`hl-make-cursor-zoom-gesture zoom . mode`, `hl-curve-add! name type .
vals`, `hl-state-ref k . default`, `hl-group-add! g window . index`,
`hl-window-fullscreen-state w internal client . layout-aware`.

Other inconsistencies worth a single sweep:

- **Window-or-`#f` acceptance.** `hl--wid` (accepts `#f` = active) vs
  `hl-window-id` (does not). `hl-window-group?` uses `hl-window-id`;
  `hl-window-deny-from-group?`, `hl-window-pseudo?`, `hl-window-maximized?`
  use `hl--wid`. Same family, different rules. The docstrings mostly
  don't say which.
- **Mutators without `!`:** `hl-window-fullscreen-state`,
  `hl-exec-scheduled-prop-refresh-immediately`, `hl-timer-set-timeout`
  (should be `hl-timer-timeout-set!` per the `-set!` convention),
  `hl-layout-add!` fine, `hl-submap` (registers binds — arguably a
  definer, like `define`, so OK).
- **Predicates without `?`:** `hl-is-key-down` → `hl-key-down?`.
- **Misnamed action:** `hl-focus-direction-set!` is a *move*, not a
  setter — `hl-focus-move!` or `hl-focus-direction!`.
- **`hl-layout-msg`** (send) vs the `'layout-msg` callback key vs the
  C++ `hl-scheme-layout-message`. Fine, but `hl-layout-message!` would
  match the `!` rule.
- **`hl-notify!`** takes `text duration` positionally; `hl-notification-add!`
  takes `#:text #:timeout` — the same concept, two shapes, and the
  latter accepts `timeout`/`duration`/`time` aliases C++-side
  (`SchemeManager.cpp:3725`) that no docstring mentions.
- **`hl-notification-remove!` / `hl-notification-active?`** operate on
  *event subscription* records (`hl-event`), while the `hl-notification-*`
  family otherwise means the on-screen bubble (`hl-notification-add!`,
  `-text`, `-dismiss!` …). A reader seeing `hl-notification-remove!`
  next to `hl-notification-dismiss!` cannot tell which object each
  takes. The event-registration family was renamed to
  `hl-X-notification-add!` at Chris's request, so this is flagged, not
  prescribed — but the two operations on `hl-event` records should at
  least be `hl-event-remove!` / `hl-event-active?` (the C++ symbols
  already are `hl-scheme-event-cancel`/`-active`). §6.3 has the
  Emacs-flavoured alternative (hooks).
- **`hl-gesture-add!` returns a bare 5-element list** and `hl-gesture?`
  is a structural check (`bootstrap:2001-2004`); `hl-gesture-action?`
  likewise checks "pair whose car is a positive integer". Every other
  handle is an opaque record. Both should be records (the gesture-action
  car is a C++ singleton *address* — a `#:print` of it leaks a pointer).
- **Directions.** `hl--dir` passes strings through and `actionDir()`
  (`SchemeManager.cpp:1323`) takes the first character, so `"l"`, `"lol"`,
  `"left"`, `'left` all work. CLAUDE.md says only documented spellings
  are accepted; the code disagrees. Validate to `'(left right up down)`
  in Scheme.
- **Monitor resolution.** `monitorFromName()` (`:1327`) matches `m_name`
  only and is used by focus-monitor, dpms, swap-monitors,
  move-to-monitor; `hlSchemeMonitorSetSpecial` and `hlMonitorFrom` use
  `query().configString()` (accepts `desc:`). So
  `(hl-monitor-focus! "desc:…")` fails while `(hl-monitor-from "desc:…")`
  works. Use the query everywhere (and see §5 for relative selectors).
- **`hl-window-cycle!`** hardcodes window `-1`; the C++ takes an id.

### 2.12 Smaller things

- `hyprscheme-compat-guile.scm:132-139`: the after-gc hook runs
  `hl--drain-handles!` which calls `c-hl-handle-free` through the
  `foreign-procedure` shim — that shim allocates (`map`, closure,
  `call-with-blocked-asyncs`). The "allocation-free by design" comment
  in the bootstrap (`:2570-2572`) is Chez-era; on Guile it is fine
  because `after-gc-hook` runs as an async at a safe point, not inside
  the collector. Update the comment or, per §4.3, delete the mechanism.
- `hl--fire-list` (prelude `:169-171`) and `hl--fire-list-rec` return
  `#f` on error but callers ignore the value; the other fire helpers
  return "not aborted". Harmless, inconsistent.
- `hlWindowFrom`, `hlUrgentWindow`, `hlLastWindow`,
  `hlSchemeActiveWindowId` return handle addresses as **`double`**
  (48-bit pointers fit in 53 bits — true today, and a landmine). Every
  other mint returns `SchemeValue integer`.
- `windowMatchesSelector()` (`:2991-3034`) is a hand-rolled selector
  parser that compiles a `std::regex` per window per call
  (`hl-windows-from` over N windows = N regex compiles) and only knows
  `class: initialclass: title: initialtitle: pid: address: tag: floating
  tiled active`. Upstream has a selector engine the Lua bindings use;
  this one will drift from it. (The parity agent's report, §5, covers
  which selector spellings are missing.)
- `stringVal()` uses `scm_from_locale_string`; `stringUtf8` is used for
  titles (correct). `globalRef` names and paths go through the locale
  variant — fine unless the compositor runs with `LC_ALL=C` and a
  non-ASCII config path.
- `hlSchemeExec` returns `int` pid; pids fit, but the documented "returns
  the new pid" also returns `-1 → error` via `errorf` — fine.
- `hl-window-swap-next!`'s docstring says "'prev or #t swaps backwards"
  and the code maps anything truthy to `prev` — `(hl-window-swap-next! w
  'next)` swaps *backwards*.
- inotify (`SchemeManager.cpp:4791-4838`) watches the config's parent
  directory and only reloads when the event name matches the config
  basename. So: a config that is a **symlink** (dotfiles managers) never
  reloads when the target changes; edits to `(load …)`-included files
  never reload. Watch the realpath's directory and any `.scm` there, or
  the set of files the last load touched.
- `registerIpc()`'s handler does `req.command.substr(find_first_of(' ')
  + 1)` — `hyprctl scheme` with no argument yields the whole string
  `"scheme"` evaluated as a symbol → "Unbound variable: scheme". Guard.
- The eval channel is arbitrary code execution for anything that can
  reach the hyprctl socket. That is the same trust boundary as
  `hyprctl dispatch exec`, so not a new hole — but `core.md` should say
  so plainly ("your config can execute arbitrary code" is there; "so can
  anything with access to the socket" is not).
- `Makefile`: no `-Wall -Wextra`, no dependency tracking (`-MMD -MP`),
  so a header edit does not rebuild dependents; `-O2 -g` fine.
  `pkg-config lua55` is hardwired. No target compiles the `.scm`
  (§4.6). No `check` target running `tests/run.sh`.
- `tests/run.sh` `cleanup()` removes `$XDG_RUNTIME_DIR/hypr/$SIG` for
  the *nested* instance — consistent with the CLAUDE.md rule since it
  is the new dir only, but worth a comment given how loudly the rule is
  stated.

---------------------------------------------------------------------

## 3. Architecture assessment

### 3.1 Layering as it stands

```
user config (generation module, a copy of the working module)
  └─ hyprscheme-bootstrap.scm   304 public hl-* defines, 299 foreign-procedure decls
       └─ hyprscheme-prelude.scm  guards, watchdog, fire trampolines, layout entries
            └─ hyprscheme-compat-guile.scm   Chez emulation + foreign-procedure over libffi
                 └─ SchemeHostGuile.cpp   ~20 ops; scm_from_pointer(fn) per registered symbol
                      └─ SchemeManager.cpp (5.4k) + SchemeLayout.cpp   ~300 static C functions
                           └─ Hyprland internals (Config::Actions, State::*, Event::bus, …)
```

Two observations:

1. **Everything Scheme-facing is written in Chez dialect and then
   translated.** The compat layer exists to make Guile look like Chez so
   the prelude/bootstrap needn't change. That was the right *migration*
   strategy. It is the wrong *steady state*: it costs a libffi call plus
   a `call-with-blocked-asyncs` plus `map` per API call, it leaks
   Chez-isms into user configs (§2.10), it forbids compiling (the shim
   resolves function pointers by name at load time), and it keeps ~600
   lines of emulation alive that nobody wants to maintain.

2. **The C++ is ~300 near-identical stubs.** Each: `if (!g_up) return
   …; resolve handle; if (!obj) return False; do the thing; wrap`. With
   gsubrs and a small template layer, most of `SchemeManager.cpp`
   collapses to one-liners, and the 299 `(define c-hl-… (foreign-procedure
   …))` lines in the bootstrap disappear entirely (a gsubr *is* a Scheme
   procedure). GUILE-CONVERSION.txt §2 already identified "ALL 298 as
   gsubrs" as the uniform alternative; it is the natural step 4.

### 3.2 What the generation model actually is

`hl--reset` → `copy-environment` → `make-fresh-user-module` +
`module-for-each` copying **every** local binding (all ~1000 of them:
the API, the `c-hl-*` foreign wrappers, the compat shims, the record
constructors, the internal helpers) into a fresh module, then
`module-use!` of the source's interfaces. Per reload. It works, but:

- it is O(bindings) per reload and copies things users should never see
  (`c-hl-window-title`, `hl--foreign-mk`, `make-hl-window`);
- it exists to make `set!` of `hl--watchdog-ms` land in the generation —
  a single variable that should be a Guile *parameter* or a setter
  `(hl-watchdog-ms-set! n)`;
- the idiomatic Guile shape is: the API is a **module**
  `(hyprscheme)` (or several) with an explicit export list; a generation
  is `(make-fresh-user-module)` + `(module-use! gen (resolve-interface
  '(hyprscheme)))`. Imports are separate from locals, the export list
  *is* the public API (so `t-coverage` reads it, the docs generator
  reads it, the REPL's `,apropos` reads it), and a user `define` shadows
  cleanly.

### 3.3 The handle model, physically

A handle is `new SHandle<WP<T>>` → address → `double`/`int64` across
the FFI → `(cons addr 0)` → guardian-registered cell inside a record →
guardian yields the cell after GC → `after-gc-hook` → `hl--drain-handles!`
→ `c-hl-handle-free` → `delete`. Six hops and three mechanisms
(guardian, hook, FFI) for what Guile does natively with **foreign
object types with finalizers** (`(system foreign-object)`; or SMOBs):
`scm_make_foreign_object_type` with a finalizer that runs `delete`;
the object *is* the `SCM`; `hl-window?` is `scm_is_a_p`; the address is
never a Scheme-visible number. Chez needed the guardian because it has
no finalizers on records; Guile does not. This also fixes §2.4.

### 3.4 Error reporting

`hl--report` prints `"[scheme] error: " + message` to fd 2 and nothing
else. No backtrace (the `guard` unwinds before anything can capture the
stack), no source file:line (forms are `read` then `eval`'d one at a
time — Guile keeps source properties on the pairs, but nothing prints
them), no notification (Lua pops "Runtime error in lua: …" for runtime
errors and an error overlay with the error list for config-load errors,
`ConfigManager.cpp:879`, `:786-810`), and fd 2 is not `hyprland.log`, so
`hyprctl rollinglog` never shows it. For a user, a broken callback is
silent unless they know to look at the session's stderr. This is the
biggest *usability* gap in the project and it is cheap to fix (§4.5).

### 3.5 Performance posture

Nothing is compiled: `GUILE_AUTO_COMPILE=0` and `primitive-load` mean the
2.5k-line bootstrap and every user config run in Guile's *interpreter*
(`ice-9/eval.scm`), which is roughly an order of magnitude slower than
compiled code, and every FFI call adds libffi marshalling. It does not
matter for a keybind; it matters for `input.keyboard.key` handlers, live
gesture `update` callbacks (per pointer event), layout recalculates, and
startup time. Compiling the machinery to `.go` at build time
(`guild compile`) and loading with `load-compiled` fixes the machinery
half; user configs can be compiled on load with
`(compile-file …)` into a cache dir the plugin owns (not `~/.cache/guile`
— the deterministic-startup concern in `SchemeHostGuile.cpp:206-208`
is right, but it argues for a controlled cache, not for interpreting).

---------------------------------------------------------------------

## 4. The Guile-native rewrite (the big plan)

This is one coherent programme, ordered so each step leaves the suite
green and is committable on its own. Estimated at "mechanical days" per
step, like the Chez→Guile steps were; the hard thinking was done in
GUILE-CONVERSION.txt.

### 4.1 Step A — invert the compat layer

Goal: `hyprscheme-compat-guile.scm` is deleted; prelude and bootstrap
are plain Guile.

- Replace `errorf` with a house `hl-error` that keeps `who` (§2.9).
- Replace the Chez `define-record-type` shorthand with SRFI-9
  `define-record-type` (or R6RS `(rnrs records syntactic)`) with an
  opaque printer (`set-record-type-printer!` → `#<hl-window 0x…>` that
  prints class/title when alive — a small usability win in the REPL).
- Replace `format` calls with `(format #f …)` / `simple-format`.
- Replace `guard`+`display-condition` with `with-exception-handler`
  (`#:unwind? #f` for the backtrace, §4.5).
- `hl--watchdog-ms` → a parameter `hl-watchdog-ms` (settable with
  `(hl-watchdog-ms 8000)` from the config; parameters are per-thread and
  need no generation lookup — `top-level-value` goes away).
- `set-current-error-port` instead of the shadow.
- Watchdog stays as sigaction+asyncs (it is already Guile-native); move
  it to the prelude proper.
- `load`/`eval` shadows: replaced by §4.4.

### 4.2 Step B — gsubrs instead of `foreign-procedure`

Goal: the 299 `(define c-hl-… (foreign-procedure …))` lines and the
libffi shim are gone; each C++ entry point is `scm_c_define_gsubr` taking
and returning `SCM`.

Sketch of the C++ side (this is what "`SchemeManager.cpp` collapses"
means):

```cpp
// one-time helpers
template <class T> SCM handle(SP<T>);                 // mint a foreign object
template <class T> SP<T> from(SCM, const char* who);   // type-checked unwrap, raises Scheme error on mismatch
SCM str(const std::string&); std::string str(SCM);
// a getter is now one line
DEFINE_GSUBR("hl-window-title", 1, (SCM w) { return str(from<CWindow>(w, "hl-window-title")->metadata().title()); })
```

Consequences: `SchemeValue`, `word()`, `truthy()`, `hl--scm->word`,
`hl--word->scm`, `marshRoot`/`marshRelease`, `schemeIntList`'s locking,
the `(scheme-object)` typing, the `int`-vs-`double` pointer return
confusion (§2.12), and `call-with-blocked-asyncs` per call all go away
(asyncs are not delivered inside a gsubr's C code — a gsubr is C, the
"foreign calls are not interruptible" contract is native). `SThunkRef`
stays (as `scm_gc_protect_object` RAII) — it is the right tool for
callbacks captured in C++ lambdas.

The Scheme-side wrappers then shrink too: `hl-window-title` can *be* the
gsubr (docstring set from C++ via `scm_set_procedure_property_x` with
`'documentation`, or by keeping a thin Scheme `define` with the docstring
— the latter keeps docs in `.scm` where Chris can edit them; recommended).

### 4.3 Step C — foreign object types for handles

Goal: `hl-window`, `hl-workspace`, `hl-monitor`, `hl-group`, `hl-layer`,
`hl-notification` are Guile foreign object types with finalizers; the
guardian, the cell, `hl--drain-handles!`, `after-gc-hook`,
`c-hl-handle-free`, `hl-*-id` (as public API) and §2.4 all disappear.
`hl-bind`, `hl-timer`, `hl-event`, `hl-rule` stay as Scheme records
(they carry Scheme data; nothing C++ needs to finalize).

One subtlety: finalizers run from Guile's finalizer thread by default
(`scm_set_automatic_finalization_enabled`); the `delete` of a `WP<T>`
touches Hyprland's weak-pointer control block, which is not thread-safe.
Disable automatic finalization and call `scm_run_finalizers()` from the
event loop (a `doLater` after each callback, or a low-frequency timer) —
the same shape as today's after-gc drain, one line instead of forty.

### 4.4 Step D — modules instead of `copy-environment`

Goal: `(define-module (hyprscheme) #:export (hl-bind-add! …))` (possibly
split into `(hyprscheme window)`, `(hyprscheme workspace)`, …, re-exported
by `(hyprscheme)`); a generation is a fresh user module that uses
`(hyprscheme)`; `hl--load` is `(save-module-excursion (λ ()
(set-current-module gen) (primitive-load path)))` — which gives source
properties and lets `use-modules` in configs work naturally. User
`(load "keybinds.scm")` then just works (relative to `%load-path`, which
the plugin should seed with the config directory).

Persistent state (`hl-state`) lives in the `(hyprscheme)` module, so it
survives generations exactly as now. The export list becomes the single
source of truth for "public API" — consumed by `t-coverage`, the docs
generator (§7.4) and `,apropos`.

### 4.5 Step E — errors a human can find and read

- Capture the backtrace at the point of raise: fire trampolines use
  `with-exception-handler … #:unwind? #f` and render
  `(display-backtrace (make-stack #t) port)` plus `exception-message`,
  `exception-origin` (`who`) and, for config-load errors, the source
  file:line from the form's source properties.
- Route the report to **all three** of: the compositor log (through a
  gsubr that calls `LOG()` — see the logger note below), a Hyprland
  notification (`Notification::overlay()->addNotification`, as Lua does
  for runtime errors, rate-limited so a broken repeating timer does not
  spam), and the hyprctl reply when the error came from `hyprctl
  scheme`.
- Config-load errors should additionally go through the error overlay
  like Lua's (`ErrorOverlay::overlay()->queueCreate`), so a broken
  `hyprland.scm` is visible at login.
- The "plugin LOG() is swallowed" issue (CLAUDE.md): `Log::logger` is an
  `inline UP<CLogger>` (`Hyprland/src/debug/log/Logger.hpp:69`), so the
  plugin gets its own instance unless the symbol is resolved against the
  executable. Two ways out without touching upstream: `dlsym(RTLD_DEFAULT,
  <mangled name of Log::logger>)` at init and log through that instance;
  or write a `hyprscheme.log` next to `hyprland.log` in the instance dir
  and tell users. The first is a one-liner worth trying.
- `hl--eval` should capture `current-output-port` during the eval and
  return `output + value`, so `(display …)` in `hyprctl scheme` shows
  up. Today it vanishes into the compositor's stdout.

### 4.6 Step F — compile and lint

- `make` runs `guild compile -Wunbound-variable -Warity-mismatch
  -Wformat -o build/hyprscheme.go src/config/scheme/hyprscheme.scm`
  (after §4.4 the machinery is a module, so this is straightforward).
  Unbound-variable warnings **fail the build** — that is the lint that
  catches `hl-monitor=?` disappearing, a wiki example calling
  `hl-window-resize`, a renamed helper, a typo in a rarely-run branch.
- The plugin loads `.go` via `load-compiled` with the `.scm` as fallback
  (dev override via `HYPRSCHEME_SCM_DIR` as now).
- Same tool over the wiki blocks and `examples/hyprland.scm` (with the
  `(hyprscheme)` module stubbed — `tools/load-check.scm` already does a
  version of this by stubbing `foreign-procedure`; after §4.2 the stub
  is "a module of no-op gsubrs", cleaner).
- User configs: compile on load into `$XDG_CACHE_HOME/hyprscheme/`
  (optional; interpret if compile fails; report warnings as config
  warnings). This also gives users `-Wunbound-variable` on *their*
  config at reload — the "hallucinated function" class of error becomes
  a load-time warning with a line number instead of a silent bind that
  does nothing when pressed.

### 4.7 Step G — split `SchemeManager.cpp`

After B the file is mostly one-liners, but 5.4k lines in one TU is still
hostile. Split by object family, matching the wiki's page structure and
the module split in D: `Window.cpp`, `Workspace.cpp`, `Monitor.cpp`,
`Group.cpp`, `Layer.cpp`, `Bind.cpp`, `Event.cpp`, `Timer.cpp`,
`Rule.cpp`, `Config.cpp` (options, devices, monitors, animations,
permissions), `Notification.cpp`, `Gesture.cpp`, `Exec.cpp`,
`Layout.cpp`, `Host.cpp` (init/reload/ipc/watch/shutdown). One header
with the shared helpers from §4.2. The registration list
(`attachInterp`, `:4953-5270`) becomes a per-file `registerX()`.

---------------------------------------------------------------------

## 5. Parity with Hyprland's Lua API

Method: a function-by-function audit of
`Hyprland/src/config/lua/{bindings,objects,layout}/*.cpp`,
`LuaEventHandler.cpp` and `ConfigManager.cpp` against the bootstrap.
The full table is the addendum at the end of this file. Headline:

**Parity is genuinely close to complete.** All 34 built-in `hl.on`
events are wired; every `LuaWindow`, `LuaWorkspace`, `LuaLayerSurface`,
`LuaGroup`, `LuaTimer`, `LuaNotification`, `Lua*Rule` field/method has
a twin (LuaMonitor is two short); every `hl.dsp.*` dispatcher has an
`hl-*!`; config/rules/devices/monitors/animations/permissions/gestures
all map. The earlier TODO/DONE claims of "full parity" are close to
true. What remains falls into six groups, ranked:

1. **Selector strings for focus/move** — the largest real gap. Lua's
   `hl.focus{workspace="+1"}`, `"e+1"`, `"m-1"`, `"r+1"`, `"empty"`,
   `"previous"`, `"name:x"`, `"special:x"`, monitor `"+1"`/`"l"`/`"r"`/
   `"current"`, and `hl.focus{workspace=…, on_current_monitor=true}` go
   through upstream's `resolveWorkspaceStr` / `monitorState()->query()
   .relativeTo(…).configString(…)`. Scheme's `hl-workspace-focus!`,
   `hl-monitor-focus!`, `hl-workspace-monitor-set!`, `hl-monitor-swap!`
   resolve by *name only* (`monitorFromName`, `SchemeManager.cpp:1327`;
   `hl--ws-arg` passes strings through to `changeWorkspace`, which does
   accept some of these — the monitor side does not). Also missing:
   `hl.get_workspace(sel)` (no workspace-by-selector query),
   `hl.dsp.window.swap` with a *target selector*, `hl.dsp.workspace.move`
   with the current workspace implied.
   *Plan:* one C++ helper pair `workspaceFromSelectorOrHandle` /
   `monitorFromSelectorOrHandle` that calls exactly what
   `LuaBindingsInternal.cpp:236-303` calls, used by every
   workspace/monitor-taking action; add `hl-workspace-from`; document
   the selector grammar once in `naming-conventions.md` / `windows.md`.
2. **Safety/visibility parity**: no emergency mode, no runtime-error
   notification, no traceback (§3.4, §4.5). Lua: `guardedPCall` with
   per-site budgets, `luaL_traceback`, "Runtime error in lua" bubble,
   emergency binds when errors + empty registry.
3. **Keybind introspection and enable/disable.** `LuaKeybind` exposes
   ~24 read-only fields (`display_key`, `description`, `submap`, every
   flag, `devices`…) plus `set_enabled`/`is_enabled`. Scheme's `hl-bind`
   record has `tokens` and `thunk` only, and there is no
   `hl-bind-enabled-set!`. The bind is findable C++-side by its tag, so
   `hl-bind-enabled-set!`/`hl-bind-enabled?`/`hl-bind-description`/
   `hl-bind-flags` are small additions; they also feed `hl-describe-key`
   (§6.3).
4. **`hl.env(k, v)`** (environment export to spawned clients) and
   **`hl.plugin.load(path)`** — no Scheme equivalent at all. `hl-env!` is
   a one-line wrapper around whatever `hl.env` calls; plugin loading from
   Scheme is a judgement call (the Lua config must load *this* plugin
   anyway).
5. **Plugin custom events.** Lua's `hl.on` also accepts names from
   `Event::bus()->m_events.plugin` (`LuaEventHandler.cpp:264-300`),
   marshalling bool/int/double/string/window/workspace/layer/monitor
   payloads. Scheme has no dynamic event-name subscription, so
   third-party plugin events are unreachable. Needs one generic
   `hl-plugin-event-notification-add! "name" fn` with a payload
   marshaller — or the hooks design of §6.3, where a hook per known
   name is created on demand.
6. **Query filters.** `hl.get_windows{monitor, workspace, floating,
   mapped, className, title, tag}`; `hl-windows` takes no filters (it
   does match Lua's mapped-only default). `hl-windows-from` covers
   class/title/tag/floating via selector strings; monitor/workspace
   filters are missing. `#:key` filters on `hl-windows` is the natural
   shape (`(hl-windows #:workspace ws #:floating #t)`), replacing the
   hand-rolled selector parser of §2.12 with the upstream query.
7. **Minor:** `hl.unbind("all")`; `hl.dsp.no_op`; monitor `cm` field and
   `set_workspace` method; `group:remove(index)` (Scheme takes a window
   only); `hl.dispatch(dispatcher)` — Lua's dispatchers are first-class
   *values* you can store and dispatch later; Scheme's are immediate
   calls, which is fine (a thunk is the value) but worth one sentence
   in `dispatchers.md`. Two dropped payload fields: `window.active`'s
   focus `reason` and `window.move_to_workspace`'s target workspace
   (`SchemeManager.cpp:4512-4513`). No reentrancy guard on event
   dispatch (Lua caps at depth 32).

And, in more detail because it is a design question rather than a
missing function:

- **Custom layouts** are the documented non-parity area
  (`custom-layouts.md`: "Unlike the Lua model, a Scheme layout does not
  receive a `ctx` object … no targets with `:place()`"). The Scheme
  layout gets `(count W H windows [dx dy corner])` and returns boxes. Lua
  layouts get a context with the work area (including its origin —
  §2.5), helpers, and target objects, plus callbacks for
  move/swap/next-candidate/predict-size that the Scheme provider
  implements C++-side with fixed behaviour (`SchemeLayout.cpp:161-209`).
  Whether to close this gap is a design decision (a pure-Scheme
  `(hyprscheme layout)` helper library — `split`, `grid`, `column`,
  `row` — costs nothing at runtime and would make the README example
  five lines; exposing move/swap/next-candidate as optional callbacks is
  a modest C++ change).
- **Monitor selectors**: `monitorFromName` only (§2.11) vs Lua's
  selector-or-object helper.
- **Error surfacing**: Lua notifies + overlays; Scheme logs to fd 2 (§3.4).
- **Emergency mode**: `ConfigManager.cpp:781` trips when the Lua config
  has errors *and* no binds exist. A Scheme user's Lua config is one
  line and never errors, so a broken `hyprland.scm` that registers zero
  binds leaves the user with **no binds and no emergency binds**. The
  plugin should implement its own emergency fallback (SUPER+Q terminal,
  SUPER+R launcher, SUPER+M exit, plus SUPER+Shift+R "reload scheme")
  when a load fails and the registry is empty — that is a real safety
  gap versus Lua.
- **`print`**: Lua rebinds `print` to `LOG(INFO, "[Lua] …")`. Scheme's
  `display` goes to the compositor's stdout. A `hl-log` (and routing
  `current-output-port` in callbacks to the logger) is the equivalent.

---------------------------------------------------------------------

## 6. More Scheme-like, more Guile-like, more Emacs-like, easier

### 6.1 Scheme-like

- Records for every handle (§2.11 gestures), `?` for predicates, `!`
  for every mutator, one option style (`#:key`), no symbol-flag rest
  args. One pass, one commit, one wiki sweep.
- `(hl-window-title w)` returning `#f` for a dead handle is fine, but
  actions returning `#t`/`#f` *and* sometimes raising (`hl-bind-add!`
  raises; `hl-window-focus!` returns `#f`) is two error models. Pick:
  raise on programmer error (bad argument), return `#f` on runtime
  refusal (window gone). Document the rule in `naming-conventions.md`.
- Directions and modes as symbols only, validated (§2.11).
- Replace hand-rolled `hl--split-string`, `hl--string-index`,
  `hl--string-join`, `hl--trim` (prelude `:240-266`) with `(srfi
  srfi-13)`/`(ice-9 string-fun)` — Guile has them.
- Plist helpers: fine as internal machinery, but `hl-plist-get` as a
  *public* API is odd (it exists because gesture events arrive as
  plists). With `#:key` everywhere, consider delivering gesture events
  as records or applying them as keyword args
  (`(lambda* (#:key phase direction delta …))`) — then `hl-plist-get`
  can retire.

### 6.2 Guile-like

Everything in §4, plus:

- **Parameters** for tunables (`hl-watchdog-ms`), **hooks** for events
  (§6.3), **`(ice-9 match)`** in the machinery instead of
  `car`/`cadr`/`cddr` chains (`hl--layout-resize` is a `match` waiting to
  happen), **`define-syntax-rule`** for the 20 near-identical
  `hl-X-notification-add!` definers.
- **Docstrings in Texinfo/GFM** readable by `(ice-9 documentation)` —
  already there; add `@var{}`-style argument references consistently so
  a generated reference reads well.
- **`%load-path`** seeded with the config directory so `(load
  "keybinds.scm")` and `(use-modules (my hypr keys))` both work — the
  natural way for a big config to be split into modules, which is what
  Guile users will reach for first.
- **Debugging conveniences**: `,trace`, `,profile`, `,break` all work
  once there is a REPL connection (§7.1); nothing to build.
- **A `(hyprscheme)` module you can `use-modules` from a plain `guile`
  process** with every gsubr stubbed — for unit-testing pure config
  logic outside the compositor (the layout arithmetic, key parsing,
  state handling). Today `tools/load-check.scm` approximates this.

### 6.3 Emacs-like

Chris's stated design north star. Where Hyprscheme already is
Emacs-like: `hl-kbd`, docstrings + `describe`, submaps ≈ keymaps,
cross-reload state ≈ `defvar`. Where it could go:

- **Hooks.** Emacs and Guile agree here: `(add-hook! hl-window-open-hook
  (lambda (w) …))`, `(remove-hook! …)`, `(run-hook …)`. Guile has
  first-class hook objects (`make-hook`, `add-hook!`, `remove-hook!`,
  `hook->list`, `reset-hook!`). Twenty `hl-X-notification-add!`
  functions plus `hl-notification-remove!`/`-active?` become twenty
  hook *variables* and Guile's own four hook procedures; removal is
  `(remove-hook! hl-window-open-hook fn)` — identity by procedure, as in
  Emacs, no subscription record needed. Reload resets the hooks. The
  C++ side connects one bus listener per hook that calls `run-hook`.
  This is *the* most Emacs-flavoured change available and it deletes
  API surface rather than adding it. (Noted: the `-notification-add!`
  names were Chris's call in the record-callbacks batch; this is offered
  as the alternative he may not have had in front of him then.)
- **Keymaps.** `(hl-define-key hl-global-map (hl-kbd "s-q") thunk)`,
  `(hl-define-key resize-map …)`, `(hl-set-keymap! "resize")`. A keymap
  is a Scheme object holding binds; a submap is a keymap. `hl-bind-add!`
  stays as the primitive. Mostly a Scheme-side library.
- **`defcustom`.** CLAUDE.md records the decision: `:type :set :get`
  only, assign-if-unbound. `(hl-defcustom hl-terminal "foot" :type
  string :doc "…")` → a variable users can `(hl-customize-set!
  'hl-terminal "kitty")` and that `describe-variable` documents. Small,
  and it gives configs a documented options section. Ties into
  `hl-state` for persistence.
- **`describe-function` / `apropos` / `describe-key`.** Free with Guile
  + a REPL (`,describe hl-bind-add!`, `,apropos hl-window`). Add
  `(hl-describe-key (hl-kbd "s-q"))` → which bind, its description and
  the source location of its thunk (`program-source`) — Emacs's `C-h k`.
  The C++ side already stamps binds with the record address; the record
  can carry the source location captured at `hl-bind-add!` time.
- **Interactive commands.** `(hl-defcommand name docstring body)` →
  registers in a command table; `hl-execute-command` (bindable to a
  launcher key, fed by a fuzzy picker via `hl-exec!` of `fuzzel`/`wofi`
  on `(hl-commands)`) gives `M-x`. Cheap, fun, and a natural home for
  the layout-msg-style state toggles.
- **Live evaluation from the editor.** §7.1. `C-x C-e` on a form in
  `hyprland.scm` evaluates it in the running compositor. That is the
  Emacs experience.

### 6.4 Easier to use

- Errors you can see (§4.5) — the number-one usability item.
- An emergency keymap when the config fails (§5).
- `hyprctl scheme` returning printed output, not just the value (§4.5).
- Handle printing: `#<hl-window "foot" "~/src">` instead of `#<struct …>`.
- A `(hyprscheme layout)` helper library so a custom layout is arithmetic
  over `split`/`grid`, not a hand-rolled loop (§5).
- Reload on symlinked / included files (§2.12).
- A `make check` target; a `make lint` target (§4.6).
- README rewritten for Guile (it is still the Chez README).

### 6.5 Easier to program (for the maintainers)

- §4.2 + §4.7: the C++ becomes one-liners in small files.
- §4.6: the compiler finds the rename bugs.
- The `t-coverage` meta-test working again, reading the export list.
- One naming rule, written down, checked by a script (`hl-` public,
  `!` mutators, `?` predicates, `-set!`/`-add!`/`-remove!` pairs) — a
  20-line Guile script over the export list can enforce the mechanical
  parts.
- Delete the stale comments (§2.8) in one pass; a fifth of the comments
  in the tree currently mislead the next reader.

---------------------------------------------------------------------

## 7. External tools worth building

### 7.1 `hyprscheme-repl` — a real REPL into the live compositor (build first)

Guile ships exactly the thing this project needs and does not yet use:
**`(system repl coop-server)`**. `spawn-coop-repl-server` opens a Unix
socket; `poll-coop-repl-server` is called by the *host's* event loop and
evaluates pending client forms **on the calling thread**. Hyprland is
single-threaded, so that is precisely the integration point: the plugin
calls `poll-coop-repl-server` from a `doOnReadable` on the server socket
(or a 50 ms timer) and every REPL form runs on the compositor thread,
inside the watchdog, in the current generation module. Verified present
on this machine's Guile 3.0.11.

What that buys, for zero client-side code:

- `guile -c '(connect …)'`, `nc -U`, or any Guile REPL client talks to
  `$XDG_RUNTIME_DIR/hypr/$SIG/.scheme.sock` (instance dir, next to the
  hyprctl sockets — same trust boundary).
- **Emacs Geiser** connects with `M-x geiser-connect` → completion,
  `C-c C-d d` describe, `C-c C-d a` apropos, `C-M-x` eval-defun into the
  compositor, `,backtrace` after an error. This is the "Emacs-like"
  headline.
- Guile's meta-commands: `,help`, `,describe`, `,apropos`, `,binding`,
  `,module`, `,time`, `,trace`, `,profile`, `,pretty-print`, `,backtrace`.

The thin wrapper `hyprscheme-repl` (a `guile -s` script or a shell
one-liner) just finds the instance signature and connects; add
`--instance`, `-e expr`, and `-l file` for scripting. Keep `hyprctl
scheme` for one-shots.

### 7.2 `hyprscheme-apropos` / `hyprscheme-describe` — CLI documentation

Over `hyprctl scheme` (no REPL needed), a script that evaluates
`(hl--apropos "window")` → list of exported names matching, with the
first docstring line; and `(hl--describe 'hl-bind-add!)` → formals +
docstring. Both are two-line Scheme functions over `module-map` and
`procedure-documentation`/`program-arguments-alist`. Ship them as
`hl-apropos` and `hl-describe` in the API too — `describe-function` was
in the Chez tree and retired with defun; Guile gives it back for free.

### 7.3 `hyprscheme-lint` — check a config without a compositor

`tools/load-check.scm` grown up: load `(hyprscheme)` with stub gsubrs,
`compile-file` the user's config with `-Wunbound-variable
-Warity-mismatch`, report file:line. Run it as an editor save hook, in
CI over `examples/` and every wiki block. After §4.6 this is a
ten-line wrapper.

### 7.4 `hyprscheme-doc` — generate the API reference from docstrings

The wiki's function tables are hand-maintained and have drifted (§2.6).
The docstrings are the truth. A generator walks the export list, emits
one Markdown table per object family (name, formals, first docstring
paragraph, `*from lua …*` annotation — which can live in the docstring
as a trailing line), and the wiki pages `{{include}}` or embed the
generated sections between markers. `t-zzz-docs` then also asserts
"every exported name appears in the wiki" and "every `hl-` name in the
wiki is exported" — the spellcheck TODO.txt asks for.

### 7.5 `hyprscheme-mode` for Emacs (later)

A small major mode deriving from `scheme-mode`: `geiser-connect` to the
instance socket, `hl-describe-key` at point, a "reload scheme config"
command, `imenu` over `hl-bind-add!` forms. Mostly Geiser configuration.
Not urgent; 7.1 gives 90% of it.

### 7.6 `hyprscheme-check-config` — dry-run in a headless nested instance

`tests/run.sh` generalized: start a headless nested compositor with the
user's config, report load errors and the resulting `hyprctl binds`, exit.
Useful before switching a session to a new config. The harness pieces
exist.

### 7.7 Not worth building (agreeing with the closed decisions)

- A `.lua`-style completion stub: with 7.1 the editor gets completion
  from the live image; with 7.3 it gets it from the module. Closed
  stays closed.
- An in-process GUI for describe: the REPL/Emacs is the GUI.

---------------------------------------------------------------------

## 8. Tests and tooling plan

1. Fix `t-coverage` (§2.1) with a vacuity guard; add the 13 tests.
2. **Exercise thunks in doc blocks.** Make `t-zzz-docs` run each block
   inside a wrapper that, after evaluation, fires every bind/handler
   registered by the block (`hl--bind-fire-rec` on the returned records
   is already callable) so unbound names inside lambdas surface. Even
   simpler: after §4.6, compile each block with `-Wunbound-variable` and
   fail on warnings — no compositor needed for that half.
3. **Name cross-check test**: wiki `hl-` names ⊆ exported names, and
   exported names ⊆ wiki names (allow-list for deliberately internal
   `-id` accessors).
4. **Multi-monitor test.** `hyprctl output create headless` gives a
   second output in the nested instance; a layout test on it would have
   caught §2.5. Also gives `hl-monitor-*` real coverage (mirrors,
   focus, workspace moves).
5. **Type-confusion test**: `(hl-window-title (hl-active-workspace))`
   must return an error, not a crash (after §2.4).
6. **Error-path tests**: an erroring bind thunk produces a log line and
   a notification; a broken config at load produces the overlay and the
   emergency keymap (after §4.5/§5).
7. **Symlink/include reload test** (after §2.12).
8. **Event self-removal during fire** (already on TODO).
9. `make check` → `tests/run.sh`; `make lint` → syntax + compile
   warnings over machinery, examples, wiki blocks.
10. Guard the test harness against the `/tmp/hs-*` class: a test that
    asserts the plugin created no files under `/tmp` during the run.

---------------------------------------------------------------------

## 9. Documentation plan

- README: rewrite for Guile (build, install, "how it works" — the
  current text describes embedding a Chez kernel). Keep the structure.
- `Home.md`, `core.md`: Guile; correct the error-visibility and
  `--config` claims; add "the eval socket is arbitrary code execution".
- Sweep the eight stale names (§2.6) and the two `'option #t` leftovers.
- `monitors.md`: add `hl-monitor=?`; `bind-gestures.md`: document that
  `hl-gesture-add!` returns a handle record (after §2.11).
- `windows.md` etc.: state for every function whether `#f` is accepted
  for "active window".
- `naming-conventions.md`: the error model (raise vs `#f`), the option
  style, the `-set!`/`?` pairing rule — so reviewers have a rule to
  point at.
- `custom-layouts.md`: the work-area origin (after §2.5); the helper
  library if built.
- Generated API tables (§7.4) so this drift class ends.
- CLAUDE.md: it says "Test configs go through HYPRSCHEME_CONFIG" and
  lists the three `.scm` files — update as §4 lands (compat file gone,
  `.go` artifacts, module names). Its Chez cheat-sheet section can move
  to `chez/README.md`.

---------------------------------------------------------------------

## 10. Reorganisation (files)

```
src/
  hyprscheme/            ; Scheme, one module per family, compiled to .go
    hyprscheme.scm       ; (define-module (hyprscheme) …) re-exports
    core.scm             ; guards, watchdog, reporting, generations
    window.scm workspace.scm monitor.scm group.scm layer.scm
    bind.scm keys.scm event.scm timer.scm rule.scm config.scm
    notification.scm gesture.scm layout.scm exec.scm state.scm
    layout-lib.scm       ; (hyprscheme layout): split/grid helpers (pure)
  plugin/                ; C++
    Host.cpp/.hpp        ; init, generations, ipc, inotify, repl poll, shutdown
    Bindings.hpp         ; gsubr helpers, handle<T>, from<T>, str()
    Window.cpp Workspace.cpp Monitor.cpp Group.cpp Layer.cpp
    Bind.cpp Event.cpp Timer.cpp Rule.cpp Config.cpp
    Notification.cpp Gesture.cpp Exec.cpp Layout.cpp
    plugin-main.cpp
tools/
  lint.scm apropos.scm describe.scm gen-docs.scm repl.sh check-config.sh
tests/  (as now, plus §8)
chez/   (frozen; move the CLAUDE.md Chez cheat-sheet here)
```

Delete from the root: `plugin-main.cpp` (moved), `build-chez.sh`,
`rename.txt*`, `emacs-keys.txt*`, `TODO2.txt`, `#SchemeManager.cpp#`,
build products. Fold the useful parts of `TODO2.txt` into `TODO.txt`.

---------------------------------------------------------------------

## 11. Prioritised roadmap

**Phase 0 — bugs (a day).** §2.1, §2.2, §2.3, §2.7, §2.8's dead code,
§2.6 doc sweep, run the suite, commit. No design decisions needed.

**Phase 1 — safety and visibility (a few days).** §2.4 option 1 or 2,
§2.5 offset fix (backward-compatible variant), §4.5 backtraces +
notification + logger, §5 emergency keymap, `hl--eval` output capture,
inotify realpath. Tests §8.4–8.7.

**Phase 2 — the Guile-native rewrite (a week or two, mechanical).**
§4.1 → §4.2 → §4.3 → §4.4 → §4.6 → §4.7, one commit each, suite green
at each step. This is where the compat layer, the guardian machinery,
the 299 declarations and most of the C++ boilerplate go away.

**Phase 3 — the REPL and Emacs (days).** §7.1 coop server + `poll` in
the event loop; `hyprscheme-repl`; `hl-apropos`/`hl-describe` (§7.2);
Geiser instructions in the wiki. §7.3 lint, §7.4 doc generation.

**Phase 4 — API polish (needs Chris's decisions).** §2.11 naming and
option sweep (one breaking pass, well announced), hooks vs
`-notification-add!` (§6.3), gesture handles as records, layout API
(`ctx`/helpers/optional callbacks, §5), `defcustom`, keymaps, commands.

**Phase 5 — parity leftovers (§5).** Selector-or-handle resolution
everywhere + `hl-workspace-from`; `hl-bind-enabled-set!`/`?` and bind
introspection; `hl-env!`; plugin custom events; `hl-windows` `#:key`
filters over the upstream query; `hl-unbind-all!`; monitor `cm` /
`set_workspace`; `group-remove!` by index.

---------------------------------------------------------------------

## 12. Questions only Chris can answer

1. Hooks (`add-hook! hl-window-open-hook`) instead of
   `hl-X-notification-add!` — is the Emacs shape wanted enough to accept
   the rename churn? (The bubble/subscription name clash in §2.11 is the
   forcing function.)
2. Custom layouts: keep the minimal "boxes in, boxes out" model (fix the
   origin, add a helper library) or grow toward Lua's `ctx`/target model
   with optional move/swap/next-candidate callbacks?
3. Raise vs return-`#f`: which error model for actions?
4. Is `hl-window-id` (raw address) meant to stay public? §4.3 removes
   the need for it.
5. Compile user configs on load (with a plugin-owned cache) or keep
   interpreting them?
6. Which of the `. opt` symbol-flag functions in §2.11 may change shape
   (all breaking for existing configs; the wiki examples are the only
   known configs besides Chris's own).

---------------------------------------------------------------------

## Addendum — parity audit, full mapping

Lua file:line references are into `Hyprland/src/config/lua/`; Scheme
`:N` references are into `hyprscheme-bootstrap.scm`. Registration
helper upstream: `Internal::setFn`/`setMgrFn`
(`bindings/LuaBindingsInternal.hpp`), tables assembled in
`bindings/LuaBindingsRegistration.cpp:31-49`.

**Two systematic shape differences** (not gaps, but worth one paragraph
in `dispatchers.md`): (a) every `hl.dsp.*` is a *dispatcher factory*
returning a closure for `hl.bind` / `hl.dispatch`; Scheme's `hl-*!` are
immediate calls and a `(lambda () …)` is the value. (b) Lua takes one
options table and accepts a selector *string or object* wherever a
window/workspace/monitor is expected (`tableOptWindowSelector` etc.,
`LuaBindingsInternal.cpp:236-303`); Scheme takes positional handles
plus `#:key`s, selector strings only via `hl-*-from`.

### A.1 `hl.*` toplevel (`LuaBindingsToplevel.cpp`, `LuaBindingsConfigRules.cpp`)

| Lua | Scheme | Note |
|---|---|---|
| `hl.on(name, fn)` :505 | per-event `hl-*-notification-add!` | no string-keyed subscribe; plugin events unreachable |
| `hl.bind(keys, fn, opts)` :506 | `hl-bind-add!` :386 | key string vs token list; all flags present; Lua `"catchall"` handled at :89/:214 — Scheme sets the flag when the last token is `"catchall"` |
| `hl.define_submap` :507 | `hl-submap` :80, `-activate!`, `-exit!` | |
| `hl.timer(fn, {timeout, type})` :508 | `hl-after` :491 / `hl-repeat` :500 | |
| `hl.dispatch(d)` :510 | — | dispatchers are not values in Scheme (by design) |
| `hl.version()` :511 | `hl-version` :1702 | |
| `hl.get_loaded_plugins()` :512 | `hl-loaded-plugins` :1698 | |
| `hl.exec_cmd(str)` :513 | `hl-exec!` :1082 | Scheme adds `#:effects` |
| `hl.clear_crashed_lockscreen()` :515 | `hl-clear-crashed-lockscreen!` :1001 | |
| `hl.exec_scheduled_prop_refresh_immediately()` :517 | `hl-exec-scheduled-prop-refresh-immediately` :1009 | |
| `hl.unbind(bind \| "all")` :519 | `hl-unbind!` :2487 / `hl-unbind-key!` :2491 | **`"all"` missing** |
| `hl.is_key_down(k)` :521 | `hl-is-key-down` :1693 | |
| `hl.config` / `hl.get_config` (ConfigRules :1400) | `hl-config-add!` :1024 / `hl-config-get` :1063 | |
| `hl.device{}` :1402 | `hl-device-add!` :1070 | |
| `hl.monitor{}` :1403 | `hl-monitor-rule-add!` :2019 | |
| `hl.window_rule{}` :1404 | `hl-window-rule-add!` :1172 | |
| `hl.layer_rule{}` :1405 | `hl-layer-rule-add!` :1186 | |
| `hl.workspace_rule{}` :1406 | `hl-workspace-rule-add!` :1277 | |
| `hl.env(k, v)` :1407 | **MISSING** | |
| `hl.permission(...)` :1408 | `hl-permission-add!` :2085 | |
| `hl.plugin.load(path)` :1411 | **MISSING** | judgement call |
| `hl.gesture{}` :1414 | `hl-gesture-add!` :1964 + `hl-make-*-gesture`, `hl-gesture-remove!` | Scheme richer |
| `hl.curve` :1415 / `hl.animation` :1416 | `hl-curve-add!` :2060 / `hl-animation-add!` :2071 | |
| `hl.layout.register(name, tbl)` (LuaLayoutProvider :325) | `hl-layout-add!` :57 | `lua:` vs `scheme:` prefix |
| `hl.notification.create` / `.get` (Notification :133) | `hl-notification-add!` :1755, `hl-notify!` :1718 / `hl-notifications` :1774 | |
| `print(...)` (Registration :16 → `LOG INFO "[Lua]"`) | — | Scheme `display` goes to compositor stdout; see §4.5 |

Queries (`LuaBindingsQuery.cpp:417-435`): `get_windows(filters?)` →
`hl-windows` :658 (**filters missing**: `{monitor, workspace, floating,
mapped, className, title, tag}`, `SWindowQuery` :27-35; Lua defaults
`mapped=true` :115 and so does `hlSchemeWindowIds`); `get_window(sel|obj)`
→ `hl-window-from` :1334; `get_active_window` → `hl-active-window`;
`get_urgent_window` → `hl-urgent-window`; `get_last_window` →
`hl-last-window`; `get_workspaces` → `hl-workspaces`; **`get_workspace(sel)`
→ MISSING**; `get_active_workspace` / `get_active_special_workspace` /
`get_last_workspace` → `hl-active-workspace` / `hl-active-special-workspace`
/ `hl-last-workspace`; `get_workspace_windows` → `hl-workspace-windows`;
`get_monitors` → `hl-monitors`; `get_monitor` → `hl-monitor-from`;
`get_active_monitor` / `get_monitor_at` / `get_monitor_at_cursor` →
`hl-active-monitor` / `hl-monitor-at` / `hl-monitor-at-cursor`;
`get_layers(filters?)` → `hl-layers` (`#:monitor #:namespace`, matches
`SLayerQuery`); `get_cursor_pos` → `hl-cursor-pos`; `get_current_submap`
→ `hl-current-submap`.

### A.2 `hl.dsp.*` (`LuaBindingsDispatchers.cpp:1275-1372`)

- `cursor.move_to_corner` → `hl-cursor-move-to-corner!`; `cursor.move` → `hl-cursor-move!`.
- `group.toggle` → `hl-window-group-set!`; `group.next`/`prev` → `hl-group-cycle!` (+`'prev`);
  `group.active` → `hl-group-window-active!`; `group.move_window` → `hl-group-window-move-next!`;
  `group.lock` → `hl-groups-lock-set!`; `group.lock_active` → `hl-window-group-lock-set!`.
- `window.close/kill/signal/float/fullscreen/fullscreen_state/pseudo/move/swap/center/cycle_next/tag/clear_tags/toggle_swallow/pin/bring_to_top/alter_zorder/set_prop/deny_from_group/resize/drag`
  → `hl-window-close!/-kill!/-signal!/-float-set!/-fullscreen-set!|-maximized-set!/-fullscreen-state/-pseudo-set!/-move-direction!|-position-set!/-swap-direction!|-swap-next!|-swap-with!/-center!/-cycle!/-tag-add!/-tags-clear!/-swallow-toggle!/-pinned-set!/-zorder-set! "top"/-zorder-set!/-prop-set!/-deny-from-group-set!/-size-set!/hl-mouse-action!`.
  Lua-only arg shapes: `{action="toggle"/"on"/"off"}` (`parseToggleStr` :317) vs `#:on?`;
  `fullscreen{mode, action, layout_aware}` (:686-720); `cycle_next{next, tiled, floating, window}` tri-state (:564);
  `swap` accepts a **target selector** (`dsp_swapWithWindow` :571); every window dispatcher takes optional `window = obj|selector` (:336).
- `workspace.rename` → `hl-workspace-name-set!`; `change_id` → `hl-workspace-id-set!`;
  `move` → `hl-workspace-monitor-set!` (Lua also has the implied-current-workspace form :1279);
  `swap_monitors` → `hl-monitor-swap!`; `toggle_special` → `hl-workspace-special-set!` / `hl-monitor-workspace-special-set!`.
- root: `exec_cmd`/`exec_raw` → `hl-exec!`; `exit` → `hl-exit!`; `reload_config` → `hl-config-reload!`;
  `submap` → `hl-submap-activate!`; `pass` → `hl-window-pass-shortcut!`; `send_shortcut` → `hl-window-send-shortcut!`;
  `send_key_state` → `hl-window-send-key-state!`; `layout` → `hl-layout-msg`; `dpms` → `hl-monitor-power-set!`;
  `event` → `hl-event!`; `global` → `hl-global!`; `force_renderer_reload` → `hl-force-renderer-reload!`;
  `force_idle` → `hl-force-idle!`; `release_input_capture` → `hl-release-input-capture!`;
  `focus{direction|monitor|workspace(+on_current_monitor)|window|urgent_or_last|last}` (:1097-1152) →
  `hl-focus-direction-set!` / `hl-monitor-focus!` / `hl-workspace-focus!` / `hl-window-focus!` / `hl-focus-urgent!` / `hl-focus-last!`
  — **`on_current_monitor` missing; workspace/monitor selector strings not resolved** (see §5.1); `no_op` → (trivial).

### A.3 Objects

- **`LuaWindow`** (`objects/LuaWindow.cpp:74-250`): every field has a twin
  (`address mapped hidden visible accepts_input at size workspace floating
  monitor class title initial_class initial_title pid xwayland pinned
  pin_fullscreened fullscreen fullscreen_client allowed_over_fullscreen
  fullscreen_handler group tags swallowing focus_history_id inhibiting_idle
  xdg_tag xdg_description content_type stable_id layout tearing_hint`).
  `active` has no direct predicate (use `hl-window=?` with `hl-active-window`).
  Scheme extras: `-pseudo? -maximized? -prop -deny-from-group? -alive? =?`.
- **`LuaWorkspace`** (:110-172): full parity. Naming note: Lua `windows` is
  a *count*, Scheme `hl-workspace-windows` is the *list*
  (`hl-workspace-window-count` is the count).
- **`LuaMonitor`** (:100-228): parity except **`cm`** (:197, colour
  management mode) and **`set_workspace`** (:225). Scheme extras
  `-10bit? -alive?`.
- **`LuaGroup`** (:149-170): parity; Lua `remove` also takes an integer
  index (:118). Scheme extras `-alive? =? -id`.
- **`LuaLayerSurface`** (:46-73): full parity (+ `-alive? =?`).
- **`LuaKeybind`** (:111-175): only `remove/unbind` → `hl-unbind!`.
  **Missing: `set_enabled`, `is_enabled`, and every introspection field**
  (`enabled has_description description display_key submap handler arg
  modmask key keycode catchall repeating locked release non_consuming
  auto_consuming transparent ignore_mods long_press dont_inhibit click
  drag submap_universal mouse device_inclusive devices allow_input_capture`).
- **`LuaTimer`** (:89-94): parity (+ `hl-timer-cancel!`).
- **`LuaNotification`** (:330-364): full parity (+ `=?`, `-id`).
- **`LuaEventSubscription`** (:56-58): `remove`/`is_active` →
  `hl-notification-remove!` / `hl-notification-active?` (the naming clash
  of §2.11).
- **`Lua{Window,Layer,Workspace}Rule`** (:62-64): `set_enabled`/`is_enabled`
  → `hl-rule-enabled-set!` / `hl-rule-enabled?`.

### A.4 Events (`LuaEventHandler.cpp:231-266`, payloads :85-215)

All 34 built-in events have an `hl-*-notification-add!`:
`window.{open, open_early, close, destroy, kill, active, urgent, title,
class, pin, fullscreen, update_rules, move_to_workspace, bell, minimize}`,
`layer.{opened, closed}`, `monitor.{added, removed, focused,
layout_changed}`, `workspace.{active, special_active, created, removed,
move_to_monitor}`, `config.{reloaded, props_refreshed, unload}`,
`keybinds.submap`, `screenshare.state`, `hyprland.{start, shutdown}`,
`input.keyboard.key`. Payload notes: `window.active` carries a
`reason:int` in Lua that Scheme drops (`SchemeManager.cpp:4513`);
`window.move_to_workspace` carries `(win, ws)` in Lua, Scheme passes the
window only (`:4512`). Gaps: **plugin custom events** (:264-300) and
Lua's reentrancy suppression / `MAX_DISPATCH_DEPTH = 32` (:29, :44) —
a Scheme handler that triggers its own event recurses until the
watchdog fires.

### A.5 Custom layouts

Lua (`layout/LuaLayoutContext.cpp:152-175`, `LuaLayoutTarget.cpp:39-66`,
`LuaLayoutProvider.cpp`): `ctx.area = {x, y, w, h}` (**with origin**),
`ctx.targets` (each: `index`, `window`, `box`, `place(box)`/`set_box`),
helpers `ctx:grid_cell(i, cols, rows?)`, `ctx:column(i, n)`,
`ctx:row(i, n)`, `ctx:split(box, dir, ratio)`; callbacks `recalculate(ctx)`
(required) and `layout_msg(ctx, msg)`; `newTarget/movedTarget/
removeTarget/resizeTarget (ignores Δ)/swapTargets/moveTargetInDirection/
getNextCandidate/predictSizeForNewTarget` are fixed C++ behaviour on both
sides. `Space::workArea()` is seeded from
`m_monitor->logicalBoxMinusReserved()` (`Space.cpp:85`, `Monitor.cpp:1691`),
i.e. `{m_position - reserved, …}` then gaps — **its x/y are non-zero on
any non-primary monitor and on the primary whenever `gaps_out > 0` or a
bar reserves space.** Scheme sends `W H` only (§2.5). Scheme-only:
`resize` (with `#f` = "re-run recalculate"), `window-open`,
`window-close`. Scheme-missing: context/origin, targets, helpers,
`predictSizeForNewTarget` override, watchdog on layout callbacks
(`SchemeLayout.hpp:24` says none; `dispatchRecalculate` is not routed
through `hl--guarded-run` — Lua uses `LUA_TIMEOUT_LAYOUT_CALLBACK_MS`).

### A.6 Error handling

Lua: `guardedPCall(fn, timeoutMs, context)` (`ConfigManager.cpp:467`)
with `LUA_MASKCOUNT` watchdog hook (:452) and per-site budgets
(`LUA_TIMEOUT_{CONFIG_RELOAD, EVENT_CALLBACK, KEYBIND_CALLBACK,
LAYOUT_CALLBACK, EVAL}_MS`); `luaL_traceback` message handler at config
load (:750); `addError` accumulates during parse/eval and otherwise pops
"Runtime error in lua:" (:867-879); per-site reports (`LuaEventHandler
:72`, keybinds :1391, layouts `LuaLayoutProvider:213` sticky `didError`);
`dispatchResultFromLua` (:1379) `{ok, error, pass_event, request_release}`;
emergency mode (:782, `Emergency.hpp`: SUPER+Q terminal, SUPER+R
`hyprland-run`, SUPER+M exit) when errors *and* empty registry; eval
channel `CConfigManager::eval(code, repl)` (:906) wraps bare expressions
in `return`, collects prints and issues.

Scheme: `hl--report` → stderr only, no traceback, no bubble (layout
errors are the exception: `SchemeLayout.cpp:346`); Scheme-level watchdog
with one budget for everything (`hl--watchdog-ms`), not applied to
layouts; result protocol at parity (`hl--bind-result`); **no emergency
mode**; `hyprctl scheme` evaluates into the current generation and
returns the last value only.

### A.7 Scheme beyond Lua

`hl-state-*`; `hl-kbd`/`hl-key`; the `hl-make-*-gesture` constructors,
`hl-gesture-action?`, `hl-gesture?`, `hl-gesture-remove!`; layout
`resize`/`window-open`/`window-close`; `=?` and `alive?` on every handle
family; `hl-window-pseudo?/-maximized?/-prop/-deny-from-group?`,
`hl-monitor-10bit?`, `hl-workspace-window-count/-group-count`,
`hl-active-title`; `hl-notify!`, `hl-plist-get`, `hl-after`/`hl-repeat`,
`hl-timer-cancel!`, `hl-submap-exit!`, `hl-unbind-key!`; per-generation
environments; a persistent eval channel.
