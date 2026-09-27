# FABLE-DONE.md — the work ledger for the FABLE.md review

WHENEEVER WE COMPLETE WORK FROM FABLE.md we log it here to keep track of
it using the same reference number in the same order as in FABLE.md.

Every heading is **FABLE.md's own reference**, so the two documents
line up directly:

- `§2.x` — a bug or defect from FABLE.md's review section (§2.17–§2.19
  are new: found while doing the work, and added to FABLE.md in the
  same pass)
- `§4.x Step N` — a step of the Chez purge, FABLE.md §4
- `§9`, `§12` — the documentation plan and the open questions

Sections appear in FABLE.md's order, so "what is done" and what is not
can be read straight off.

Status vocabulary: **DONE** (changed and verified) · **NOT STARTED** ·
**NEEDS A DECISION** (deliberately untouched — design call, per
CLAUDE.md).

Verification for everything below: `tests/run.sh` green — run with
`PLUGIN=$PWD/scheme-plugin-guile.so`, and again (the final run, after
`make install`) through the default installed path, which is what the
harness does with no override. There is no freshness warning in that
final run, i.e. plugin and `.scm` are from the same build. The count now
means something — see §2.13.

The suite was **12 files** until the layout-callback pass added
`t-zzz-monitors.sh` (§2.5, the 13th) and the doc-verification pass added
`t-zzz-snippets.sh`, so it is **14** from there on. Earlier entries below record
the count of their own time — "12/12" in a section written before those passes
was true when it was written, and has not been rewritten to match.

---------------------------------------------------------------------

## §2.1 — `tests/t-coverage.sh` extracted nothing: DONE

Three stacked defects, all fixed:

- it read the API list from `SchemeManager.cpp`, looking for a
  `SCHEME_BOOTSTRAP` marker that has not existed since the machinery
  moved into `hyprscheme-bootstrap.scm` (it found 0 names and passed on
  an empty list);
- its pattern `[a-z0-9-?]` is rejected outright by this machine's grep
  (ugrep: `Invalid range end`), and could not match `(define*` or names
  containing `=`/`!` anyway;
- the name search was an unanchored regex, so names containing `?`/`*`
  could match loosely.

It now reads `src/config/scheme/hyprscheme-bootstrap.scm` with a
POSIX-safe pattern under `LC_ALL=C` (`-` last in the bracket
expression), extracts **304 public APIs**, uses `grep -F` for the name
search, and carries a **vacuity floor**: under 200 names is reported as
"the extraction is broken, not the API" and fails.

### §2.1, continued — the 13 uncovered APIs now have tests: DONE

Running the repaired test listed exactly the 13 predicted:
`hl-group-alive?`, `hl-group-cycle!`, `hl-group-id`, `hl-layer=?`,
`hl-layer-id`, `hl-layer-pid`, `hl-monitor-id`,
`hl-notification-color-set!`, `hl-notification-id`, `hl-plist-get`,
`hl-timer-cancel!`, `hl-windows-from`, `hl-window-swallow-toggle!`.
All 13 now have assertions in `tests/t-api.sh`. Two of them failed on
first run and turned out to be **real bugs** — §2.17 and §2.18.

Notes on coverage honesty: the layer accessors are asserted behind the
file's existing `(or (null? ls) …)` guard because the nested session
has no bar — it reports `layers in this session: 0`. The guard is why
`t-coverage` is a *reference* check, not a runtime-assertion check; the
names are exercised for real only where a layer exists.

---------------------------------------------------------------------

### The cleanup pass — §2.2, §2.3, §2.7, §2.8, and the lints: DONE

Five items, one pass, suite green.

- **§2.3 — the debug writes are gone.** `watchdogEnter` appended to
  `/tmp/hs-wd-probe` on **every scheme callback**, and `hlSchemeWindowClass` to
  `/tmp/hs-sel-debug`. The probe file had reached **1,056,510 bytes** in one
  session: synchronous I/O in the hot path of the whole API, unbounded. Both
  writes deleted (`Host.cpp`, `Window.cpp`).
- **§2.2 — `hl-monitor-rule-add!=?` is `hl-monitor=?`.** The name a botched
  rename had mangled, sitting where a monitor equality predicate belongs and
  named like its siblings (`hl-window=?`, `hl-workspace=?`, …). Renamed in the
  family, in the public list, **and in `t-api.sh`** — the test asserted the
  broken name, which is what kept the bug alive.
