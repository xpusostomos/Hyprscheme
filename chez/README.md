# chez/ — the deprecated Chez backend

**DEPRECATED.** The project has moved to Guile (`SchemeHostGuile.cpp` +
`hyprscheme-compat-guile.scm`); development happens in the tree one
directory up, and from the freeze point on the two will DIVERGE. This
subtree is a frozen snapshot of the last verified Chez state, kept so
the Chez plugin can still be built if ever needed.

What is here:

- `Makefile` — builds `scheme-plugin.so` (Chez) only. The paths are
  relative to `$(CURDIR)`, so it uses the SAME `../ChezScheme` and
  `../Hyprland` trees as the main build; no path edits were needed.
- `src/config/scheme/SchemeHostChez.cpp` — the Chez host (the only file
  that ever included `<scheme.h>`).
- `src/config/scheme/hyprscheme-defun.scm` — the defun machinery
  (Chez-only; describe-function lives here).
- `src/config/scheme/*.scm` copies — prelude + bootstrap, frozen at the
  defun-rollout state (every public function a defun with a docstring).
- `src/...` C++ copies — SchemeManager / SchemeLayout / host interface /
  plugin-main, frozen.
- `BUILDCHEZ.md` — how the PIC Chez tree is built.
- `packaging/PKGBUILD` — ships the Chez plugin as `scheme-plugin.so`
  plus the matched compositor (`hyprland-scheme`).

Build it:

    cd chez && make           # Chez workarea + Hyprland built in place as needed
    cd chez && make install   # .so + .scm machinery + boot files

The install layout is unchanged (`~/.local/lib/hyprscheme/`), so the
Chez plugin installs side by side with the Guile artifact and the
config's `hl.plugin.load` picks which one loads.

Do not evolve this subtree — fixes and features go into the main tree.
If a Chez fix is ever wanted, copy it in and re-verify here; the main
tree no longer carries the Chez host.