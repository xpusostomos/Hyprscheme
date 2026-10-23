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
;; LOADED ONCE, EVER — and this file is re-read on EVERY generation.
;; `use-modules` loads a module once per PROCESS; every call after that is a
;; no-op. So the second generation on, each form below does nothing at all and
;; the module OBJECTS stay put. That reuse is deliberate: the gsubrs the host
;; registered into the kernel (`registerAllBindings`, before this file is read)
;; live in those objects and have to survive every generation, and it is what
;; lets the kernel's `#:export`, which names them, be satisfied each time.
;;
;; Its cost is that a module's TOP-LEVEL STATE now outlives the generation that
;; set it, where re-reading every module used to reset it by accident. A
;; config's `set!` on an imported binding writes into the EXPORTING module's
;; cell, so a watchdog budget one config set would still be in force for the
;; next. So a module that keeps state defines an unexported `reset`, and the
;; sweep at the END of this file calls them all — after every module is in, and
;; before the config is read.
;;
;; WHY NOT RELOAD INSTEAD: re-reading each module through `reload-module` was
;; the old way to get a clean slate, and the sweep beats it on every count. It
;; leaks some 4.5 kB per call (twenty modules, every reload); it throws away
;; whatever the host registered into the module; and it re-runs every module's
;; body whether that module holds any state or not.
;;
;; The ORDER of the forms below is the dependency order and is load-bearing.
;;
;; This file defines no module and exports nothing: it is a loader, not a
;; library. `(hyprscheme)` is not a thing — the public API is DERIVED, by asking
;; each module what it publishes (see tools/api-names.scm) — and the generation a
;; config runs in imports the families and the kernel directly (see Host.cpp
;; buildGeneration), not this file.

;; The standard library a config has always had (call-with-string-output-port,
;; exists/for-all, filter, keyword arguments …), imported here rather than by the
;; host so that THIS file alone says what a config's environment is.
;;
;; (rnrs enums) is here because the enum machinery (hyprscheme enums) hands back
;; a real rnrs enum, and "you also have the whole (rnrs enums) toolkit" is only
;; true if a config can actually reach it — enum-set-member?, enum-set-union,
;; enum-set-indexer. Without this the enum it returns is inert.
(use-modules (rnrs io ports))
(use-modules (rnrs lists))
(use-modules (rnrs enums))
(use-modules (srfi srfi-1))
(use-modules (ice-9 optargs))
(use-modules (ice-9 exceptions))

;; ORDER IS LOAD-BEARING: a module is loaded after everything it imports.
;;   kernel  — the host has already created it and filled it with the gsubrs
;;   enums   — the enum MACHINERY; imports the kernel only
;;   values  — the concrete enums two families share; imports kernel + enums
;;   inotify — the inotify(7) binding; imports nothing of ours at all
;;   core    — the shared plumbing the families build on
;;   families— each imports only the kernel, core, enums and values, never
;;             another family
;;   composers (extras, emacs-keymaps, watch) — the only modules that compose
;;             families. watch calls hl-config-reload!, which (hyprscheme
;;             config) owns, so it is a composer for the same reason extras is.
;;
;; THIS LIST IS THE WHOLE SET of what a config can reach. It is the only place
;; modules are named for LOADING: the public API is derived by introspecting
;; these modules, not listed.
(use-modules (hyprscheme kernel)
             (hyprscheme enums)
             (hyprscheme values)
             (hyprscheme inotify)
             (hyprscheme core))

(use-modules (hyprscheme bind)
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

(use-modules (hyprscheme extras)
             (hyprscheme emacs-keymaps)
             (hyprscheme watch))

;; ---- resetting the machinery -------------------------------------------------
;; The generation is new; the machinery is not. Every `use-modules` above is a
;; no-op after the first generation and the module objects stay put, so whatever
;; they keep in a top-level variable — including anything a config pushed in
;; there with `set!` — is still in place, one reload later. This puts it back.
;;
;; A module opts in by defining an UNEXPORTED `reset`. Nothing is registered
;; anywhere, so there is no list here to keep in step and no way to add a module
;; and forget to reset it: the sweep asks the modules themselves.
;;
;; `module-local-variable`, NOT `module-variable`: the latter answers for
;; IMPORTED bindings too, so a module that merely uses something named `reset`
;; would look as though it had one. Only an own binding counts. The variable is
;; checked for being BOUND as well as present — an `#:export` placeholder for a
;; name nothing ever assigned is a variable too.
;;
;; Guarded, and per module: a `reset` that raises is reported and the sweep
;; carries on, because one broken module must not cost the others their reset.
;;
;; IT RUNS ON EVERY GENERATION, including the first, where it is a no-op — the
;; modules were just loaded and are already at their initial values. Making the
;; first generation a special case would mean the host or this file tracking
;; which one it is, for no gain.
(define (hl--reset-modules!)
  (for-each
    (lambda (iface)
      (let ((name (module-name iface)))
        (when (and (pair? name) (eq? (car name) 'hyprscheme))
          ;; RESOLVE THE NAME BACK TO THE MODULE — what `module-uses` holds is
          ;; each module's PUBLIC INTERFACE, not the module. `use-modules` is
          ;; `module-use!` applied to `resolve-interface`, and an interface
          ;; carries only the EXPORTED names, so an unexported `reset` cannot be
          ;; found in one. Verified for (hyprscheme kernel): the use-list entry
          ;; has module-kind `interface`, the resolved name has `directory`, they
          ;; are not eq?, `module-local-variable` answers #f for the first and
          ;; the variable for the second.
          ;;
          ;; Autoload OFF and #:ensure #f: this must FIND, never load. Every name
          ;; here is already loaded — the use-list would not name it otherwise.
          (let ((m (resolve-module name #f #:ensure #f)))
            (when m
              (let ((v (module-local-variable m 'reset)))
                (when (and v (variable-bound? v) (procedure? (variable-ref v)))
                  ;; named, so a failing reset pops a notification rather than
                  ;; only writing to a log nobody reads — and the name says it is
                  ;; the MACHINERY that broke, not the config (a config error is
                  ;; the banner; see hl--report-config in the kernel)
                  (guard (e (#t (hl--report e "module reset")))
                    ((variable-ref v))))))))))
    (module-uses (current-module))))

;; MUST BE LAST, and must stay that way: it has to see every module above, and it
;; has to run before the config is read — which is what the host does next.
(hl--reset-modules!)
