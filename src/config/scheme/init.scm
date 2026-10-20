;; init.scm — THE ENTRY POINT. The one file the host loads, every generation.
;;
;; Everything else is reached from here, so each module is loaded exactly ONCE,
;; BY NAME, in dependency order. That is the point of this file: the host used
;; to load all nineteen files itself, by explicit path, through
;; `scm_primitive_load` — a hand-maintained list in C++ that had to be kept in
;; step with the module tree, and, because the primitive skips the Scheme-level
;; loader, no module was ever compiled or cached.
;;
;; Loading by name instead puts every module through Guile's own loader: it uses
;; a compiled `.go` when one is there (shipped beside the `.scm` by `make`), and
;; compiles and caches one when not.
;;
;; THE RELOAD, which is the subtle half. `use-modules` loads a module once per
;; PROCESS, ever — on a second call for a module already loaded it is a no-op,
;; which would leave the previous generation's definitions live and silently
;; break the wipe. So after the first pass this file RELOADS every module
;; explicitly, with `reload-module`, which re-reads the source (or the `.go`)
;; into the existing module. Two properties make that safe, both verified:
;;
;;   - it does NOT cascade: reloading a module leaves its dependents as they
;;     were, so EVERY module is reloaded here, in dependency order;
;;   - the module OBJECT is reused rather than replaced, so the gsubrs the host
;;     registered into the kernel (`registerAllBindings`, before this file is
;;     read) survive. That is what lets the kernel's `#:export`, which names
;;     them, be satisfied on every generation.
;;
;; WHY THIS IS SYNTAX AND NOT A `for-each`: `use-modules` is a SYNTAX form whose
;; arguments must be literal module names at expansion time — a runtime
;; `(use-modules name)`, or an `eval` of one, fails to expand ("source
;; expression failed to match any pattern"). A procedure therefore cannot wrap
;; it, but a MACRO can: `use-or-reload-modules` (below) takes the module name as a
;; pattern variable and uses it BOTH as a quoted datum, for the `in?` and
;; `resolve-module` lookups, and as the literal name `use-modules` needs. That is
;; what turns the load-or-reload decision into one line per module.
;;
;; The ORDER of those lines is the dependency order and is load-bearing.
;;
;; This file defines no module and exports nothing: it is a loader, not a
;; library. `(hyprscheme)` remains the public API list, and the generation a
;; config runs in imports the families and kernel directly (see Host.cpp
;; buildGeneration), not this file.

(use-modules (guile))   ; reload-module

;; `reload-module` re-reads a file into an existing module; `use-modules` loads
;; one that is not there yet. A module is "already in" when `resolve-module`
;; finds it WITHOUT creating — plain `resolve-module` makes an empty module for
;; a name it cannot find (verified), so `#:ensure #f` is the probe. The kernel
;; is always present (the host made it); the rest are present from the second
;; generation on.
(define (in? name)
  (catch #t
    (lambda () (if (resolve-module name #:ensure #f) #t #f))
    (lambda _ #f)))

;; The whole load-or-reload decision, as syntax — variadic, like `use-modules`
;; itself, so several modules can go in one form. `name` is a pattern variable,
;; so the template can use it BOTH as a quoted datum (for the lookups) and as
;; the literal module name `use-modules` needs. A procedure could not do that —
;; `use-modules` is a macro whose arguments must be literal at expansion time,
;; which is the only reason this file is not a `for-each`.
;;
;; The recursion expands to one clause per name, IN ORDER, which is what makes
;; the order below meaningful; the null case ends it.
(define-syntax use-or-reload-modules
  (syntax-rules ()
    ((_) (begin))
    ((_ name rest ...)
     (begin
       (if (in? 'name)
           (reload-module (resolve-module 'name))
           (use-modules name))
       (use-or-reload-modules rest ...)))))

;; ORDER IS LOAD-BEARING: a module is loaded after everything it imports.
;;   kernel  — the host has already created it and filled it with the gsubrs
;;   core    — the shared plumbing the families build on
;;   families— each imports only the kernel and core, never another family
;;   extras  — the one module that composes families
;;   umbrella— the public API list, over all of them
(use-or-reload-modules (hyprscheme kernel)
                      (hyprscheme core))

(use-or-reload-modules (hyprscheme bind)
                      (hyprscheme config)
                      (hyprscheme event)
                      (hyprscheme exec)
                      (hyprscheme gesture)
                      (hyprscheme group)
                      (hyprscheme layer)
                      (hyprscheme monitor)
                      (hyprscheme notification)
                      (hyprscheme query)
                      (hyprscheme rule)
                      (hyprscheme timer)
                      (hyprscheme window)
                      (hyprscheme workspace))

(use-or-reload-modules (hyprscheme extras)
                      (hyprscheme emacs-keymaps)
                      (hyprscheme))
