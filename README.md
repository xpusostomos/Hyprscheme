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

The Scheme config is `hyprland.scm`, beside the `hyprland.lua` that loads
the plugin. It is found the same way Hyprland finds its own config: the
`$XDG_CONFIG_HOME` base (or `$HOME/.config` when that is unset), then each
`$XDG_CONFIG_DIRS` entry, then `/etc/xdg` — always under `hypr/`.
`HYPRSCHEME_CONFIG` overrides the whole search.

Guile Scheme scripting for [Hyprland](https://hypr.land), as a loadable
plugin. Write your window management logic — keybinds, layouts, timers,
event reactions, queries — in Scheme.

A full API — binds, timers, window queries and actions, events, custom
layouts with state, submaps, trackpad gestures — runs inside the
compositor on the system Guile interpreter. Scripting errors
are contained: a broken config or a failing callback never takes the
compositor down.

## A quick word from our lack of sponsors...

This project has been moderately expensive to implement in its use of
AI, and I'm running out of money to do it. If you want to see it move
forward, money, AI tokens or human assistence would help it move
forward much faster. Having said that, look at whats been achieved by
one guy in a short time.

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

## Installing through hyprpm

There are two ways to get the plugin running. The one above installs a
whole matched compositor, which is the safest. The other is
[hyprpm](https://wiki.hypr.land/Plugins/Using-Plugins/), Hyprland's own
plugin manager — worth using if you already run other plugins through it,
since a hyprpm plugin loads into your existing compositor and coexists with
them:

```sh
hyprpm add file:///path/to/Hyprscheme    # a local checkout is fine
hyprpm enable hyprscheme
hyprpm reload                            # load it now
```

**Do not run hyprpm under `sudo`** — it refuses to run as root, and
escalates on its own (via polkit) for the one thing that needs it.

hyprpm clones Hyprland at your running compositor's commit, builds it, and
compiles the plugin against **those** headers, so the result always matches
the compositor you are running. It installs exactly one file per plugin —
the `.so` — into its own cache, and throws the build tree away afterwards,
so the Scheme machinery is installed separately to
`~/.local/share/hyprscheme/scm/` by the second build step. The plugin finds
it there automatically.

Two caveats. `hyprpm enable` does not survive a restart — nothing loads
hyprpm plugins at compositor startup — so put `hyprpm reload` in your
Hyprland config's startup, or run it after logging in. And a hyprpm build
follows your running Hyprland, which may be older than the commit this
plugin targets; a build against a release the plugin has not caught up with
will fail loudly rather than misbehave.

## Version matching

The plugin resolves Hyprland's own symbols at load time, so it must be
compiled against headers matching the running compositor. Keep the
plugin and the compositor moving together: rebuild both from the same
tree (`make install` installs the matched compositor as `Hyprscheme`
alongside the plugin). A Hyprland update with the plugin stale either
fails to load (renamed symbols — loud) or misbehaves (changed class
layouts — silent and bad).

## Installing

```sh
sudo make install                        # everything, to /usr/local (the default)
make install-local                       # per user: wire up ~/.config/hypr/hyprland.lua
make install PREFIX=$HOME/.local         # files only, per-user, no sudo
```

One target installs all of it: the plugin and the Scheme machinery to
`$(PREFIX)/lib/hyprscheme/`, the matched compositor as
`$(PREFIX)/bin/Hyprscheme`, and a session entry in
`$(PREFIX)/share/wayland-sessions/`. The default `PREFIX` is
`/usr/local` — a system install, hence the `sudo`. Only a system
location works for a display-manager session: SDDM reads its session
entries from `/usr/local/share/wayland-sessions` and
`/usr/share/wayland-sessions` (its `SessionDir`), and never from
anywhere under `$HOME`.

Then load the plugin from your `hyprland.lua` (the Lua config — the
old `plugin = path` hyprlang directive does not exist there). Run

```sh
make install-local
```

which appends this to `~/.config/hypr/hyprland.lua` for you (and does
nothing else — it installs no files; add the block by hand if you
prefer):

```lua
-- Hyprscheme: load the Scheme plugin (added by make install-local)
if (os.getenv("DESKTOP_SESSION") or "") == "Hyprscheme" then
  hl.plugin.load("/usr/local/lib/hyprscheme/scheme-plugin-guile.so")
end
```

It cannot be done for you by `make install`: that runs under `sudo`,
where `$HOME` is root's, so it would edit `/root/.config/...` and
never find the real file. `install-local` runs as you, so `$HOME` is
you.

**The guard is the point.** The plugin resolves the compositor's own
symbols at load time, so it only loads into the compositor installed
alongside it — `Hyprscheme`, not the system `Hyprland`. In any other
build the load fails at `dlopen` with an `undefined symbol: …` error
and a failure notification. Harmless, but it would repeat on every
config pass of that session. `DESKTOP_SESSION` is the one variable
that survives to the end of the config: it is set by the session
launcher and nothing in the config rewrites it, whereas
`XDG_CURRENT_DESKTOP` and `XDG_SESSION_DESKTOP` are both set back to
`Hyprland` by Omarchy's `envs.lua` on every parse. In a session
started without a launcher (nested from a terminal, say) the variable
is empty, the guard is false, and the plugin does not load — start
such a session with `DESKTOP_SESSION=Hyprscheme` if you want it.

**Do not add a second `hl.plugin.load` for the same plugin.** Hyprland
compares plugin paths as literal strings, so `/home/you/.local/lib/...`
and `/usr/local/lib/...` are two different plugins and *both* load.
Each registers its own `scheme` IPC command, the duplicate registration
collides, and `hyprctl scheme` stops working — and no config reload
evicts the extra copy, because the reload only unloads plugins whose
path has disappeared from the config. Restart the compositor.
`install-local` refuses to append when it finds any existing
`hl.plugin.load`, for this reason.

The Chez backend is deprecated and frozen in `chez/` (see
`chez/README.md`); `scheme-plugin-guile.so` is the artifact this tree
builds.

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

`sudo make install` (or the AUR package) installs the compositor as
`Hyprscheme` plus a `Hyprscheme.desktop` session entry under
`$(PREFIX)/share/wayland-sessions/`. The entry is

```
Exec=uwsm start -g -1 -e -D Hyprscheme -- start-hyprscheme --path /usr/local/bin/Hyprscheme
```

Three things in that line are deliberate:

- **`start-hyprscheme`, not the compositor directly.** Hyprland is meant
  to be launched through its `start-hyprland` wrapper, which forks a
  watchdog and execs the compositor with `--watchdog-fd`; started
  without it, Hyprland logs a warning and loses hang recovery. The
  wrapper is built from the same tree as the compositor and installed
  under our own name so the package neither needs nor clashes with a
  system `start-hyprland`; `--path` is how it is told which binary to
  run.
- **`--` before it**, because everything after the compositor ID is
  passed through: without it uwsm reads `--path` as its own argument
  and refuses to start.
- **`DesktopNames=Hyprscheme`** rather than `Hyprland`, so
  desktop-aware tooling can tell the two sessions apart.

The `uwsm` wrapper makes the session's systemd services bind to the
compositor properly. How you reach it depends on how you log in:

- **Display manager (SDDM/LightDM):** pick "Hyprscheme" from the
  session menu at login.
- **uwsm:** from a TTY,
  `uwsm start -e -D Hyprscheme -- start-hyprscheme --path /usr/local/bin/Hyprscheme`
  (the `--` matters — see above), or add a uwsm desktop entry pointing
  at the same command.
- **Nested (testing):** run `Hyprscheme` from a terminal inside your
  existing session — it opens as a window like any other. Nested runs
  skip the wrapper, so the watchdog warning appears; harmless here.
- **Making it your main compositor:** point whatever launches your
  compositor today at `Hyprscheme` instead of `Hyprland`. For
  display managers the honest caveat is that every setup hides the
  session picker differently — some log in automatically to a fixed
  session. The generic mechanism: display managers list session
  entries from `share/wayland-sessions/*.desktop`, and most can be
  told to auto-select one (for SDDM: a `Session=Hyprscheme` line
  in `/etc/sddm.conf.d/*.conf` — consult your distribution's
  display-manager configuration for where that lives in your setup).

Because the plugin is compiled against a pinned Hyprland tree, keep
using the compositor installed alongside it (don't mix with system
updates of hyprland) — the plugin and the compositor must move
together.
