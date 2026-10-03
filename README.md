# Project Hyprscheme

#### Hyprland with Scheme scripting

### Project State

It's feature complete, and it works!

No Hyprland code was touched, it's a legit Hyprland plugin using the
official plugin API. It supports the entire API that lua supports,
so you should be able to code exclusively in scheme and not miss anything.

But it also still supports Lua. Nothing was removed. You can have
part of your config in lua and part in Scheme, it's fine.

The semantics of the scheme configuration has the same semantics as the
Lua in that when hyprland reloads its config, everything starts from a clean
slate.

It looks for an init file in:
* $XDG_CONFIG_HOME/hyprscheme/init.scm
* $HOME/.config/hyprscheme/init.scm
* $HOME/.hyprscheme

Guile Scheme scripting for [Hyprland](https://hypr.land), as a loadable
plugin. Write your window management logic — keybinds, layouts, timers,
event reactions, queries — in Scheme.

A full API — binds, timers, window queries and actions, events, custom
layouts with state, submaps, trackpad gestures — runs inside the
compositor on the system Guile interpreter. Scripting errors
are contained: a broken config or a failing callback never takes the
compositor down.

## A quick word from our lack of sponsors...

This project has been moderately expensive to implement in its use of AI, and I'm 
running out of money to do it. If you want to see it move forward, money, AI tokens
or human assistence would help it move forward much faster. Having said that, look
at whats been achieved by one guy in short time.

## Developer Discussion

Github forum is turned on above, you should feel free to discuss the project there.

## Installing

It's probably not a good idea to install separately Hyprland and
Hyprscheme, because any slight difference in the C++ headers, could
lead to it crashing. It is recommended that any pre-built packages
build them together and install them together to make sure
you have a Hyprscheme that was built to work with your Hyprland.

## Building

All you basically need is a recent version of guile and Hyprland.
Unfortunately, because Hyprland from time to time changes its C++
objects. The best way to do it is download Hyprland. Build
it. Download Hyprscheme at the same directory level as Hyprland. Then
make install it. Now you have to make sure that you run the Hyprland
that you just built and not one you might have installed from your
package manager.

## An example

Full documentation is in the wiki, but to give you a taste 
of what you can do..

```scheme
;; ~/.config/hypr/hyprland.scm
(hl-bind-add! (hl-key "SUPER+U") (lambda () (hl-exec! "foot")))

;; a master layout: the first window takes 60% of the width, the rest stack
;; down the right. It is handed the work AREA and the PLACEMENTS — one
;; (window . box) pair per window, each box being where that window is now —
;; and returns the same shape: only the windows to move, and where.
(hl-layout-add! "master"
  'recalculate
  (lambda (area placements)
    (let ((x (hl-box-x area)) (y (hl-box-y area))
          (w (hl-box-w area)) (h (hl-box-h area)))
      (cond
        ((null? placements) '())
        ((null? (cdr placements)) (list (cons (car (car placements)) area)))
        (else
          (let* ((mw     (inexact->exact (floor (* w 0.6))))
                 (slaves (- (length placements) 1)))
            (cons (cons (car (car placements)) (hl-box x y mw h))
                  (let loop ((i 0) (ry 0) (rs (cdr placements)) (out '()))
                    (if (null? rs)
                        (reverse out)
                        (let ((rh (quotient (- h ry) (- slaves i))))
                          (loop (+ i 1) (+ ry rh) (cdr rs)
                                (cons (cons (car (car rs))
                                            (hl-box (+ x mw) (+ y ry) (- w mw) rh))
                                      out))))))))))))
```

A full API — binds, timers, window queries and actions, events, custom
layouts with state, submaps, trackpad gestures — runs inside the
compositor on the system Guile interpreter. Scripting errors
are contained: a broken config or a failing callback never takes the
compositor down.

## Requirements

- **Hyprland from source, built in place** — the plugin compiles
  against its headers and runs inside the binary built from the same
  tree (`../Hyprland` by default; `make` builds it if missing)
- **Guile 3.0 development headers** (`guile-3.0` on Arch) — the plugin
  links libguile and the machinery runs on the system interpreter; no
  bundled interpreter and no boot files

## Building

```sh
make    # builds Hyprland as needed, then the plugin
```

Everything builds **in place** — no copies, no staging:

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
make install                # plugin + Scheme machinery: ~/.local/lib/hyprscheme
make install-compositor     # matched compositor: ~/.local/bin/hyprland-scheme
                            # + a session entry in ~/.local/share/wayland-sessions
```

`PREFIX` selects the destination (default `~/.local`).

Then add to your `hyprland.lua` (the Lua config — the old
`plugin = path` hyprlang directive does not exist there):

```lua
hl.plugin.load("/home/YOU/.local/lib/hyprscheme/scheme-plugin-guile.so")
```

and reload. The plugin loads once, during config processing, before
the rest of the config runs. The Chez backend is deprecated and frozen
in `chez/` (see `chez/README.md`); `scheme-plugin-guile.so` is the
artifact this tree builds.

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

The plugin links libguile and runs its machinery (three `.scm` files,
installed next to the `.so`) on the system interpreter, registering
Scheme callbacks as first-class citizens of the compositor: keybinds
fire Scheme closures, custom layouts are Scheme functions returning
geometry, events deliver window handles as Scheme records. Crash
reporting survives the interpreter's own signal handling via a
re-installed handler that calls Hyprland's exported crash reporter.

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
