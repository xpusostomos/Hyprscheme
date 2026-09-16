# Hyprscheme

Chez Scheme scripting for [Hyprland](https://hypr.land), as a loadable
plugin. Write your window management logic — keybinds, layouts, timers,
event reactions, queries — in Scheme.

```scheme
;; ~/.config/hypr/hyprland.scm
(hl-bind "SUPER" "U" (lambda () (hl-exec "foot")))

(hl-define-layout "master"
  (let ((ratio (vector 0.5)))
    `((recalculate . ,(lambda (count W H windows)
                        (let ((mw (inexact->exact (floor (* W (vector-ref ratio 0))))))
                          (if (<= count 1)
                              (list (list 0 0 W H))
                              (let* ((n (- count 1))
                                     (sw (- W mw))
                                     (sh (quotient H (max 1 n))))
                                (cons (list 0 0 mw H)
                                      (let loop ((i 1) (acc '()))
                                        (if (= i count)
                                            (reverse acc)
                                            (loop (+ i 1)
                                                  (cons (list mw (* (- i 1) sh) sw sh)
                                                        acc))))))))))))))
```

A full API — binds, timers, window queries and actions, events, custom
layouts with state, submaps — runs inside the compositor via an embedded
Chez Scheme interpreter. The scripting errors are contained: a broken
config or a failing callback never takes the compositor down.

## Status

Working experiment. Verified: eval channel (`hyprctl scheme '...'`),
binds, exec, timers, window queries/actions, events, stateful custom
layouts, submaps. Known limits: no layout watchdog (a hung layout
callback hangs the compositor), no `minimize` action (upstream gap),
load-time layout selection only.

## Requirements

- Hyprland built from source matching your running compositor (the
  plugin compiles against its headers — see "Version matching")
- A Chez Scheme kernel built as position-independent code (the
  `build-chez.sh` script produces one)

## Building

```sh
make build-chez    # one-time: fetch and build PIC Chez into build/chez
make               # build the plugin against the Hyprland headers
```

Two inputs are selectable:

- `CHEZ_OUT` — where the PIC Chez build lives (default `build/chez`;
  if you built the kernel yourself — e.g. via the upstream `--pic`
  flag, see BUILDCHEZ.md — point this at it and skip `build-chez`)
- `HYPRLAND_SRC` — the Hyprland source tree to compile against
  (default `/tmp/hl-clean`; must match your running compositor)

## Version matching

The plugin resolves Hyprland's own symbols at load time, so it must be
compiled against headers matching the running compositor. Point
`HYPRLAND_SRC` at the source tree of the exact build you run — a git
worktree of the upstream commit you're on works well.

## Installing

```sh
make install    # PREFIX defaults to ~/.local
```

Then add to your Hyprland config:

```
plugin = ~/.local/lib/hyprscheme/scheme-plugin.so
```

and reload. Plugin directives must come before the config lines that
depend on it.

## Usage

At startup the plugin loads `~/.config/hypr/hyprland.scm` and re-loads
it whenever the file changes. From a terminal:

```sh
hyprctl scheme '(+ 1 2)'
hyprctl scheme '(hl-active-title)'
```

evaluates Scheme in the running compositor. See `examples/` for a
tour of the API: binds with options, timers, window queries and
actions, events, stateful custom layouts, submaps, cross-reload state.

## Packaging (AUR)

`packaging/PKGBUILD` builds the plugin plus a matching Hyprland from
pinned source commits and installs to `/usr/lib/hyprscheme/`. The
plugin only loads into a Hyprland built from the same commit — the
package therefore ships a matching compositor build rather than
patching the system one.

## How it works

The plugin statically embeds a position-independent Chez Scheme kernel
(see `BUILDCHEZ.md` for the build) and registers Scheme callbacks as
first-class citizens of the compositor: keybinds fire Scheme closures,
custom layouts are Scheme functions returning geometry, events deliver
window handles as Scheme records. Crash reporting survives the Chez
runtime's own signal handling via a re-installed handler that calls
Hyprland's exported crash reporter.
