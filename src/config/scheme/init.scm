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
;; one that is not there yet. So the question is not "is the name findable" —
;; autoloading makes every name on %load-path findable — and not even "is the
;; module object in the tree". It is "does this module have a FILE to re-read",
;; which is exactly what `reload-module` needs and exactly what a module still
;; waiting to be loaded does not have.
;;
;; Three spellings, three answers, and only the third is a probe:
;;   (resolve-module name)                 autoloads — loads the file, THEN finds it
;;   (resolve-module name #:ensure #f)     still autoloads: autoload and #:ensure are
;;                                         separate switches, and #:ensure only says
;;                                         whether to invent an empty module when the
;;                                         search comes up empty
;;   (resolve-module name #f #:ensure #f)  finds only what is ALREADY in the tree,
;;                                         loading nothing
;;
;; The first two answer "yes" for a module that has never been loaded — the first
;; because it went and loaded it. That is not a probe but a load that reports the
;; wrong answer, and the old spelling here did precisely that: on the first
;; generation `in?` loaded each module and the branch below reloaded it straight
;; after, so every module's body ran TWICE. Verified.
;;
;; Turning autoload off is still not enough on its own, because a module can be in
;; the tree with no file behind it — and `reload-module` on one of those fails
;; outright ("unknown file name for module"), there being no file to read. The
;; case that bit us was `(hyprscheme)`, the umbrella: it was the PARENT of every
;; `(hyprscheme X)`, so the tree grew it as a directory module as soon as the
;; first family was defined, while its own file had not been read. Its
;; module-filename stays #f until that file IS read, so that is the test.
;;
;; The umbrella is gone now, but the check stays: the parent of the modules below
;; is `(hyprscheme)` all the same — it is the directory they live in — so the
;; same trap is still one edit away.
;;
;; The kernel is the one module whose file is already behind it — the host read it
;; before this file was — so it takes the reload branch, as it must from the second
;; generation on. On the first generation that reads it a second time, which is the
;; price of not being able to tell "just loaded" from "loaded a generation ago".
;;
;; NOTE: this file is read with the GENERATION module current, not the kernel, so
;; every `use-modules` below imports into the generation — which is where a config
;; looks. Reading it with the kernel current would have imported the families into
;; the KERNEL instead, and the generation would then need the host to import them
;; separately. That is exactly what the host used to do, with a 23-line list in
;; C++ to keep in step with the module tree; the order is load-bearing here for
;; that reason as well.
(define (in? name)
  (catch #t
    (lambda ()
      (let ((module (resolve-module name #f #:ensure #f)))
        (and module (module-filename module) #t)))
    (lambda _ #f)))

;; Load-or-reload, as syntax — variadic, like `use-modules` itself, so several
;; modules can go in one form. `name` is a pattern variable, so the template can
;; use it BOTH as a quoted datum (for the lookups) and as the literal module name
;; `use-modules` needs. A procedure could not do that — `use-modules` is a macro
;; whose arguments must be literal at expansion time, which is the only reason
;; this file is not a `for-each`.
;;
;; TWO steps per module, and the second is not optional:
;;
;;   1. get the module's CURRENT source in. `reload-module` when it is already
;;      loaded (which re-reads the file and is what makes the wipe work — a plain
;;      `use-modules` is a no-op on a module loaded earlier in the process, so the
;;      previous generation's definitions would stay live), otherwise
;;      `use-modules` to load it for the first time.
;;   2. `use-modules` it UNCONDITIONALLY. `reload-module` re-reads a module but
;;      does NOT import it into the current one, so without this the second
;;      generation on would import nothing at all.
;;
;; The unconditional `use-modules` is also what puts the module into THIS
;; module's uses-list, and this file is read with the GENERATION current (see
;; Host.cpp buildGeneration), so that list is the generation's environment — the
;; whole point of the file. It is why the host no longer needs a list of its own.
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
           #t)
       (use-modules name)
       (use-or-reload-modules rest ...)))))

;; The standard library a config has always had (call-with-string-output-port,
;; exists/for-all, filter, keyword arguments …), imported here rather than by the
;; host so that THIS file alone says what a config's environment is.
;;
;; (rnrs enums) is here because the enum machinery (hyprscheme enums) hands back
;; a real rnrs enum, and "you also have the whole (rnrs enums) toolkit" is only
;; true if a config can actually reach it — enum-set-member?, enum-set-union,
;; enum-set-indexer. Without this the enum it returns is inert.
;;
;; These go through plain `use-modules`, never the macro above: they are Guile's
;; own modules, and `reload-module` on one of those is an error, not a refresh —
;; there is nothing of ours to re-read.
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
;;   core    — the shared plumbing the families build on
;;   families— each imports only the kernel, core, enums and values, never
;;             another family
;;   composers (extras, emacs-keymaps) — the only modules that compose families
;;
;; THIS LIST IS THE WHOLE SET of what a config can reach. It is the only place
;; modules are named for LOADING (see the note at the end of this file): the
;; public API is derived by introspecting these modules, not listed.
(use-or-reload-modules (hyprscheme kernel)
                      (hyprscheme enums)
                      (hyprscheme values)
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
                      (hyprscheme emacs-keymaps))

;; THERE IS NO UMBRELLA. (hyprscheme) used to be a module here — a curated
;; #:re-export list over everything, and the "single source of truth for what is
;; public". It is gone: the public set is DERIVED now, by asking Guile what each
;; module publishes (module-public-interface), which cannot disagree with the
;; code the way a hand-kept list can. It had already drifted — it listed
;; hl-bind-thunk and hl-bind-tokens, two internal record accessors that no
;; config should see.
;;
;; So the list of modules above is the whole story: what init.scm names is what
;; a config can reach, and what each of those modules #:exports is the API.