- **§2.7 — `src/plugin-main.cpp` deleted.** The Makefile builds
  `src/plugin-main.o` from the **root** copy; the `src/` one was a stale
  Chez-era duplicate (its comments still said "Chez displaces…", "after
  Sbuild_heap"). `chez/` has its own copy, so the frozen build is unaffected.
- **§2.8 — the two dead C entry points deleted** (`hl--c-window-float`,
  `hl--c-group-lock-active`), each superseded by its `-act`/`-set!` variant.
  `bind-audit` reports **306 registered, 306 called: no dead code**.
- **`bind-audit`'s dead detection was repaired.** It had gone silent in the
  module pass: the kernel's export list is generated from the registrations, so
  every entry point appeared "called". Declarations are now stripped before the
  scan, and the tool found both dead entry points again.

### The lints now run on every `make` (six)

`all: $(TARGET) check`, and `check` runs:

| lint | catches |
|---|---|
| `tools/syntax-check.scm` | the reader's view: parens, quotes, docstring corruption, at the edit site |
| `tools/module-audit.py` | the module rule: a family imports only the kernel and core and never calls another family — plus, since the layout-callback pass, that every top-level define is in its own module's `#:export` and every public name reaches the umbrella's `#:re-export` |
| `tools/load-check.scm` | a load-time error, with no compositor needed |
| `tools/bind-audit.py` | registrations: unbound, duplicate, dead — and **anything undefined in the built `.so`** |
| `tools/doc-audit.py` | a name the WIKI uses that does not exist (textual, so it sees inside thunks) |
| `tools/doc-exercise.scm` | a name inside a callback the wiki REGISTERS — by calling it |

Each was **verified to fail**: a deliberate cross-family call trips module-audit,
an undefined call at load trips load-check. (A check that cannot fail is the trap
`t-coverage` spent its whole life in.)

`bind-audit` earned its keep again in this pass: it is what caught that
consolidating the registrations had dropped `hl--c-run-finalizers`, which would
have left the collector's finalizer queue never drained.

### §2.5 — custom layouts ignored the work-area origin: DONE

Layout geometry is **absolute** now. The work area reaches Scheme as an
`hl-box` carrying its real `x`/`y`; the placements handed in are in the same
global space; the boxes handed back are applied as given. Nothing adds an
offset behind the layout's back, so a layout written on the primary monitor
works on a second one and under a bar — the case the harness could never have
seen, having one nested output at the origin.

§2.5 also flagged `schemeIntList` in `SchemeLayout.cpp` as a copy of the one in
`SchemeManager.cpp`. The copy was **dead** — it built the old positional
payload's integer lists, and the new payload uses `hl::listOf` — so it is
removed. (`Event.cpp`'s, which is live, stays.)

Fixing the origin meant settling what a callback is handed and what it returns,
which is §12.2's question: see **§12.2** below for the contract this pass
landed.

**Tested in its own scenario, not just reasoned.** §2.5 was rated HIGH for
multi-monitor, and the first fix pass could only *reason* that it was right:
the harness has one nested output at the origin, which is the blind spot the
finding itself names. `tests/t-zzz-monitors.sh` closes that — it creates a
second output (headless, the one step with no API), moves the primary to
x=1920, and drives a layout that places every window at the work area's own
corner. Measured: the layout is handed `area x=1940` (the monitor's 1920 plus
gaps_out), and its windows come out at global x=1941 — on the monitor at 1920.
Under the old payload the layout received only the size, so the same layout
would have returned origin-relative boxes and put the windows at x≈0, on the
other output.

Three things that test cost, all now known rather than guessed: the headless
backend is **mandatory** in this build, so `output create headless` works even
in the nested wayland session; a config write (`hl-monitor-rule-add!`) rebuilds
the Scheme generation, so monitors are positioned *before* any layout is
registered; and the compositor opens new windows on the monitor **under the
cursor**, so the fixture window is moved onto the target workspace rather than
opened there. It sorts after the doc test — nothing but the exit test follows —
and it asserts its own teardown (one monitor, primary back at x=0), because a
test that changes the monitor topology does not get to assume it put it back.

### §2.6 — the two failing wiki blocks: DONE

- The resize submap block (was line 33) used `hl-window-resize`, which
  no longer exists (it is `hl-window-size-set!`), and the pre-keyword
  option form `'repeat #t`. Now `(hl-window-size-set! #f ±20 0
  'relative)` with `#:repeat #t`, and the NOTE above it says so.
- The universal-submap block (was line 65) passed a bare `thunk`
  placeholder, which the interpreter resolves — `Unbound variable:
  thunk`. Now a real thunk (`(lambda () (hl-notify! …))`), which also
  removed the need for a skip entry.
- Also corrected the stale comment in the first submap block ("the
  resize dispatcher does not exist in Scheme yet"), which was untrue.

## §2.13 — the doc-example test never ran: DONE

`tests/t-zz-exit.sh` called `(hl-exit!)` and sorted *before*
`t-zzz-docs.sh`, so the 120-block wiki test ran against a dead
compositor and passed vacuously. Four sub-steps, each reported
separately:

1. **Renamed** `tests/t-zz-exit.sh` → `tests/t-zzzz-exit.sh` (via
   `git mv`), so it sorts strictly after `t-zzz-docs.sh`
   ("t-zzz-docs.sh" < "t-zzzz-exit.sh" byte-wise and under en_US
   collation). Its header comment now states the ordering contract
   instead of claiming "named zz so it runs LAST".
2. **Liveness guard** added at the top of `tests/t-zzz-docs.sh`: it
   evaluates `(hl-version)` and *fails* if the compositor is not
   answering, so a dead compositor can never again be read as 120
   passes. Also added an `evaluated=/skipped=` tally, printed when the
   file is run directly.
3. **Ordering contract documented** in `tests/run.sh` above the rule
   loop, naming the failure mode explicitly.
4. **The two blocks it then failed on were fixed** (see below).

Result: the doc test now genuinely runs. Replaying the driver's loop
independently against a live instance: **125 blocks extracted, 120
evaluated, 5 skipped (schematic placeholders, by design), 0 failures**.

### §2.13, second defect — a third stale block, found only once the test really ran: DONE

`custom-layouts.md`'s `master-stack` example failed with
`layout master-stack rejected` — not because the example is wrong, but
because `tests/t-zz-example.sh` live-loads `examples/hyprland.scm`,
which registers `scheme:master-stack` and leaves it registered for the
rest of the session; the doc block then collides with the name.
`tests/t-zz-example.sh` now ends with `(hl-config-reload!)`, which is
the documented layout lifecycle (the registry is cleared per
generation), restoring the harness config's state for the doc test.
The wiki block itself was left alone — it is correct in isolation.

---------------------------------------------------------------------

## §2.13 — stale skip patterns: DONE

`tests/wiki-examples.skip`:

- `thunk 'submap-universal` no longer matched anything (the block had
  been converted to `#:submap-universal`). The block is now a real
  example, so the entry was **removed** — the skip list's own rule is
  that every entry is an example that never gets executed.
- the `THUNK` entry is case-sensitive but correct: uppercase `THUNK`
  really does appear in `bind-flags.md` and `binds.md` blocks (verified
  by extracting the blocks), so it stays.
- the remaining four entries (schematic callback names, the
  startup-only permission example, the lockscreen refusal) still match
  their blocks.

The remaining five skips are the schematic placeholders and the two
intentionally-excluded examples — `THUNK` ×2 (`bind-flags.md`,
`binds.md`), `recalculate-fn` (`custom-layouts.md`), `hl-permission-add!`
(startup-only) and `hl-clear-crashed-lockscreen!` (its refusal *is* the
documented behaviour). Each carries its reason, as the file requires.
The block tally (`evaluated=… skipped=…`) is printed when the test is
run directly, so the next reader can see the test did real work.

---------------------------------------------------------------------

## §2.14 — stale installed artifacts + wrong README commands: PARTLY DONE

- **`make install` run**: `~/.local/lib/hyprscheme/` now holds the
  current `scheme-plugin-guile.so` and the three current `.scm` files
  (they were from 2026-09-25 17:19, i.e. *before* the `#:keyword`
  conversion — the state that made a default `tests/run.sh` fail in
  ways that looked like code bugs).
- **Freshness warning** added to `tests/run.sh`: when the plugin under
  test differs (byte-wise) from the tree build next to the sources, it
  prints the path, the mismatch and the `PLUGIN=…` line to use instead.
  Verified both ways — it fires against the old install, and is silent
  after `make install`.
- **README build/install instructions corrected**: `make guile` does
  not exist (the Guile artifact is the default `make` target), so
  `make guile && make install` was a hard failure; the Chez
  requirements, the `CHEZ_DIR` bullet, the boot-file mentions, the
  `scheme-plugin.so` load path and the "statically embeds a
  position-independent Chez Scheme kernel" paragraph were all wrong for
  this tree and are corrected. The `chez/` freeze is now stated.
  - *Not done:* the fuller README restructure in FABLE.md §9, and the
    prose in `Status`/`Usage`/`Session integration` was left as-is.
- **DONE (later): the Chez-era leftovers are gone.** `hyprscheme-defun.scm`,
  `petite.boot`, `scheme.boot` and `scheme-plugin.so` (~20 MB) were deleted from
  `~/.local/lib/hyprscheme/` after re-checking that nothing in `~/.config`
  references them. `make install` now puts the module tree
  (`hyprscheme.scm` + `hyprscheme/`) there, and the superseded pre-module
  `hyprscheme-prelude.scm`/`hyprscheme-bootstrap.scm` copies were removed with
  them. The suite is green afterwards.

### §2.15 — layout callbacks were not under the watchdog: DONE

`hl--layout-call` now runs its callback through `hl--guarded-run`, in the shape
every other callback path already used: an error and an abort both collapse to
the `hl--wd-aborted` marker, the wrapper turns that into `#f`, and C++ rejects
`#f`. A runaway `recalculate` is therefore abandoned and reported, and the
layout falls back to the default grid **for that pass only** — the next
recalculate tries the layout again, so a transient failure recovers.
`hl--layout-resize` got the same treatment.

`core.md`'s promise — "an infinite loop in a bind or a layout recalculate … is
aborted from the compositor event loop and reported" — is true as of this
pass. It had been false since it was written.

**Tested, and tested for discrimination.** `tests/t-watchdog.sh` registers a
layout whose `recalculate` loops forever, selects it by switching
`general:layout` (that write is what fires a recalculate), and asserts the
layout really is selected, the callback really ran, and the compositor still
answers. The first version of the test passed **vacuously**: a layout with no
windows is never recalculated at all, so nothing ran and the runaway was never
exercised. That is why the callback now records that it was entered before it
loops. Verified the other way round too — with the guard removed from the
source `.scm` (no rebuild needed; the machinery is read from the tree) the test
fails with `Hyprland IPC didn't respond in time`, i.e. a wedged compositor.
The loop fires **once** on purpose: re-running it every pass would leave the
compositor alive but spending a full watchdog budget on each layout pass, a
hazard the test should not manufacture on top of the one it tests.

---------------------------------------------------------------------

## §2.17 – §2.19 *(new — found while doing the above)*

### §2.17 — `hl-plist-get` raised when its optional DEFAULT was omitted: DONE *(new)*

`(hl-plist-get plist key . default)` was implemented as
`(apply hl--plist-get plist key default)`, which passes an empty
argument list when the default is omitted — `Wrong number of arguments
to hl--plist-get`. Found by the §2.1 test. Now passes the default
explicitly (`#f` when omitted), preserving the documented contract.
Verified in isolation: `→ 2`, `→ dflt`, and an explicit stored `#f`
reads back as `#f`.

### §2.18 — `hl-group-alive?` always returned `#f`: DONE *(new)*

The C entry `hl-scheme-group-alive` returns an **int** (1 alive, 0 gone,
−1 before init), but the Scheme wrapper compared it with `eq? … #t` —
so the predicate was constant `#f`. Found by the §2.1 test.
Now `(= 1 …)`. Audited exhaustively afterwards, since the first audit
pass had a name-truncating pattern and could have missed cases: all
**23** `(eq? (c-hl-… ) #t)` comparisons use `scheme-object`-returning
entries and all **27** `(= 1 (c-hl-… ))` comparisons use `int`-returning
ones, so this was the only mismatch of its kind. Two ways to get it
wrong, one instance — worth a line in the conventions doc rather than a
defensive helper everywhere.

### §2.19 — `t-zz-example.sh` leaks the example's layout registration: DONE *(new)*

See 1b: the example config registers `scheme:master-stack`, which stays
registered for the session and collided with the doc test's own layout
example. The test now reloads the config at the end to restore a clean
generation.

---------------------------------------------------------------------

## Still not started

The items still genuinely open. Seven of the originals have been struck from
this list rather than left in both places — §2.2, §2.3, §2.7 and §2.8 by the
cleanup pass, §2.4 by Step A's typed accessors and Step C's foreign objects (see
§4.1/§4.3), and §2.5 and §2.15 by the layout-callback pass. A ledger that says
"done" in one section and "open" in another is worse than no ledger.

- **§2.12** — `hl-window-swap-next!` docstring vs behaviour; `hyprctl
  scheme` with no argument; the watchdog thread never stopped; symlinked or
  `(load …)`-ed configs never triggering the inotify reload.
- **§2.16** — the remaining small item: the double-resolving
  `hlSchemeWindowClass`. (The two dead C entry points it also lists are
  **gone** — the cleanup pass.)


*(§2.9 — `errorf` discarding its `who` — is **done**, as a consequence of
§4.1 Step A: `hl--error` keeps the origin and the renderer prints it.)*

## Wiki rot (the non-failing names) — MOSTLY DONE

Four of the five are **fixed**, and the class is now **checked rather than
hunted** — see "the wiki's snippets, verified three ways" below.

- `code-snippets.md` `hl-window-into-group` / `-out-of-group` / `hl-group-cycle`
  / `hl-group-index` / `hl-group-move-window` — fixed in the rewrite (five
  names, and the group-toggle that forced groups OFF where the original
  toggles).
- `bind-globals.md` — `hl-pass`/`hl-send-shortcut` in prose became
  `hl-window-pass-shortcut!`/`hl-window-send-shortcut!`.
- `binds.md` — `(hl-window-cycle)` became `(hl-window-cycle!)`.
- `scheme-utilities.md` — `(hl-monitor-selector M)` became `(hl-monitor-name M)`
  (`hlMonitorSelector` returns the same field as `hlMonitorName`, and has no
  public wrapper of its own).

Still open: `naming-conventions.md`'s `'release #t` pairs (a shape change, not a
name).

*(`core.md`'s layout-watchdog promise was on this list and is now **true
rather than rot** — the §2.15 fix; it describes what the code does.)*

*(`core.md`'s two FALSE CLAIMS are fixed — "the full Chez Scheme standard
library" became Guile, and errors are no longer promised to raise a
notification that nothing raises; `scheme-utilities.md` had the same Chez claim
and was fixed with it.)*

## Needs a decision (from FABLE.md §12) — NOT TOUCHED

Hooks vs the `-notification-add!` family; the naming/convention sweep;
gesture handles as records; emergency binds; compile-configs-on-load; and the
parity gaps of FABLE.md §5. (The layout `ctx`/target model was on this list —
Chris answered it this pass: see §12.2.)

### §12.2 — the layout model: ANSWERED (the minimal model, keyed by window): DONE

FABLE.md §12.2 asked whether to keep the minimal "boxes in, boxes out" model or
grow toward Lua's `ctx`/target model with move/swap/next-candidate callbacks.
**Chris kept the minimal model** — a layout is still one callback that computes
plain geometry and returns boxes, with no `ctx` and no `:place()` — but the
boxes are no longer positional:

```scheme
(recalculate  area placements)               ; area = hl-box, GLOBAL coords
(resize       area placements dx dy corner)
              → ((window . box) …)
```

`placements` is one `(window . box)` pair per window to place, each box being
where that window is **now**; the return names the windows to move and where.
Windows left out keep their geometry, so a partial return is legal and `'()`
means "change nothing".

**Why keying by window and not by index.** The old shape was
`(count W H windows)` → `((x y w h) …)`, where box *i* was assumed to belong to
window *i*. That is silently wrong the moment a callback reorders, filters or
returns fewer boxes than it was given — every window gets the wrong geometry and
nothing reports it. Matching by window makes a reordered or partial return
either correct or rejected, never quietly mis-placed. A window that is not in
the layout is refused and logged.

**A target with no window is dropped.** A dead target exists only for the
moment between a window dying and the layout dropping the slot; there is
nothing to place in it, so it is skipped rather than passed as a
dead-handle convention the callback has to know about. (An earlier design
considered `dynamic_cast`-ing group targets; asking the window for its group is
both simpler and what the API now offers.)

**`hl-box` and `hl-window-group` are new public API.** A box is an `hl-box`
record built with `(hl-box x y w h)` and read with `hl-box-x`/`-y`/`-w`/`-h`,
so a wrong-shaped value is an error naming the function rather than a silent
read of the wrong slot. Boxes are **whole pixels**: the compositor hands back
exact integers and rounds what it is given. That symmetry is deliberate —
C++ used to hand back inexact doubles, so `(quotient (hl-box-h area) 2)`, the
first thing a layout writes, failed on an inexact argument. `hl-window-group`
returns a window's group handle (or `#f`), which is how a layout discovers
grouping now that it is handed a window rather than a target.

**The returned box is the cell.** The compositor insets the window inside it by
the border and `gaps_in`, exactly as for the built-in layouts, so a layout
computes cells and gaps look after themselves. Measured, not assumed: a probe
layout returning `(100 200 200 150)` produced a window at `(106 . 206)` sized
`(188 . 138)` — an inset of `border + gaps_in` on each side.

**One real bug found by probing, worth recording.** `isBox` used
`scm_struct_vtable_p`, which answers "is this a *vtable*", not "is this a
struct with this vtable" — so **every returned box failed the shape check** and
every layout silently fell back to the default grid. The old positional code
never noticed because it read plain lists. It was found by instrumenting the
C++ (with `fprintf` to fd 2: `LOG()` is swallowed by the logger refactor) and
dumping the offending value, which printed as `#<hl-box x: 100 y: 200 w: 200 h:
150>` — correct data, wrong predicate. `SCM_STRUCTP` is the right test.

**Docs rewritten in the same pass**, because all three described the old shape:
`custom-layouts.md` (contract, boxes, the cell/inset rule, global coordinates),
`README.md` (whose example was *also* independently broken — it passed a lone
lambda, which is an error, and used the R6RS-only `exact`), and
`examples/hyprland.scm`. The wiki blocks are executed by `t-zzz-docs`, so the
rewritten examples are verified, not just written; the README and example
versions were additionally run against stubs to check the geometry (a
4-window master/stack sums its slave heights to the work-area height).

**`tests/soak.sh` was stale four ways** and had not been run in a while: it
defaulted to the **deprecated Chez** plugin (so it could not start at all),
used the old layout shape, called `exact` (unbound in a config), and called
`hl-rule-enabled-set!` positionally after the `#:on?` migration — 104 eval
errors a run. All four fixed; a 25s soak is now `VERDICT: PASS`, 0 eval errors,
0 compositor ERR lines, 0 crash markers.

---------------------------------------------------------------------

## Observations from this pass (no action taken)

- **`tests/t-api.sh`: `line 194: -: command not found`** appears in the
  failing-run output (it was "line 186/180" in earlier records — the
  same phenomenon, following my insertions). It does not affect the
  result (the suite is green), it is the flake already noted in
  `TODO2.txt`, and it is not caused by the edits here. Worth
  root-causing separately.
- **`dissolved,`** is an untracked stray file in the repo root (already
  recorded in `TODO2.txt` as junk from a botched test run); left alone.
- **`CLAUDE.md` shows as modified in git** — that is Chris's edit (the
  "@chez is deprecated" note), not part of this pass.
- Nothing has been committed; committing is Chris's call as always.

## THE CHEZ PURGE (FABLE.md §4, Steps A–G)

Agreed with Chris on 2026-09-26: purge the Chez dialect and the Chez-era
shims entirely, so the machinery reads as natively-written Guile.
Decisions taken: **native foreign objects** for step 3, **compile the
machinery to `.go`** in step 6, and **step 7 (the C++ file split)
deferred to a later run** so this one does not sprawl. The numbering
below continues the bug-list sequence (17 = §4 step 1) so the ledger
stays a single ordered track.

### §4.1 Step A — native dialect in the machinery: DONE

Scope: the three `.scm` files only. **No C++ change, no rebuild.**

What was Chez and is now Guile:

| was | is |
|---|---|
| `errorf` (87 call sites; dropped its `who`) | `hl--error` — builds a condition that KEEPS the origin, so every message now names the function that rejected the argument |
| `(format fmt args...)` Chez arity | `(format #f fmt args...)` |
| `andmap` / `list*` | `every` (srfi-1) / `cons*` |
| `(exact x)` | `inexact->exact` |
| `(eof-object)` (0-arg, Chez-only) | `the-eof-object` (Guile's own name for the value) |
| `collect` + `collect-request-handler` (a NO-OP on Guile — the real hook was in the compat file) | one `(add-hook! after-gc-hook …)` in the bootstrap, where the drain lives |
| `set-timer` / `timer-interrupt-handler` (Chez timer API names) | `hl--wd-alarm` / `hl--wd-handler-set` (the implementation was already Guile's sigaction + asyncs) |
| `time-difference` + `time-nanosecond` + `current-time` | `hl--now-ms` over `gettimeofday` |
| `top-level-value sym env` | `module-ref` |
| `copy-environment` | `hl--generation-copy` (same mechanism, house name; real modules are §4 step 4) |
| `void`, `real-load`/`real-eval` names | `*unspecified*`, `hl--base-load`/`hl--base-eval` |
| `hl--split-string` / `hl--string-index` / `hl--string-join` / `hl--trim` (hand-rolled) | `string-split` / `string-index` / `string-trim-both` |
| the `current-error-port` **shadow** | `set-current-error-port` — the fd-2 port is installed natively instead of overriding Guile's parameter |
| `define-record-type` Chez shorthand (10 records, via a macro in the compat file) | explicit Guile record primitives (`make-record-type` / `make-struct/no-tail` / `struct-ref`) with **type-checking accessors** |
| 19 Chez-only definitions in `hyprscheme-compat-guile.scm` | deleted — the imports are now standard libraries declared in the prelude: `(ice-9 exceptions)` (guard + the exception API), `(rnrs io ports)` (`call-with-string-output-port`), `(rnrs lists)` (`exists`/`for-all`, which configs and tests use), `(srfi srfi-1)`, `(ice-9 optargs)` |

The compat file is now **the C-function bridge and nothing else**
(280 → 68 lines); step 18 deletes it outright.

**Two bugs from the bug list are fixed by this step**, as natural
consequences of writing the code natively — both now have regression
tests in `t-api.sh`:

- **§2.4** — untyped record accessors let `(hl-window-title (hl-active-workspace))`
  reinterpret a workspace as a window and hand compositor C++ a bad
  pointer. The accessors type-check now: the call reports
  `hl--window-cell: not a hl-window: #<hl-workspace cell: (…) >` — the
  origin is the innermost accessor, and the record prints its own
  family. (Step 19 replaces the records
  with foreign objects, which makes this structural, but the safety
  lands here.)
- **§2.9** — `errorf` threw away the `who` argument it was given at
  all 87 sites. `hl--error` keeps it and `hl--print-exception` prints
  it, so every rejection now names its function.

**User-visible dialect changes** (the machinery is what configs and
`hyprctl scheme` evaluate against, so these are API-surface changes):

- `display-condition` no longer exists (it was a compat definition of a
  Chez procedure; Guile has no such binding). The house renderer is
  `hl--print-exception`, used by `hl--report`, by `hl--eval`'s error
  reply, and by the suite's 29 error-capture sites.
- `exact` no longer exists → `inexact->exact`. Fixed in
  `examples/hyprland.scm` and two wiki blocks.
- Chez-style `(format "…" args)` (no destination) no longer works →
  `(format #f "…" args)`. Fixed in two wiki blocks. `(format #t …)` is
  still valid Guile and was left alone.
- `hl--watchdog-ms` (documented in the wiki as user-settable) is
  unchanged for now; §4 step 4 makes it a public parameter.

**Also corrected in this pass:** a latent doc bug found while fixing
`bind-gestures.md` — the example looked up *keyword* keys
(`#:fingers`, `#:scale`) in the symbol-keyed gesture-event plist, so
those two fields silently returned the defaults.

**Verification:** `tests/run.sh` **12/12**; `tools/syntax-check.scm`
clean on all three files; `t-coverage` found three predicates my
rewrite had left untested (`hl-notification?`, `hl-workspace?`) or
newly visible (the internal accessors, which are now `hl--`-prefixed
per the house convention), all resolved with tests or renames.

Files touched: `hyprscheme-prelude.scm` (rewritten),
`hyprscheme-compat-guile.scm` (reduced to the bridge),
`hyprscheme-bootstrap.scm` (records, errorf, dialect call sites),
`tests/t-api.sh`, `tests/t-config.sh`, `examples/hyprland.scm`, `CLAUDE.md`
(the machinery list), and the wiki: `submaps.md`-era dialect in `Home.md`,
`core.md`, `custom-layouts.md`, `notifications.md`, `bind-gestures.md`.

**Not done in step 1** (the compat file's two remaining "Chez"
comments describe the FFI type names it translates; both die with the
file in step 18).

### §4.2 Step B — gsubrs replace `foreign-procedure`: DONE

The FFI mechanism is gone. Every one of the **299 C entry points** is now
registered as a **gsubr** — a real Guile primitive taking and returning
`SCM` — and the Scheme side calls them directly.

What was deleted:

| gone | replaced by |
|---|---|
| `hyprscheme-compat-guile.scm` (the libffi shim) | nothing — the file is deleted; the machinery is **two `.scm` files** now |
| the 299 `(define c-hl-… (foreign-procedure …))` declarations | direct gsubr calls (`hl--c-*`) |
| `SchemeValue` (409 mentions) | plain `SCM` |
| `SchemeHost.hpp` + `SchemeHostGuile.cpp` | `Bindings.hpp` + `Guile.hpp`/`Guile.cpp` |
| `SchemeHost::cons/integer/flonum/stringUtf8/symbol/car/cdr/isPair/isString/isSymbol/isNull/isFixnum/isFlonum/fixnumValue/flonumValue/stringBytes/symbolName/word/False/True/Nil` | the libguile call each one stood for (`scm_cons`, `scm_from_int64`, `scm_from_utf8_stringn`, `scm_car`, `scm_is_pair`, `scm_to_int64`, `SCM_UNPACK`, `SCM_BOOL_F`, …) |
| the `hl--scm->word` / `hl--word->scm` primitives (value smuggling) | nothing — an SCM crosses as an SCM |
| `call-with-blocked-asyncs` around every call | nothing — asyncs are not delivered inside a C function, so the property holds by construction |
| the compat-file load phase, the Chez-only defun phase, the boot-file lookup (with its hardcoded `/home/chris/GITE/chez-pic`) | nothing — those phases do not exist |
| the build-time marshalling | `hl::pin`/`hl::unpin` kept, everything else gone |

**The one piece deliberately kept** is the pinning idiom (renamed from
`marshRoot`/`marshRelease`): Boehm scans the C stack and statics but **not
`malloc`'d C++ containers**, so a value parked in a `std::vector<SCM>` while
a list is stitched can still be collected. That is a real hazard, not a
Chez artifact — the comment in `Bindings.hpp` says so.

**New files, and why they are shaped this way**

- `Bindings.hpp` — `bind<Fn>("name")` wraps any C entry point in a
  template trampoline (registered with a rest argument, so one shape covers
  every arity) that unboxes arguments and boxes the result. Six argument
  types (`SCM`, `long long`, `int`, `unsigned long long`, `double`,
  `const char*`) and five return shapes, which covers all 299. **Arguments
  are validated on the way in**: a wrong type or count raises a Scheme
  error instead of reaching compositor code as a bad pointer — verified in
  a standalone harness (`/tmp/bindtest`, not in the repo): `add-one(41)=42`,
  a string argument, a void return, and both error paths.
- `Guile.hpp`/`Guile.cpp` — the runtime: interpreter startup, contained
  file loads, global lookup, contained calls (errors never unwind through
  C++), and GC pinning.

**Verified end-to-end after the rewrite**: the registered gsubr names and
the names the bootstrap actually calls were cross-checked mechanically
(299 registered; 291 called directly plus 8 passed as values — no unbound
name), and the suite is **12/12**.

**Found and left for the §2.8/§9 leftovers**: two entry points are registered
but referenced nowhere — `hl--c-window-float` (the toggle-only float action
`hl-window-float-set!` superseded) and `hl--c-group-lock-active` — dead C
code, not bugs.

**Also in this step**

- `tools/load-check.scm` rewritten. Its whole purpose was stubbing
  `foreign-procedure`, which no longer exists; it is now a **machinery
  load-check**: it stubs every `hl--c-*` name it finds in the bootstrap,
  loads the prelude and bootstrap in a plain `guile`, and reports any
  top-level form that errors, with `file:line`. Verified in both directions
  (clean machinery → `hl--ready: #t`, exit 0; a deliberately broken file →
  reported, exit 1). It gives the remaining steps a pre-flight that needs no
  compositor.
- `CLAUDE.md`: the machinery list (three `.scm` files → two), the
  `SchemeHost`/`SchemeValue` migration note (now historical), and the
  "foreign calls must wrap in `call-with-blocked-asyncs`" gotcha — which
  applied to the deleted FFI shim and does **not** apply to the gsubr
  boundary (asyncs are not delivered inside a C function).
- The wiki's `building-the-plugin.md` file listing (dropped the compat
  layer), the `Makefile` header comment, and `plugin-main.cpp` — the
  compiled one, which still called the removed `SchemeHost::backendName()`
  (it now says "Guile Scheme scripting for Hyprland").
- Installed: `make install` re-run; the now-unloadable
  `hyprscheme-compat-guile.scm` copy was removed from
  `~/.local/lib/hyprscheme/`. The four Chez-era files there are still
  untouched (see §2.14).

**Process note (an honest one).** The first run of the new bridge was
uniformly red with `Unbound variable: hl--c-*`. Cause: **my own build
sequencing** — I normalised the gsubr names in the `.scm` and the C++
*after* building the `.so`, so the plugin registered `hl--c-hl-*` while the
bootstrap (loaded from the source tree at runtime) called `hl--c-*`. Not a
code defect; a stale binary. It took a standalone bridge test (green) plus a
live `(defined? 'hl--c-version)` probe (bound) to localise it. `tests/run.sh`
now warns when a machinery `.scm` is **newer than the plugin under test**,
which is the guard that would have caught it in seconds.

### §4.3 Step C — native handles (foreign objects + finalizers): DONE

A handle is now a Guile **foreign object**: one slot holding the heap weak ref
to the compositor object, plus a finalizer that deletes it when the Scheme
object becomes unreachable. Six families, six distinct types.

**C++** — new `Handles.hpp`/`Handles.cpp`:

- `Family` enum, `IHandle`/`SHandle<W>` (moved out of SchemeManager) and
  `SNotificationHandle` (the per-handle paused bit).
- `wrapHandle` / `isHandle` / `unwrapHandle`, the last raising a Scheme error
  that names the family it wanted, plus per-family one-liners
  (`windowHandle`/`windowOf`/`isWindow`, …).
- `initHandleTypes()` at interpreter start, and
  `scm_set_automatic_finalization_enabled(0)`: finalizers delete a weak ref,
  which touches compositor state, so they must run on the compositor's thread
  — the machinery pumps them from the GC hook instead (`runFinalizers`, which
  is *contained in C++* because it runs inside that hook).
- Every entry point taking a handle now takes `SCM`; the old `-1`/`0`
  sentinels became `#f` (one place holds the "`#f` = focused window"
  convention); mints return the object; the three queries that returned a
  packed address as a `double` return handles; `hl-*-id` and `hl-handle-free`
  are gone. The dead `fireSchemeBind(int)` (§2.8) went with them.

**Scheme** — the six record families, their mint helpers, the guardian,
`hl--drain-handles!` and the `hl-*-id` accessors are deleted. The predicates
are one-liners over the new gsubrs, getters pass the handle straight through,
and the GC hook pumps the finalizers with a **bare call**: a hook that
allocates re-triggers the collector and livelocks (found today the hard way —
so the error containment lives in C++, not in a Scheme `guard`).

**Layouts**: payloads carry handles; a box with no window gets a *dead* handle,
which is exactly what the documented "every query reads #f" contract needs.

**Effect**: no address ever reaches Scheme, and a handle of the wrong family is
a type error at the boundary — so §2.4 is fixed structurally rather than by a
check in each accessor. About 90 lines of mint/unpack/guardian/drain machinery
disappeared on the Scheme side.

**Verified**: clean rebuild of all objects; `nm` shows no undefined scheme
symbols in the `.so` (an intermediate state had one — caught before it could
fail at load); suite **12/12**.

**Outstanding defect from this step — `tools/load-check.scm` hangs.** The tool
I wrote in Step B now stalls intermittently when it loads the machinery. I
traced part of it (its stubs are variadic, so they cons, and a GC hook that
allocates livelocks — the hook form is now skipped by the tool) but it still
stalled once afterwards, and I have not root-caused it. The suite remains the
verification until it is fixed. **Treat the load-check tool as broken.**

**Process note (honest)**: my regex-driven edits mangled four forms — a literal
`\'()` quoting, an eaten `hl--wid` definition, a lost call-paren, and a body
that referenced the deleted `w`. `tools/syntax-check.scm` catches *reader*
errors only; each of these was found by the suite or the load-check, one run at
a time, which is why this step took several suite cycles.

### §4.4 Step D — the WIPE and the MODULES: DONE

**The design changed from FABLE's.** FABLE §4.4 proposes modules with a generation
that *imports* the API, which leaves the API in one long-lived set of bindings.
Chris's question — "why not wipe the slate clean?" — turned out to be the better
shape, and the probes agreed:

| | copy (was) | import a module (FABLE §4.4) | **wipe (now)** |
|---|---|---|---|
| a config's `set!` on an API name | stays in its generation | **writes the shared module — leaks forever, and reloading the config cannot undo it** | gone with the generation |
| per-reload cost | 397 boxes copied | none | re-evaluating the machinery (measured 0.072 s interpreted) |
| export list to maintain | no | yes, 335 names, unguarded | no |

So the generation is not a copy and not an importer: **the whole Scheme side is
rebuilt from source into a fresh module on every reload.** The thing that made
the leak real — the API living in one place forever — is gone, so there is no
leak to fix and no parameter or hybrid copy needed to fix it.

**Done: `buildGeneration()` (Host.cpp).** Make a fresh module; with it current,
run `registerAllBindings()` (every gsubr, re-registered into the new module, which
is what makes the generation self-contained); load the prelude; load the API; the
caller then loads the config. Built aside and swapped in only if every step
succeeded, so a broken config leaves the previous generation live rather than a
half-built one. `hl--reset` and `hl--generation-copy` are deleted, as is
`hl--load-source` (which nothing had read since it was written).

**Exactly three things live outside the wipe**, each for a measured reason:

- **the stderr port** — now installed once by the host (`Guile.cpp`). A port owns
  the fd it wraps, so a re-created one closes fd 2 when its predecessor is
  collected: the scheme error log dies silently, and twenty of them kill the
  process outright. Both measured.
- **the after-gc hook** — installed once (`installGcHook`), with the finalizer
  gsubr itself as the thunk. `add-hook!` dedupes an identical procedure but not a
  fresh closure, and the prelude is now re-evaluated every reload.
- **the survive list** — the value moves to the host (`hl--c-state-get/-set`);
  the list manipulation stays in Scheme, so `hl-state-*` is unchanged as public
  API.

**`hl--watchdog-ms` keeps working as documented** — and `core.md`'s "tunes the
budget per generation" is now literally true, because the reload rebuilds it from
source. FABLE's parameter change is therefore **not needed**.

**Also fixed on the way**: `g_pendingStart` (a "start" handler subscribed before
the first render frame) was pinned across a reload, so a discarded generation's
handler could be replayed into the new one — now dropped at the reload boundary.

**Verified**: `tools/bind-audit.py` → 308 registered, 306 called, 0 unbound, 0
duplicates, 0 undefined in the `.so`; clean rebuild; suite **12/12** against the
tree build and again through the installed plugin. **Six generation rebuilds
observed in one suite run** (temporary fd-2 probe, since `LOG()` is swallowed),
so the wipe provably runs per reload; that code path did not exist before.

**New tests** in `t-api.sh`, after the reload: a generation-scoped `define` is
unbound, and `hl--watchdog-ms` is back to 5000. Honest note: those two cases
behaved the same under the copy model, so they pin the *contract* ("a reload is a
clean slate") rather than discriminating the implementation.

**Still not started from §4.4**: modules, the export list, `%load-path` seeding,
and the §10 file layout. Two probes say the case for modules is weaker than
FABLE implies: a plain top-level file compiles to `.go` and `load-compiled` runs
it (so Step F needs no modules), and `(load "other.scm")` already works scoped to
the generation (so config-splitting needs none either). What is left for modules
is an explicit export list in place of the `hl-` naming convention, and
`use-modules` by name — plus a kernel module, since a fresh module cannot import
the flat once-part, and a `globalRef` that resolves against a named module.



The machinery is now three Guile modules instead of two flat `.scm` files, and
**the file paths are the module names**:

| file | module | what it is |
|---|---|---|
| `hyprscheme.scm` | `(hyprscheme)` | the **public API** — a curated `#:re-export` of **310** names. This list is the single source of truth for "what is public". |
| `hyprscheme/kernel.scm` | `(hyprscheme kernel)` | the machinery, and the layer the **host** reaches: watchdog, fire trampolines, layout entry points, and the record types those trampolines read. 55 exports. |
| `hyprscheme/api.scm` | `(hyprscheme api)` | the API itself, public and internal. 337 exports. |

The generation a config runs in imports the **API and the kernel directly**, not
the umbrella — so the `hl--` helpers stay reachable exactly as they were when
everything was one flat environment. `(hyprscheme)` exists for configs and tools
that want the public list alone; it exports 310 names and neither the internals
nor the record representation (`hl--plist-get`, `hl--fire-str-rec`, `hl-bind-rtd`
are all absent — verified against the interface).

The host (`buildGeneration`) now creates each module, defines its bindings into
it, reads its file, then assembles the generation. **`%load-path` gets the
machinery directory**, which is what makes the module names resolve.

`t-coverage` reads the umbrella's `#:re-export` list instead of scraping a file
with a `define` pattern — the payoff FABLE gives for the module work, and the
reason it matters: that test scraped an in-C++ copy for its entire life and
passed on an empty list. The vacuity floor stays.

**Six things the module conversion taught, each of which cost a cycle** (they are
now in CLAUDE.md so the next person does not pay again):

1. **`module-export-all!` does not reach a later importer.** It makes bindings
   visible to `module-variable`, but a module that `use-modules`/`#:re-export`s
   it afterwards does NOT see them. Every module needs an explicit `#:export`.
2. **`#:re-export` re-exports what the module has already imported** — the
   umbrella needs `#:use-module (hyprscheme api)` (and the kernel) first.
3. **A module's bindings must exist before its file is read**, because its
   `#:export` names them. Hence the host defines the kernel's two gsubrs
   (`hl--c-generation`, `hl--c-run-finalizers`) *before* reading `kernel.scm`.
4. **Guile hoists top-level defines**, so `(define hl--base-load load)` inside a
   module file captures that file's own `load` shadow. It must be
   `(@ (guile) load)`. And a module that shadows `load`/`eval` needs
   **`#:declarative? #f`**.
5. **Imports are not transitive**, and the machinery had been relying on one flat
   environment: the API needs its own `(srfi srfi-1)` etc., and the *generation*
   gets the same list so a config keeps the environment it always had
   (`call-with-string-output-port`, `every`, …).
6. **The kernel must not depend on the API** (the API imports the kernel). The
   record types and their accessors therefore live in the kernel — which is also
   the right home for them, since the trampolines read those records.

**Two things the host must not do**: `add-to-load-path` is a *syntax
transformer*, not a procedure (set `%load-path` as a variable), and **an
exception escaping into C++ corrupts the compositor heap** — one uncontained
lookup produced `corrupted size vs. prev_size` and killed the session. Every
C++→Scheme call goes through the contained wrappers.

**Process note (honest, and the worst of this pass).** Two of my block-slicing
edits to `Guile.cpp` deleted live functions — `globalRef`, `makeFreshModule` and
all of `call0..call3`. One was caught by the compiler, the rest only at plugin
load; I found them by diffing `Guile.hpp` against `nm` on the `.so`. Separately,
a script that moved twelve definitions out of `api.scm` wrote the file and *then*
failed its own assertion, leaving the definitions in neither file — recovered
from the pre-module copy that `make install` had left in
`~/.local/lib/hyprscheme/`. Both are the same mistake: editing by slicing
between index markers. **Fix forward with targeted edits, and verify the tree
compiles and the suite is green before believing a script's output.**

**Verified**: `tools/syntax-check.scm` clean; `tools/load-check.scm` exit 0 with
`hl--ready: #t`; `tools/bind-audit.py` → 308 registered, 306
called, 0 unbound, 0 duplicates, 0 undefined in the `.so`; clean rebuild; suite
**12/12** against the tree build and again through the installed plugin.

**Also**: the superseded `hyprscheme-prelude.scm`/`hyprscheme-bootstrap.scm` were
removed from `~/.local/lib/hyprscheme/`, and `make install` now creates
`hyprscheme/` there. The Chez-era leftovers (defun file, two boot files,
`scheme-plugin.so`) are still untouched, as §2.14 records.

**Still not done of §4.4**: per-family module files (§10's `hyprscheme window.scm`
… — the umbrella's list would not change, only where the definitions live), and
the §10 directory move.

## §5, the selector gap — DONE

The headline item in FABLE §5 ("selector strings for focus/move — the largest
real gap") turned out to be mostly **already working**, and the real defect was
different from what FABLE describes. FABLE says the Scheme actions "resolve by
name only"; in fact `Config::Actions::changeWorkspace(const std::string&)`
resolves internally, so `hl-workspace-focus!` has always accepted the grammar.

**What was actually wrong: we stringified handles.** Every action taking a
workspace or monitor converted it to its *selector text* at the Scheme boundary
(`hl--ws-arg`/`hl--mon-arg`) and left C++ to look the text back up — and the
lookup it used was a **name** comparison (`monitorFromName`, `workspaceFromName`)
rather than the grammar. So:

- a handle worked (its name always resolves), but wastefully;
- a **selector string could not work at all** — the grammar was unreachable
  through those paths, which is the whole of the "gap";
- and the C++ underneath wanted the *object* all along: upstream's actions are
  `changeWorkspace(PHLWORKSPACE)`, `focusMonitor(PHLMONITOR)`, and so on, with
  exactly **one** string overload in the whole action API. Passing objects is
  upstream's own model — its Lua bindings do the same (`windowFromUpval`).

**Done:**

- **A workspace/monitor argument is now one shape everywhere**: a handle, a
  number, or a selector string. New `workspaceArg`/`monitorArg` (Workspace.cpp,
  declared in SchemeInternals.hpp) use a handle **directly** and resolve only a
  string — a handle is never re-derived from text.
- **Six entries changed** from name lookup to the grammar: `hl-monitor-focus!`,
  `hl-monitor-power-set!`, `hl-monitor-swap!`, `hl-workspace-monitor-set!`,
  `hl-workspace-name-set!`, `hl-workspace-id-set!`. Both forms now work:
  `(hl-monitor-focus! <handle>)` and `(hl-monitor-focus! "+1")`.
- **`hl-workspace-from` added** — selector → handle, the mirror of the existing
  `hl-monitor-from`. It *finds only*, which is why the actions take a selector
  directly rather than being composed from it (the composed form would fail
  where `changeWorkspace` creates).
- `hl-workspace-focus!` was deliberately **left alone**: it passes its string to
  `changeWorkspace(string)`, which resolves *and creates* and honours
  `previous` / `binds:workspace_back_and_forth`. Resolving first would lose all
  three.
- **Docs**: both grammars documented once, in `dispatchers.md`
  ("Workspace selectors" / "Monitor selectors"), with `monitors.md` and
  `scheme-utilities.md` pointing at them.
- **Tests**: selector forms for both object types, the handle form, a
  `hl-workspace-from` lookup, and a miss. An existing test caught a regression
  on the way — `(hl-workspace-id-set! 50 …)` passes a workspace **number**, so
  `workspaceArg` accepts integers too.

**Verified**: `make check` clean (syntax-check, module-audit, load-check,
bind-audit: 307 registered, 307 called, 0 unbound, 0 dead, 0 undefined in the
`.so`); suite **12/12**.

**Still open from §5**: `hl-window-swap-with!` takes a window handle where Lua's
takes a target *selector*, and Lua's `dsp.workspace.move` with the current
workspace implied. Both are small and separate.

## §4.5 Step E, and the §9 leftovers — NOT STARTED

- **§4.5 Step E** — native error reporting: backtraces + source location, log +
  rate-limited notification (the `core.md` claim).
- **§10, the directory move** — `src/hyprscheme/` + `src/plugin/`; cosmetic, and
  it touches five hardcoded `src/config/scheme` paths.
- **§2.8 + §9 leftovers** (much reduced by Step B, which already removed every "Chez"
  mention from the C++ — comments included — deleted the dead boot-file
  lookup, rewrote `tools/load-check.scm` and refreshed the docs it touched)
  what is left: a final read-through
  of the remaining historical notes (`GUILE-CONVERSION.txt` is deliberately
  kept as the record; `DONE.txt`/`TODO.txt` still describe the two-backend
  era).

### §4.6 Step F, the compile lint — ATTEMPTED, NOT LANDED

`-Wunbound-variable` is the only thing that sees "a name used but never defined"
in a lambda body: the reader does not resolve it and `load-check` only finds it
if the form actually runs. So it is worth having. It is blocked, and here is
exactly where:

- **Compilation needs the dependencies compiled**, not just present: a module
  that imports another fails with "no code for module" unless that dependency is
  already a `.go`. So the machinery must be compiled **in dependency order** —
  kernel, core, families, extras, umbrella — which also means Step F's lint and
  its `.go` speedup are the same piece of work.
- **Compiled files are found on `%load-compiled-path`, not `%load-path`**, and
  `guild` has no `-C`. The env vars `GUILE_LOAD_PATH` + `GUILE_LOAD_COMPILED_PATH`
  are the way in (verified: with `-C`, a stub-kernel `.go` loads and resolves).
- **The kernel cannot be linked ahead of time** at all: its export list names
  every `hl--c-*` gsubr, which does not exist until the compositor registers it.
  It needs a generated stub (a module of no-op definitions) to compile against —
  which worked, and let 10 of 16 modules compile clean.
- **The remaining six are blocked by a Guile quirk**, isolated by bisection:
  every module that uses `#:key #:allow-other-keys #:rest` in a `define*` fails
  to LINK with `Undefined symbol #f`. The correlation is exact — `config`, `exec`,
  `layer`, `monitor`, `window`, `workspace` use the idiom and fail; the other ten
  do not and compile. `hl-device-add!` alone reproduces it.

So the lint needs either that idiom to change, a different compile path
(`compile-file` through the Guile API rather than `guild`), or a Guile that links
it. The tools built for it were removed rather than left half-working; the
findings are above.

### §4.7 Step G — split `SchemeManager.cpp`: DONE

The pattern (FABLE §4.7's "the registration list becomes a per-file
`registerX()`"): each family is its own translation unit that **defines its
entry points and registers them**, and `attachInterp` calls its `registerX()`.
Nothing outside a family needs its entry points declared, so the ~300-line bind
list dissolved family by family. **`SchemeManager.cpp` no longer exists**: it is
`Host.cpp` (764 lines) and it is the last file to be cut.

**All 15 translation units now**: Host (init, reload, inotify watch, IPC, crash
handler, the watchdog, `attachInterp`, `shutdown`) + Window (943), Query,
Monitor, Workspace, Config (739), Notification, Gesture, Rule, Exec, Bind,
Event, Group, Timer (185), Layer (182) + `SchemeLayout.cpp` (custom layouts,
untouched, as agreed). `SchemeManager.hpp` → `Host.hpp`.

- **`SchemeInternals.hpp` is the one shared header**, and it grew the
  handle/result plumbing the families had each been duplicating:
  `boolResult`, `windowHandleResult`, `monitorHandleResult`,
  `workspaceHandleResult`, `wsGet`/`monGet` (templates, hence `inline` here),
  `actionResult`, `actionDir`, plus declarations for `actionWindow`,
  `windowOrFocused`, `windowMatchesSelector`, `monitorFromName`,
  `workspaceFromName`, `specialWorkspaceFromName`, `workspaceFromSelector`,
  `configScratch`, `modsMaskFromTokens`, `watchdogEnter/Exit`.
- Family-private state stayed `static` in its own file (`g_timerIndex`,
  `g_windowRules`/`g_ruleIndex`, the gesture makers, the device table). State
  the *host* tears down is exposed as a function instead of a shared global —
  `cancelAllTimers()` (Timer), `clearSchemeRules()` (Rule),
  `dropEventHandlers()`/`shutdownEvents()`/`registerStartDispatch()` (Event).
  That last one also **removed a real duplication**: the once-only "start"
  listener was written out twice, in `init` and in `reloadScheme`'s soft
  re-attach path; it is one function now.
- **Two deviations from the ledger's table**, both deliberate: the three
  lifecycle globals (`g_pendingStart`, `g_startSeen`, `g_lifecycleListeners`)
  went to Event.cpp with the accessors above rather than being reached from the
  host directly; and `modsMaskFromTokens` / `windowMatchesSelector` were moved
  out of the host into Exec.cpp / Query.cpp so `Host.cpp` is purely host
  concerns.
- **Verified**: `make clean` + full rebuild (18 objects); `tools/bind-audit.py`
  → 305 registered, 303 called, 0 unbound, 0 duplicates, **0 undefined in the
  `.so`**; suite **12/12** against the tree build, and again after
  `make install` through the default installed path with no freshness warning.

**The extractor** (`tools/split-family.py`) was a migration tool and is
**deleted**, as agreed — the same call as the Chez→Guile scaffolding. It earned
its keep; four bugs in it had to be fixed first, each found by the compiler or
the suite rather than by reading:

1. it swept registrations by *name prefix* (the pilot's layer-rule mistake) —
   ownership follows the **bound function**, never the name;
2. `\s*` after the `;` in the variable/declaration patterns matches **newlines**,
   so a cut swallowed the comment belonging to the *next* declaration;
3. `DEVICE_FIELDS[]`'s array declarator defeated the pattern, and the greedy
   `[^;]*` initializer would have run to the last semicolon in the file;
4. offsets were computed against the progressively-cut text, so a name asked for
   twice (the `schemeIntList` overload pair) came out **reversed**.

**The audit script is kept** — the pilot's was inline and lost, and it is worth
more than it was before the split: `tools/bind-audit.py` now also runs
`nm -D --undefined-only` on the built `.so`. See the process note below for why
that check is the one that matters.

- **Process note (honest).** One suite run went red with `undefined symbol:
  _ZN6Config6Scheme13registerEventEv` at plugin load — `Event.o` had never been
  added to the Makefile's `COMMON_OBJS`. The `.so` **links anyway**: a family
  object missing from the link line is invisible to the compiler and to the
  source-level audit, and shows up only when the compositor dlopens the plugin.
  That is what the `nm` check in `bind-audit.py` is for, and why the clean
  rebuild at the end is not optional.
- **Process note 2.** Cutting Event went wrong once: the tool wrote
  `SchemeManager.cpp` and then failed its header assertion, and a later restore
  from a backup left the whole family **duplicated** in both files. The
  duplicate was found mechanically (`definitions_in` on both files, intersect —
  23 names) rather than by eye, and the leftovers removed. Fixed at the root
  too: the two insertion points are now explicit marker lines
  (`tools/split-family.py inserts the next family above this line`) instead of a
  regex hunting for "the last `registerX()`" — the host has its own
  `registerIpc()`/`registerStartDispatch()`, and no regex can tell those from a
  family's.
- **Docs**: the 13 family files carry a one-line header naming the family; the
  stale "State shared between SchemeManager.cpp and SchemeLayout.cpp" and
  `SchemeLayout.*`/`plugin-main.cpp` references to the old filename are gone.

### §10 — the API split into per-family modules: DONE

The API is now **17 modules**, and a family file is *raw*: thin wrappers over
what the compositor can actually do.

| module | contents |
|---|---|
| `(hyprscheme kernel)` | the machinery **and the whole C++ boundary** — every `hl--c-*` gsubr, its export list generated from the C++ `hl::bind<>()` registrations |
| `(hyprscheme core)` | shared plumbing: coercion, the plist convention, key-spec parsing, the handle predicates, the state accessors, the listen helpers |
| 14 families | `window` (94 defs) `monitor` (40) `workspace` (34) `notification` (21) `exec` (18) `layer` (16) `group` (15) `gesture` (12) `config` (10) `bind` (9) `query` (6) `event` (4) `timer` (4) `rule` (3) |
| `(hyprscheme extras)` | the 7 conveniences that need several calls to compose |
| `(hyprscheme)` | the public umbrella: the curated re-export list, **311 names** at this point (**312** once `hl-workspace-from` lands, §5) |

**The classification was measured, not guessed.** Of 309 public definitions: 200
are pure passthroughs (one gsubr, nothing else), ~85 only coerce arguments, and
**7 genuinely compose** — those 7 are extras. Splitting on a crisp rule rather
than by eye is what made the boundary defensible: *a function belongs to extras
if it calls another public `hl-*`*; a call to a `hl-*`/core helper is plumbing,
and the first two attempts at this measurement were wrong precisely because they
counted names found in **docstrings** as calls.

**The raw API got its missing operation.** `hl-workspace-special-set!` was
composing on/off out of the compositor's toggle by reading state and comparing
(that is why it needed three other calls, and the only reason any family
appeared to depend on another). The raw family now exposes
`hl-workspace-special-toggle!` — the compositor's actual operation — and the
on/off convenience lives in extras, built on it.

**The rule is enforced**: `tools/module-audit.py` checks that a family imports
only the kernel and core and calls nothing another family owns, and that only
extras composes. Verified both ways — a deliberate cross-family call makes it
exit 1, and removing it passes again. (A check that cannot fail is the trap
`t-coverage` spent its whole life in.)

**Five things this pass cost, all now in CLAUDE.md**: `module-export-all!` does
not reach a later importer, so every export list is explicit; a module's
bindings must exist before its file is read (the kernel's gsubrs before
`kernel.scm`); `define-module` is expanded in the *current* module, so
`loadModule` loads from a base module rather than from one of ours; `set!` of an
imported binding is a syntax error in a declarative module, so **the host** sets
`hl--ready` after the last module loads; and Guile hoists top-level defines, so
the trailing `(set! hl--ready #t)` of the old api file got absorbed into the body
of whichever definition the splitter cut last.

**The audit earned its keep**: `bind-audit.py` caught that consolidating the
registrations had silently dropped `hl--c-run-finalizers`, which would have left
the collector's finalizer queue never drained — a real regression, invisible in
the suite, found by the tool.

**Verified**: `syntax-check` clean on 18 files; `module-audit` ok; `load-check`
exit 0, `hl--ready: #t`; `bind-audit` → 308 registered, 308 called, 0 unbound,
0 duplicates, 0 undefined in the `.so`; clean rebuild; suite **12/12** against
the tree build and through the installed plugin.

**Process note (honest).** Two more scripted-edit mistakes, both caught: the
split's first classifier counted docstring text as call sites (twice), and the
`(set! hl--ready #t)` absorption above. The lesson from the previous pass — do
not slice between index markers — held up; these were *classification* errors,
where the fix is to measure twice and to verify a tool can fail before trusting
it.

## Files changed — the bug-fix pass

Repo (`Hyprscheme`): `tests/run.sh`, `tests/t-api.sh`,
`tests/t-coverage.sh`, `tests/t-zz-example.sh`, `tests/t-zzz-docs.sh`,
`tests/t-zz-exit.sh` → `tests/t-zzzz-exit.sh`, `tests/wiki-examples.skip`,
`src/config/scheme/hyprscheme-bootstrap.scm`, `README.md`, `FABLE.md`
(corrected claims + new §2.13–2.16), this file.

Wiki (`../Hyprscheme.wiki`): `submaps.md`.

Install dir (`~/.local/lib/hyprscheme/`): refreshed by `make install`
(no deletions).

## Files changed — the Chez purge, §4.1 Step A

Repo: `src/config/scheme/hyprscheme-prelude.scm` (rewritten natively),
`src/config/scheme/hyprscheme-compat-guile.scm` (280 → 68 lines: the
C-function bridge only), `src/config/scheme/hyprscheme-bootstrap.scm`
(native records with type checks, `hl--error`, dialect call sites,
`after-gc-hook`), `tests/t-api.sh` (the error-capture idiom, the
`hl-notification?`/`hl-workspace?` predicates, the wrong-family-handle
regression test), `tests/t-config.sh` (same idiom), `examples/hyprland.scm`,
`CLAUDE.md` (the machinery list).

Wiki: `Home.md`, `core.md` (Chez → Guile), `custom-layouts.md`,
`notifications.md`, `bind-gestures.md` (dialect + the keyword key bug).

Install dir: refreshed with `make install` after the run (the plugin
prefers the source tree's `.scm` in a dev build, so the suite always
tested the current files — but the installed copies should not lag).
The Chez-era files there (`hyprscheme-defun.scm`, the two boot files,
`scheme-plugin.so`) are still untouched, as recorded under §2.14.

**Suite: 12/12** on the final state of every file above.

## Files changed — §4.7 Step G (the C++ split)

Repo: `SchemeManager.cpp` → `Host.cpp`, `SchemeManager.hpp` → `Host.hpp`, and
**new**: `Window.cpp`, `Workspace.cpp`, `Monitor.cpp`, `Group.cpp`,
`Event.cpp`, `Timer.cpp`, `Rule.cpp`, `Config.cpp`, `Notification.cpp`,
`Gesture.cpp`, `Exec.cpp`, `Query.cpp` (`Layer.cpp` and `SchemeLayout.cpp`
were already there). `SchemeInternals.hpp` is the shared header;
`tools/bind-audit.py` is new; `tools/split-family.py` was the migration tool
and is deleted; `Makefile` (`COMMON_OBJS` + its comment), `CLAUDE.md` (a new
"C++ layout" section, and the stale-blobs note).

No `.scm`, `tests/` or wiki change was needed: the split is a pure
rearrangement of the C++ — the registered names, the API and every behaviour
are unchanged, which is what the audit and the suite are there to establish.

**Suite: 12/12** on the final state, twice — against the tree build, and
through the installed plugin after `make install` (no freshness warning).

## Files changed — the layout-callback pass (§2.5, §2.15, §12.2)

C++: `SchemeLayout.cpp` (the new payload — `boxRtd`/`isBox`/`boxNumber`/
`boxToScheme`/`readPlacement`/`placementList`, `applyBoxes` matched by window,
the dead `schemeIntList` removed, `boxToScheme` emitting exact integers),
`SchemeLayout.hpp` (the contract, and the CAVEAT about there being no watchdog
removed — there is one now), `Guile.cpp`/`Guile.hpp` (`call4`, `CallArgs` grown
to five), `Window.cpp` (`hl-window-group`).

Scheme: `hyprscheme/core.scm` (the `hl-box` record + accessors),
`hyprscheme/kernel.scm` (the `(area placements)` destructuring, the watchdog
around the layout paths, the stale comment), `hyprscheme/window.scm`,
`hyprscheme.scm` (the umbrella's re-exports), and the export lists regenerated.

Tests: `t-api.sh` (boxes, `hl-window-group`, and a driven custom-layout round
trip that asserts the layout settles — `in` equals `out` — plus a partial
return), `t-watchdog.sh` (the runaway-layout case, §2.15), **new**
`t-zzz-monitors.sh` (§2.5's own scenario: a second output, the primary off the
origin, a layout placing windows at the work area corner), `soak.sh` (Chez
default, layout shape, `exact`, the positional rule setter).

Docs: `custom-layouts.md`, `README.md`, `examples/hyprland.scm`, `CLAUDE.md`
(the callback contract under API conventions).

Tools: `tools/module-audit.py` gained the export checks — every top-level
define must be in its own module's `#:export`, and every public name the
families and core export must reach the umbrella's `#:re-export`. That is the
silent-invisibility bug this pass kept hitting; both rules were verified to
fail on a deliberate removal. (Kernel and extras are exempt from the second:
the kernel is the gsubr boundary and the generation imports it directly, which
is how a config's `(load …)` reaches the shadow — the first run of the new
check flagged `load`/`eval` and was right to, until that was understood.)

**Suite: 13/13** — the 13th file is `t-zzz-monitors.sh`, added by this pass for
§2.5 — with `make check` green on all four lints, and a 25s `soak.sh` at
`VERDICT: PASS`. Run twice, no flakes.

## The wiki's snippets, verified three ways (Chris's request) — DONE

Not a FABLE item: Chris asked for the code-snippets page to be brought over in
full and for a way to *exercise* what it contains. The second half turned out to
be the useful one, because the existing harness loads every snippet without ever
calling one.

### What the doc test could not see

`tests/wiki-examples.awk` extracts all 125 `scheme` blocks with a **balance**
check, and `t-zzz-docs.sh` evaluates each one, failing only on `error:`. So a
block containing

```scheme
(hl-bind-add! (hl-kbd "s-G") (lambda () ... (hl-window-into-group #f "l") ...))
```

passes: the lambda is *registered*, never called, and nothing inside it is ever
looked up. Block 0049 — the Vim-like keymaps snippet — was balanced, unskipped,
evaluated on every run, and contained **five names that do not exist**, with the
suite green.

### Three tiers, each answering a different question

| | tool | answers | fails on |
|---|---|---|---|
| A | `tools/doc-audit.py` | does every `hl-*` name the wiki uses exist? | an unknown name, in a block or in prose |
| B | `tools/doc-exercise.scm` | does every callback the wiki registers RUN? | `unbound-variable`, wherever inside a thunk it hides |
| C | `tests/t-zzz-snippets.sh` | does a snippet DO what it claims? | a composition that produces the wrong effect |

A is textual on purpose — that is the only way to see inside a thunk — and it
found all five names immediately. B goes further: it loads the blocks into the
environment a config gets, replaces every *registering* function (derived from
the API's own export list: every `-add!`, plus `hl-submap`/`hl-repeat`/`hl-after`,
so the list cannot drift) with a wrapper that records the procedures it is
handed, and then **calls them** — repeating, because the binds inside an
`hl-submap` thunk are only registered once that thunk runs. C extracts the block,
pulls the thunk out of the form and runs it against the live compositor, so it
tests the *documented* code rather than a copy that can drift.

Each was verified to fail: A on a reintroduced bad name, B on the same name put
back inside the submap thunk, C on a snippet broken so its two halves disagree
(drop the tag and the restore half cannot find the window again). A check that
cannot fail is worth nothing — this repo has that lesson twice already.

### B paid for itself on its first run

It found a **real bug in `scheme-utilities.md`**: three snippets used `(print …)`
inside timer callbacks. `print` is not a Guile binding — not in `(guile)`,
`(ice-9 pretty-print)` or `(ice-9 format)` — so every reader who copied them
got `Unbound variable: print`. `t-zzz-docs` passes them because the name sits
inside a timer thunk that never runs. Fixed to `(display …)`, which is bound.

### Two bugs in `load-check.scm`, found while building B

`make check` has been running a check that was weaker than it read. Both bugs
made every module's bindings unreachable, and both are now fixed:

- **`define-module` evaluated form-by-form does not switch the current module** —
  only the loader's own handling of it does. So every file was being defined into
  `(guile-user)`, each module kept nothing but its unassigned `#:export`
  placeholders, and the check proved only that the forms *evaluated*. The fix is
  one `set-current-module` after that form.
- **The C entry points were stubbed into `(hyprscheme api)`**, not the kernel —
  where the host puts the real ones. The kernel's own `hl--c-*` placeholders
  stayed unassigned, so a call through one was an unbound variable that this
  check could not see.

Verified: `hl--c-window-close` and a core name (`hl-kbd`) now dereference to
procedures in the generated module; before, both were unbound. (The `doc-*` tools
build the machinery the same way, so they would have had the same holes — which
is why the first version of B reported `hl-kbd` as unbound.)

### The page itself

Rewritten to the page's own structure, section for section, with all 16 snippets
in Scheme. Fixed: the five names above; the group toggle; a smart-gaps variant
missing four of its six lines; a sentence duplicated twice; three paragraphs of
analysis scaffolding left in the prose; and the browser-extension snippet, which
was a simplification where the original is a two-phase open-then-watch (now
faithful, with `hl-notification-remove!` doing the original's `sub:remove()`).
`Per workspace layouts` is kept — an addition to this wiki, verified correct.

**Suite: 14/14**, `make check` green on all six lints, soak `VERDICT: PASS`.

### Two real bugs the doc work turned up, and what found them

Chris asked whether there were bugs, having seen errors on screen. There were
two, and neither was visible to any of the checks above — they were found by
reading the **compositor log** after a suite run:

```
[scheme] error: Unbound variable: hl--plist-has?          (x4)
[scheme] error: string-append: Wrong type (expecting string): #<hl-workspace …>
```

**1. The kernel called a name it could not see.** `hl--plist-has?` was defined
in `core.scm`, and **`kernel.scm` called it** — but the kernel imports nothing
of ours (core imports the kernel; that is the whole layering). Its sibling
`hl--plist-cdr` had already been moved into the kernel for exactly this reason;
`hl--plist-has?` was left behind. The effect was not just a log line:
`hl--bind-result` normalizes a bind callback's return, so a callback returning a
**plist** raised, the guard caught it, and the result was read as **DECLINED** —
the key was passed through instead of consumed, and any `'ok #t` in the plist
was lost.

Fixed by moving the helper into the kernel, beside `hl--plist-cdr`, with the
reason in a comment. **And made checkable**: `tools/module-audit.py` now
verifies that a module can see every name it calls — its own bindings, its
`#:export` (the host defines some of the kernel's), and what its imports offer.
Verified to fail: adding a call from the kernel to `hl-kbd` (core's) reports
`calls hl-kbd, which it cannot see`, which is the exact shape of this bug.

**2. `events.md` said the workspace event passes a name.** It passes a
**handle**:

```scheme
(hl-workspace-active-notification-add! (lambda (name)
    (hl-exec! (string-append "notify-send 'Workspace: " name "'"))))
```

`string-append` on a handle raises `Wrong type` for every reader who copied it.
The prose above it even promised "an event that passes a name". Fixed to take a
`ws` and ask it for `(hl-workspace-name ws)`.

This one is instructive because **every tier above is blind to it**: the names
all exist (A), the block registers cleanly (t-zzz-docs), and B can only hand a
callback a fabricated `#f` — which makes a type error indistinguishable from
the harness's own fault, so it is a note, not a failure. `t-zzz-snippets.sh` is
the tier with REAL values, so the fix is that it now drives this snippet too:
`run-snippet-fn` takes an argument, and the test passes it `(hl-active-workspace)`.
Verified to fail on the original snippet with
`string-append: Wrong type (expecting string): #<hl-workspace …>` — the same
error the log had.

**Suite: 14/14**, six lints green, and the compositor log now carries only the
three errors the tests ask for (two deliberate watchdog runaways, and `t-api`'s
`boom` error-handling case).
