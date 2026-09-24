# Hyprscheme

Chez Scheme scripting for [Hyprland](https://hypr.land), as a loadable
plugin. Write your window management logic — keybinds, layouts, timers,
event reactions, queries — in Scheme.

```scheme
;; ~/.config/hypr/hyprland.scm
(hl-bind-add! (hl-key "SUPER+U") (lambda () (hl-exec! "foot")))

(hl-layout-add! "master"
  (let ((ratio (vector 0.5)))
    'recalculate (lambda (count W H windows)
                   (let ((mw (exact (floor (* W (vector-ref ratio 0))))))
                     (if (<= count 1)
                         (list (list 0 0 W H))
                         (let* ((n (- count 1))
                                (sw (- W mw))
                                (sh (quotient H (max 1 n))))
                           (let loop ((i 1) (acc (list (list 0 0 mw H))))
                             (if (= i count)
                                 (reverse acc)
                                 (loop (+ i 1)
                                       (cons (list mw (* (- i 1) sh) sw sh)
                                             acc))))))))))
```

A full API — binds, timers, window queries and actions, events, custom
layouts with state, submaps, trackpad gestures — runs inside the
compositor via an embedded Chez Scheme interpreter. Scripting errors
are contained: a broken config or a failing callback never takes the
compositor down.

## Status

Working experiment. Verified: the eval channel (`hyprctl scheme '...'`),
binds (with flags, devices, submaps), exec, timers, the full window/
workspace/monitor query-and-action surface, events (all of the
compositor's event bus), notification objects, window/layer/workspace
rules, trackpad gestures (built-in and callback actions), stateful
custom layouts, per-device config, cross-reload state. User-facing docs
live in the wiki (`../Hyprscheme.wiki`).

## Requirements

- **Hyprland from source, built in place** — the plugin compiles
  against its headers and runs inside the binary built from the same
  tree (`../Hyprland` by default; `make` builds it if missing)
- **Chez Scheme** built as a position-independent kernel, checked out
  as `../ChezScheme` (built in place by `make` when needed; see
  `BUILDCHEZ.md` for the details)

## Building

```sh
make    # builds Chez and Hyprland as needed, then the plugin
```

Everything builds **in place** — no copies, no staging:

- `CHEZ_DIR` — the Chez Scheme checkout (default `../ChezScheme`);
  built with `CFLAGS=-fPIC` (or the upstream `--pic` flag when
  present). The plugin consumes its workarea objects and boot files
  directly.
- `HYPRLAND_SRC` — the Hyprland checkout (default `../Hyprland`),
  built in place in `<tree>/build`. The plugin compiles against those
  headers, and the compositor binary is a make prerequisite — a rebuilt
  Hyprland forces a plugin relink automatically.

See the wiki's [[building-the-plugin]] page for the full story (or
`packaging/PKGBUILD` for an all-in-one package build).

## Version matching

The plugin resolves Hyprland's own symbols at load time, so it must be
compiled against headers matching the running compositor. Keep the
plugin and the compositor moving together: rebuild both from the same
tree (`make install-compositor` installs the matched compositor as
`hyprland-scheme`). A Hyprland update with the plugin stale either
fails to load (renamed symbols — loud) or misbehaves (changed class
layouts — silent and bad).

## Installing

```sh
make install                # plugin, Scheme sources + boot files: ~/.local/lib/hyprscheme
make install-compositor     # matched compositor: ~/.local/bin/hyprland-scheme
                            # + a session entry in ~/.local/share/wayland-sessions
```

`PREFIX` selects the destination (default `~/.local`).

Then add to your `hyprland.lua` (the Lua config — the old
`plugin = path` hyprlang directive does not exist there):

```lua
hl.plugin.load("/home/YOU/.local/lib/hyprscheme/scheme-plugin.so")
```

and reload. The plugin loads once, during config processing, before
the rest of the config runs.

## Usage

At startup the plugin loads `~/.config/hypr/hyprland.scm` and re-loads
it whenever the file changes. From a terminal:

```sh
hyprctl scheme '(+ 1 2)'
hyprctl scheme '(hl-active-title)'
```

evaluates Scheme in the running compositor. See `examples/` for a
tour of the API, and the wiki for the full reference: binds with
options, timers, window queries and actions, events, stateful custom
layouts, submaps, cross-reload state, trackpad gestures.

## Testing

```sh
tests/run.sh            # the full suite (12 files) against a nested compositor
tests/soak.sh [SECONDS] # sustained-load soak test (not part of the suite)
```

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

## Session integration

`make install-compositor` (or the AUR package) installs the compositor
as `hyprland-scheme` plus a `hyprland-scheme.desktop` session entry
under `share/wayland-sessions/`. How you reach it depends on how you
log in:

- **Display manager (SDDM/LightDM):** pick "Hyprland (Scheme)" from the
  session menu at login.
- **uwsm:** `uwsm start -e hyprland-scheme` from a TTY, or add a uwsm
  desktop entry pointing at the same command.
- **Nested (testing):** run `~/.local/bin/hyprland-scheme` from a
  terminal inside your existing session — it opens as a window like
  any other.
- **Making it your main compositor:** point whatever launches your
  compositor today at `hyprland-scheme` instead of `Hyprland`. For
  display managers the honest caveat is that every setup hides the
  session picker differently — some log in automatically to a fixed
  session. The generic mechanism: display managers list session
  entries from `share/wayland-sessions/*.desktop`, and most can be
  told to auto-select one (for SDDM: a `Session=hyprland-scheme` line
  in `/etc/sddm.conf.d/*.conf` — consult your distribution's
  display-manager configuration for where that lives in your setup).

Because the plugin is compiled against a pinned Hyprland tree, keep
using the compositor installed alongside it (don't mix with system
updates of hyprland) — the plugin and the compositor must move
together.
