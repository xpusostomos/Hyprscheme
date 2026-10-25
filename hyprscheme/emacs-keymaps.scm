;; hyprscheme/emacs-keymaps.scm — emacs key notation, and the machinery built on it
;;
;; This is SUGAR over the core API, not core API itself, which is why it is its
;; own module rather than part of (hyprscheme core): nothing here is needed to
;; drive the compositor, it is a way of SPELLING keys that configs may prefer.
;;
;; It is a composer, like (hyprscheme extras) — the second module allowed to
;; reach across families — because key sequences have to register binds
;; (hl-bind-add!), scope them to a submap (#:submap) and switch between them
;; (hl-submap-activate!). A family may import only the kernel and core.
;;
;; Everything emacs-shaped lives here: the notation (hl-kbd/hl-key and the
;; hl--emacs-* tables), the canonicalisation (hl-key-regularise), the
;; logical/physical conversions, key-description, and the keymap layer built on
;; them (define-key!, global-set-key!, global-map, hl-keymap-attach-to-tag!).

(define-module (hyprscheme emacs-keymaps)
  #:export (hl-key-regularise hl-key-logical-to-physical
            hl-key-physical-to-logical hl-key-description
            hl-kbd hl-key define-key! global-set-key! global-map
            define-keymap
            hl-keymap-attach-to-tag! hl-keymap-attach-to-window-condition!
            hl-keymaps-recheck!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))
(use-modules (hyprscheme bind))
;; the third family this composer reaches: hl-keymap-attach-to-tag! reads the
;; focused window's tags, and both of those are (hyprscheme window)'s.
(use-modules (hyprscheme window))

;; the same standard library the parser had in (hyprscheme core): string-index
;; comes from (guile), remove/every from srfi-1, string-join from srfi-13
(use-modules (srfi srfi-1)
             (srfi srfi-13))

(define (hl-key-regularise tokens) "Return the CANONICAL form of one chord:
TOKENS is a token list of the shape hl-bind-add! takes, and the result is
the same shape — every modifier first, in a fixed order, then the keys in
the order they were given — with every spelling of the same chord
collapsed to one list. \"MOD1+x\" and \"M-x\" both give '(\"ALT\" \"x\");
\"CONTROL+x\" and \"x CTRL\" both give '(\"CTRL\" \"x\"). Keys are lower
case — Hyprland resolves names case-insensitively, so \"Return\", \"Tab\"
and \"F1\" come back as \"return\", \"tab\" and \"f1\".

The aliasing is Hyprland's (CONTROL/CTRL, MOD1/ALT, WIN/LOGO/MOD4/SUPER/
META all resolve to one modifier) and the key's case and canonical
spelling are xkb's, so this cannot be reproduced by sorting the tokens.
A token that is not a keysym — \"code:28\", \"mouse:272\", \"switch:lid\",
\"catchall\" — is passed through unchanged.

Raises if TOKENS is not a list of strings, or holds only modifiers and no
key."
  (let ((r (hl--c-key-regularise tokens)))
    (if r
        r
        (error "hl-key-regularise:" (hl--c-config-last-error)))))

(define (hl-key-logical-to-physical tokens) "Convert a LOGICAL chord to the
PHYSICAL keys that produce it. A logical key is the symbol you mean — \"%\",
\"A\", \"Return\"; a physical one is what has to be pressed — \"SHIFT\" \"5\".
TOKENS is a token list (modifiers first, key last), as hl-bind-add! takes.

Hyprland keys binds on the keysym of a PHYSICAL key at level 0, so a bind
on \"percent\" parses and then matches nothing; the \"%\" character is
really SHIFT plus the key that carries it. This asks the active keymap
which key and level produce the symbol, so it is layout-aware: \"%\" is
SHIFT+5 on us and a different key on fr, and \"#\" is AltGr rather than
SHIFT on fr. Named keys (\"Return\", \"F1\") and event tokens (\"code:28\",
\"mouse:272\") pass through unchanged.

The reverse is hl-key-physical-to-logical; hl-kbd calls this for you.

When no keymap is available — a harness loading the machinery with no
compositor — the tokens are returned UNCHANGED rather than raising, since
there is nothing to convert against. Inside a compositor there is always a
keymap, so a failure there is real and raises."
  (let ((r (hl--c-key-logical-to-physical tokens)))
    (cond
      ((pair? r) r)
      ;; #f with no keymap = nothing to convert against; pass through
      ((and (not (hl--c-config-last-error))) tokens)
      (else (error "hl-key-logical-to-physical:" (hl--c-config-last-error))))))

(define (hl-key-physical-to-logical tokens) "Convert a PHYSICAL chord to the
LOGICAL key it produces: (\"SHIFT\" \"5\") → (\"%\"), (\"SHIFT\" \"a\") →
(\"A\"). Named keys and event tokens pass through unchanged. This is the
inverse of hl-key-logical-to-physical and is layout-aware in the same way."
  (let ((r (hl--c-key-physical-to-logical tokens)))
    (if r
        r
        (error "hl-key-physical-to-logical:" (hl--c-config-last-error)))))

;; ---- emacs key-description ---------------------------------------------------
;; The token -> emacs letter tables, the reverse of the ones hl-kbd uses.

(define hl--emacs-mod-letter
  '(("CTRL" . "C") ("ALT" . "M") ("SHIFT" . "S") ("SUPER" . "s")
    ("MOD3" . "H") ("MOD2" . "2") ("MOD5" . "5") ("CAPS" . "caps")))

;; xkb names whose emacs spelling differs. hl--emacs-keys in core.scm holds the
;; forward direction; this is its inverse, plus the bracketed forms.
(define hl--emacs-key-name
  '(("Return" . "RET") ("space" . "SPC") ("Escape" . "ESC")
    ("Tab" . "TAB") ("Delete" . "DEL")
    ("Left" . "<left>") ("Right" . "<right>")
    ("Up" . "<up>") ("Down" . "<down>")
    ("Home" . "<home>") ("End" . "<end>")
    ("BackSpace" . "<backspace>") ("Prior" . "<prior>") ("Next" . "<next>")))

(define (hl--emacs-render-key key) "Render ONE logical key token the way
emacs' key-description would: a character as itself (upper case when it
carries SHIFT), a named key through the emacs name table, and anything
else bracketed."
  (cond
    ((assoc key hl--emacs-key-name) => cdr)
    ((= 1 (string-length key)) key)
    ;; an xkb name that is a single character after folding, or a plain
    ;; multi-character name -> <lowercased>
    (else (string-append "<" (string-downcase key) ">"))))

;; The order emacs' key-description prints modifiers in. Measured, not guessed:
;; (key-description (kbd "S-s-H-M-C-x")) => "C-H-M-S-s-x", and with A present it
;; leads ("A-C-M-x"). ALT has no slot of its own here — emacs distinguishes A
;; (alt) from M (meta) and we have one modifier for both — so it takes M's,
;; which is the letter hl--emacs-mods already gives it.
(define hl--emacs-mod-order
  '("CTRL" "MOD3" "ALT" "SHIFT" "SUPER" "MOD2" "MOD5" "CAPS"))

(define (hl--emacs-mod-rank m)
  (or (list-index (lambda (x) (string=? x m)) hl--emacs-mod-order) 999))

(define (hl--emacs-render-chord tokens) "Render one chord: modifier letters
in emacs' order, then the key. The order is canonical, so \"M-C-s\" renders
as \"C-M-s\" — the one case where the round trip through hl-kbd does not
come back byte-identical. SHIFT plus a single letter is written as the
upper-case letter (\"SHIFT\" \"a\" → \"A\"), which is how emacs writes it."
  (let* ((mods (filter (lambda (t) (assoc t hl--emacs-mod-letter)) tokens))
         (keys (filter (lambda (t) (not (assoc t hl--emacs-mod-letter))) tokens))
         (shift? (member "SHIFT" mods))
         ;; SHIFT sorts WITHIN the order, not after it: emacs prints C-S-s,
         ;; not C-s-S, so it must not be appended as a suffix
         (ordered (sort mods (lambda (a b) (< (hl--emacs-mod-rank a) (hl--emacs-mod-rank b)))))
         (letters (map (lambda (m) (cdr (assoc m hl--emacs-mod-letter))) ordered))
         (rendered (map (lambda (k)
                          (if (and shift? (= 1 (string-length k)) (char-alphabetic? (string-ref k 0)))
                              (string-upcase k)
                              (hl--emacs-render-key k)))
                        keys)))
    (string-append (if (null? letters) "" (string-append (string-join letters "-") "-"))
                   (string-join rendered " "))))

(define (hl-key-description tokens) "Render TOKENS the way emacs'
key-description would, in the LOGICAL form a user reads and writes:
(hl-key-description (hl-kbd \"C-x C-f\")) → \"C-x C-f\".

The tokens may be physical (\"SHIFT\" \"5\") or logical (\"%\"); a physical
form is logicalised first, so the output is always the character rather
than the key that carries it. A sequence — a list of chords, as hl-kbd
returns for a multi-key spec — renders space-separated."
  (define (chord toks)
    (hl--emacs-render-chord
      (let ((logical (hl-key-physical-to-logical toks)))
        (if logical logical toks))))
  (if (and (pair? tokens) (pair? (car tokens)) (list? (car tokens)))
      (string-join (map chord tokens) " ")
      (chord tokens)))

;; ---- moved from (hyprscheme core): the emacs notation and its parser ----
;; Sugar over the core API, not core API itself, so it lives here.

(define hl--emacs-mods
  '(("C" . "CTRL") ("M" . "ALT") ("S" . "SHIFT")
    ("s" . "SUPER") ("A" . "ALT") ("H" . "MOD3")))

;; the modifier token names live in (hyprscheme core) — the chord split there
;; needs the same list, and a family cannot import this module. See
;; hl--modifier-tokens / hl--modifier-token?.

(define hl--emacs-keys
  '(("RET" . "Return") ("SPC" . "space") ("ESC" . "Escape")
    ("TAB" . "Tab") ("DEL" . "Delete") ("LFD" . "Return")))

;; emacs mouse events → hyprland mouse:CODE
;; emacs mouse-1=left, mouse-2=middle, mouse-3=right
;; hyprland: 272=left, 273=right, 274=middle

(define hl--emacs-mouse
  '(("<down-mouse-1>" . "mouse:272") ("<mouse-1>" . "mouse:272")
    ("<down-mouse-2>" . "mouse:274") ("<mouse-2>" . "mouse:274")
    ("<down-mouse-3>" . "mouse:273") ("<mouse-3>" . "mouse:273")
    ("<mouse-4>" . "mouse:275") ("<mouse-5>" . "mouse:276")))

(define (hl--emacs-key name)
  ;; translate an emacs key name to the xkb/hyprland name
  (let ((mapped (or (assoc name hl--emacs-keys) (assoc name hl--emacs-mouse))))
    (if mapped
        (cdr mapped)
        ;; bracketed keyboard keysym: <f1> → F1, <left> → Left, <kp-1> → KP_1
        (if (and (> (string-length name) 2)
                 (char=? (string-ref name 0) #\<)
                 (char=? (string-ref name (- (string-length name) 1)) #\>))
            (let* ((inner (substring name 1 (- (string-length name) 1)))
                   (n (string-length inner)))
              (let build ((i 0) (acc '()))
                (if (= i n)
                    (list->string (reverse acc))
                    (let ((c (string-ref inner i)))
                      (build (+ i 1)
                             (cons (if (char=? c #\-) #\_ c) acc))))))
            name))))

(define (hl-kbd spec) "Parse an emacs-style key specification string into a
LIST of key tokens ready for hl-bind-add!: \"C-M-a\" → (\"CTRL\" \"ALT\"
\"a\"), \"<f1>\" → (\"F1\"). The LAST token is the key, even when it spells
like a modifier letter (\"s-M\" → (\"SUPER\" \"M\") — Super+M the key, not
Super+Alt). A TRAILING DASH makes it a pure modifier list: \"C-M-\" →
(\"CTRL\" \"ALT\").

The result is the PHYSICAL form, because that is what Hyprland binds: a
shifted character is expanded into SHIFT plus the key that carries it, so
\"C-%\" → (\"CTRL\" \"SHIFT\" \"5\") on a us layout. The conversion is
layout-aware (hl-key-logical-to-physical); the logical form you would read
or write is (hl-key-description (hl-kbd \"C-%\")) → \"C-%\"."
  ;; ONE chord: the dash walk, returning raw (unconverted) tokens
  (define (one-chord str)
    (let loop ((s str) (acc '()))
      (let ((dash (string-index s #\-)))
        (if (and dash (> dash 0))
            (let ((prefix (substring s 0 dash)))
              (if (assoc prefix hl--emacs-mods)
                  ;; a spec ending in the dash ("C-M-") is a pure modifier list
                  (let ((rest (substring s (+ dash 1) (string-length s))))
                    (if (= 0 (string-length rest))
                        (reverse (cons (cdr (assoc prefix hl--emacs-mods)) acc))
                        (loop rest (cons (cdr (assoc prefix hl--emacs-mods)) acc))))
                  (reverse (cons (hl--emacs-key s) acc))))
            (reverse (cons (hl--emacs-key s) acc))))))
  ;; logical -> physical, except a pure modifier list, which has no key to
  ;; convert and would be rejected
  (define (physical toks)
    (if (every (lambda (t) (member t hl--modifier-tokens)) toks)
        toks
        (hl-key-logical-to-physical toks)))
  ;; SPACES SEPARATE KEYS. "C-x C-f" is two chords, not one spec with a space
  ;; in it — that is the whole notation for a sequence. One chord returns the
  ;; flat token list; several return a LIST OF CHORDS, which is the shape
  ;; hl-key-description and hl-bind-keys! take.
  (let ((parts (filter (lambda (s) (> (string-length s) 0)) (string-split spec #\space))))
    (cond ((null? parts) '())
          ((null? (cdr parts)) (physical (one-chord (car parts))))
          (else (map (lambda (p) (physical (one-chord p))) parts)))))

(define (hl-key spec) "Parse a hyprland-style key specification string
into a LIST of key tokens: \"SUPER+SHIFT+Q\" →
(\"SUPER\" \"SHIFT\" \"Q\"). This is the PHYSICAL form, as Hyprland spells
it — unlike hl-kbd, a shifted character is not expanded."
  (map string-trim-both (string-split spec #\+)))

;; ---- define-key! / global-set-key! -------------------------------------------
;;
;; Hyprland has ONE current submap, and a bind is eligible only when its submap
;; tag equals it (or it is SUBMAP_UNIVERSAL) — there is no stack and no separate
;; "global keymap", the default map is just the binds tagged "". So a submap is
;; a MODE: while one is active every bind outside it is dead.
;;
;; THE STATE BELONGS TO THE KEYMAP, not to this module, and that is the whole
;; point of the keymap object. Three module-level hashtables used to live here —
;; which prefix submaps had been built, which had been given our s-G, which had
;; been given a catchall. A keymap holds that itself now: `children` is the
;; sub-map it built for a prefix, found again BY NAME — the assq that makes a
;; second binding on the same prefix reach the same map — and `marks` records
;; the "already given an exit / a catchall" facts. Those two are MARKS rather
;; than something read off the binds, and the difference is real: a config may
;; bind s-G itself, so "is there a bind on s-G here" would answer yes for a map
;; we never gave an exit to.
;;
;; global-map is the one thing here that genuinely IS module state, and `reset`
;; rebuilds it — see below.
;;
;; IT IS ALSO BUILT AT LOAD TIME, not only by `reset`, and that is not
;; belt-and-braces: `reset` runs from init.scm's sweep, which a tool that loads
;; the modules directly (tools/doc-exercise.scm, tools/load-check.scm) never
;; runs. A `#f` here would make every global-set-key! in the wiki fail under
;; those while working perfectly in a config. The call is to hl-make-keymap,
;; which is plain Scheme — make-struct and nothing else — so it is safe at a
;; module's top level (the rule against top-level calls is about GSUBRS, which
;; are unbound until the host has registered them).
(define global-map (hl-make-keymap #:submap ""))

;; The attachment registry and its listener handle — see
;; hl-keymap-attach-to-window-condition! at the end of this file. Declared HERE,
;; above `reset`, so that the `set!`s below are not forward references; `reset`
;; is the only other writer.
(define hl--window-conditions '())   ; ((keymap . condition) ...)
(define hl--condition-listener #f)   ; the window.active subscription, or #f

;; Called ONCE PER GENERATION by init.scm (see the note there). This file is
;; loaded ONCE per process — use-modules loads a module one time — so the
;; global-map built for the first config would still be here, still holding its
;; binds, when the second config is read. Without this, every global-set-key! in
;; a reloaded config would add a fresh bind on top of a stale one whose thunk
;; belongs to a generation that has been thrown away.
;;
;; UNEXPORTED: not API, and init.scm finds it with `module-local-variable`,
;; which sees own bindings whether or not they are exported.
(define (reset)
  (set! global-map (hl-make-keymap #:submap ""))
  ;; The tag registry is per-generation for the same reason global-map is: the
  ;; keymaps in it belong to the config that built them and their binds are gone
  ;; once the host drops the generation's handlers. Keeping it would mean
  ;; arming keymaps from a config that no longer exists.
  (set! hl--window-conditions '())
  ;; And the LISTENER handle is deliberately forgotten rather than kept. On
  ;; reload the host tears every event handler down and the new generation
  ;; re-registers; a guard that still held a handle would believe the listener
  ;; was installed, never install another one, and leave the registry frozen at
  ;; whatever the first focus change made it. The opposite answer to the GC
  ;; hook, which is installed once for the process and must NOT be reinstalled.
  (set! hl--condition-listener #f))

(define (hl--keymap! who km)
  "Refuse anything that is not a keymap, naming WHO and the value it was handed.
This argument used to be a submap NAME — a bare string — so a stale config gets
a sentence here rather than a confusing failure several calls later."
  (unless (hl-keymap? km)
    (error (string-append who ": expected a keymap, as hl-make-keymap or define-keymap builds one, but got:")
           km))
  km)

(define (hl--seq-chords sequence) "Normalise SEQUENCE to a list of chords:
hl-kbd returns a flat token list for a one-key spec and a list of chords for
a multi-key one, and both are accepted here."
  (if (and (pair? sequence) (pair? (car sequence)))
      sequence
      (list sequence)))

;; There is deliberately NO `hl-submap!` here any more — the function that
;; returned a thunk switching to a named submap. It was one line,
;; (lambda () (hl-submap-activate! name)), and it prevented nothing: any
;; config can write that lambda, and it is trivially correct, so the name
;; bought 20 characters and cost a public name whose `!` claimed it ACTED when
;; it returned a function. Entering a mode is written out now:
;;
;;   (global-set-key! (hl-kbd "s-r") (lambda () (hl-submap-activate! "resize")))
;;
;; Note the asymmetry that made the name read plausibly and mislead anyway:
;; hl-submap-exit! takes NO argument, so it can be handed to a bind directly and
;; really is the action — while ENTERING needs an argument, and so needed
;; currying. One of the pair acted and the other returned, under the same `!`.

(define (hl--expand submap chord) "The name of the submap one key DEEPER: the
current submap's name extended by this key's canonical logical description, so
each level is an expansion of the one before. \"\" + C-a -> \"C-a\";
\"C-a\" + C-b -> \"C-a C-b\"; \"resize\" + C-a -> \"resize C-a\".

The base contributes nothing when it is the default map, so a top-level
sequence is named \"C-a\" rather than \" C-a\"."
  (let ((desc (hl-key-description chord)))
    (if (string=? submap "") desc (string-append submap " " desc))))

(define (hl--ensure-exit! km target catchall?) "Give keymap KM its ways out,
each once: an s-G that switches to TARGET, and — when CATCHALL? — a catchall
that switches there too, on any key KM does not bind.

TARGET is a submap NAME, not a keymap: the compositor switches submaps by name
(hl-submap-activate!), and the names are what the chain is built from anyway.

The catchall is what makes a prefix behave like a prefix. A submap is a MODE:
Hyprland keeps one current submap and every bind outside it is dead, so a key
the map does not bind would otherwise do nothing AND leave you sitting in the
map, with nothing on screen to say so. A catchall fires only when no other bind
matched, and it consumes the key, so a bound key is unaffected.

The DEFAULT map is skipped: it is top level, so there is nothing to abandon to,
and binding s-G there would hijack the key globally rather than cancel out of a
mode."
  (unless (string=? (hl-keymap-submap km) "")
    ;; the two marks are tracked SEPARATELY, and on the KEYMAP rather than in a
    ;; module table: a TOP-LEVEL map still wants its s-G (to leave the mode for
    ;; good) but must not get a catchall (a key it does not bind must not eject
    ;; you from a mode you entered deliberately).
    ;;
    ;; A MARK, not a look for an s-G bind: a config may bind s-G itself, and
    ;; then we would think the exit had been given and leave the map without one.
    (let ((want-g (not (hl-keymap-marked? km 'exit)))
          (want-c (and catchall? (not (hl-keymap-marked? km 'catchall)))))
      (when want-g
        (hl-keymap-mark! km 'exit)
        (hl-keymap-add! km (hl-make-bind (hl-kbd "s-G")
                                         (lambda () (hl-submap-activate! target)))))
      (when want-c
        (hl-keymap-mark! km 'catchall)
        (hl-keymap-add! km (hl-make-bind (hl-kbd "catchall")
                                         (lambda () (hl-submap-activate! target))
                                         #:description "unmatched key: abandon the sequence"))))))

(define* (define-key! keymap sequence thunk #:key (top-level #t) #:allow-other-keys #:rest bind-options) "Bind a key
SEQUENCE to THUNK inside KEYMAP — emacs' define-key, scoped to a map.

  (define-key! resize-map (hl-kbd \"h\") (lambda () (hl-window-size-set! ...)))

KEYMAP is a keymap OBJECT — (hl-make-keymap ...), define-keymap, or global-map —
NOT a submap name. A submap is a name stamped on the binds that carry it; the
keymap is what holds them, so it is what you pass. The name it places its binds
in comes from hl-keymap-submap.

The sequence is walked from KEYMAP, one submap per prefix, each named by
EXPANDING the previous name with the key that reaches it (hl--expand), so
\"C-a C-b\" inside a map on \"resize\" builds \"resize C-a\" and hangs C-b off it.
The prefix key is bound in the enclosing map to enter the next; the final key is
bound in the last prefix's map to THUNK. Two sequences sharing a prefix share
the submap AND the keymap, because the child is looked up by name in the map
that built it rather than named again from scratch.

#:top-level says where the chain LEAVES to — both when s-G cancels it and when
THUNK completes:

  #t (default)  to the default map, \"\". You are ejected when it ends.
  #f            back to the keymap's submap. You stay in the mode.

So (define-key! global-map seq thunk) is exactly (global-set-key! seq thunk):
global-map is the default map, so both targets are \"\". A modal submap is one
whose sequences are defined #:top-level #f; it also gets its own s-G, to leave
it for good.

Every map the chain builds is a KEY-SEQUENCE map, and abandons to EXIT on a key
it does not bind — as if you had pressed s-G — so a mistyped prefix does not
strand you in it. The one exception is a TOP-LEVEL map, which is KEYMAP itself
when #:top-level is #f: you entered that one deliberately, as a mode, so an
unrecognised key must not eject you from it. Its own s-G still leaves.

Returns the final bind's handle, the one carrying THUNK, so a binding can be
removed again with hl-unbind!."
  (hl--keymap! "define-key!" keymap)
  ;; #:allow-other-keys MAKES #:rest CATCH #:top-level TOO, which is why it is
  ;; filtered out here: define-key!'s own option is consumed above, and hl-make-bind
  ;; would otherwise be handed a keyword it does not take. The two cannot both be
  ;; declared without this: Guile's optargs needs `#:key K #:allow-other-keys
  ;; #:rest R` for R to receive the unrecognised pairs at all, and once it does,
  ;; the recognised ones come through as well.
  (let ((bind-options
         (let loop ((l bind-options) (acc '()))
           (cond ((null? l) (reverse acc))
                 ((null? (cdr l)) (reverse (cons (car l) acc)))
                 ((eq? (car l) #:top-level) (loop (cddr l) acc))
                 (else (loop (cddr l) (cons (cadr l) (cons (car l) acc))))))))
  (let* ((base (hl-keymap-submap keymap))
         (exit (if top-level "" base)))
    ;; KEYMAP always gets its s-G, so a mode can be left for good. It gets the
    ;; catchall only when it is a key-SEQUENCE map: KEYMAP is a top-level map
    ;; exactly when it is its own exit, and you entered that one deliberately,
    ;; so a key it does not bind must not eject you from it.
    (hl--ensure-exit! keymap "" (not (string=? base exit)))
    (let loop ((remaining (hl--seq-chords sequence)) (current keymap))
      (if (null? remaining)
          #f
        (let ((chord (car remaining)))
          (if (null? (cdr remaining))
              ;; the action: run it, then leave the chain. The bind's placement
              ;; comes from the keymap it is added to; the EXIT is done here in
              ;; Scheme rather than declared as #:submap-reset, and that is a
              ;; deliberate choice:
              ;;
              ;;   - the compositor applies #:submap-reset inside invokeBind,
              ;;     AFTER the thunk — correct in a live session, but invisible
              ;;     to this repo's harness, which fires a bind by calling its
              ;;     thunk (hl--bind-fire-rec). Two real tests in t-keydesc.sh
              ;;     ("#:top-level #f returns to the base submap" and "#:top-level
              ;;     #t ejects to the default map") assert the post-action submap
              ;;     by firing the action, so moving the exit to C++ would have
              ;;     quietly turned them into tests that cannot fail — which is
              ;;     worse than no test, by this tree's own rule.
              ;;   - it also keeps the two paths in agreement: dynamic-wind
              ;;     restores on RAISE as well as on return, which is what stops
              ;;     a throwing action stranding you inside the chord.
              ;;
              ;; #:submap-reset stays a public option for configs that want the
              ;; declarative form (see hl-bind-add!, and submaps.md).
              (hl-keymap-add! current
                              (apply hl-make-bind chord
                                     (lambda ()
                                       (dynamic-wind (lambda () #t)
                                                     thunk
                                                     (lambda () (hl-submap-activate! exit))))
                                     bind-options))
              (let* ((next-name (hl--expand (hl-keymap-submap current) chord))
                     (next (hl-keymap-child current next-name)))
                (unless next
                  ;; FIRST TIME this prefix is reached, so build its map, bind
                  ;; the key that enters it, and give it its own ways out. A
                  ;; second sequence through the same prefix finds `next` above
                  ;; and only hangs its own key off it.
                  (set! next (hl-make-keymap #:submap next-name))
                  (hl--keymap-child-set! current next-name next)
                  (hl-keymap-add! current
                                  (hl-make-bind chord (lambda () (hl-submap-activate! next-name))))
                  (hl--ensure-exit! next exit #t))
                (loop (cdr remaining) next)))))))))   ; the record propagates up
(define (global-set-key! sequence thunk) "Bind a key SEQUENCE to THUNK, the way
emacs' global-set-key does. SEQUENCE is what hl-kbd returns — a one-key spec
gives a chord, a multi-key one gives a list of chords:

  (global-set-key! (hl-kbd \"C-x C-f\") (lambda () (hl-exec! \"firefox\")))

This is define-key! in the default map, leaving it when the sequence ends —
see define-key! for the submap chain it builds and for #:top-level, which
binds the same sequence inside a MODAL submap instead.

A key you did not bind abandons the sequence and returns you to the default
map, the same as s-G — so a mistyped prefix costs you the keypress, not the
session.

This binds into global-map, the keymap that IS the default map \"\". It is a
keymap like any other, so it can be read and driven with hl-keymap-binds,
hl-keymap-child and the rest — and it is REBUILT EMPTY when the config reloads,
so a global-set-key! from a previous generation cannot linger."
  (define-key! global-map sequence thunk))

;; ---- define-keymap: a TOP-LEVEL keymap in one form ---------------------------
;;
;;   (define-keymap ekeys-map
;;     (((hl-kbd "C-x C-s") (lambda () (send-shortcut (hl-kbd "C-s"))))
;;      ((hl-kbd "C-x k")   (lambda () (hl-window-close! (hl-active-window))))))
;;
;; NAME is bound to the keymap; every CLAUSE is (KEYS THUNK OPTIONS...) and the
;; clauses are given as ONE LIST, in an extra pair of parens. The list is what
;; lets a config keep its clauses in a variable, splice them, or build them in a
;; loop and hand the result over — none of which a varargs form can do, since
;; varargs are positional forms and nothing but literal forms may appear there.
;;
;; #:submap places the binds and defaults to "", the default map. It is a
;; KEYWORD so that the common case — a top-level map — says nothing:
;;
;;   (define-keymap browser-map (((hl-kbd "C-l") (lambda () (reload)))))
;;
;; Each KEYS is what hl-kbd returns: a CHORD for a one-key spec, or a LIST OF
;; CHORDS for a sequence like "C-x C-s". BOTH work, which is the point of this
;; form: a sequence is walked by define-key!, which builds the "C-x" prefix
;; submap and binds the key that enters it, so a config never writes that
;; bookkeeping out by hand. OPTIONS are the ordinary bind flags (see bind-flags)
;; — #:description, #:repeat, #:locked and the rest.
;;
;; THE KEYMAP IT MAKES IS DISABLED, unlike hl-make-keymap's, and the disarm
;; happens LAST so it covers every bind including the prefix submaps
;; define-key! built along the way. This is the one form that knows it is
;; building a top-level keymap — the kind a config arms when some condition
;; holds — and arming it at read time would make the mode active from the moment
;; the config loaded. A helper that merely builds a keymap cannot know that, so
;; it arms.
;;
;; Arm and disarm with hl-keymap-enable! / hl-keymap-disable!.
;;
;; Documented as a COMMENT because it is a macro: a macro is not a procedure, so
;; the tree's docstring convention (a leading string in a `define`, read back by
;; procedure-documentation) does not apply — `define-syntax` takes no docstring,
;; and procedure-documentation on the result raises rather than answering. There
;; is no equivalent surface, so this block is it.
;;
;; IT LIVES IN THIS MODULE, NOT IN (hyprscheme bind), and the reason is the
;; module rule: it expands to define-key!, which is THIS module's, and to
;; hl--expand → hl-key-description, the emacs key layer, which names the prefix
;; submaps. A family imports only the kernel and core (tools/module-audit.py
;; enforces it), so bind.scm — where it used to live, and could bind one chord
;; and no more — could not reach either.
(define-syntax define-keymap
  (syntax-rules ()
    ((_ name #:submap submap ((keys thunk opts ...) ...))
     (define name
       (let ((km (hl-make-keymap #:submap submap)))
         ;; ONE define-key! PER CLAUSE, INLINE. Not (apply define-key! clause):
         ;; a clause holds FORMS — (hl-kbd "C-x C-s"), a lambda, option pairs —
         ;; and apply takes VALUES, so that spelling would need an eval per
         ;; clause and would report a bad clause at load time instead of the
         ;; reader naming the line.
         (define-key! km keys thunk opts ...) ...
         (hl-keymap-disable! km)
         km)))
    ((_ name ((keys thunk opts ...) ...))
     (define name
       (let ((km (hl-make-keymap #:submap "")))
         (define-key! km keys thunk opts ...) ...
         (hl-keymap-disable! km)
         km)))))

;; ---- attaching keymaps to a window condition ---------------------------------
;;
;; (hl-keymap-attach-to-tag! browser-map "browser") arms browser-map while the
;; focused window carries the tag "browser" and disarms it while it does not. The
;; mode follows the application instead of being toggled by hand, which is the
;; point: the terminal's keys are live in a terminal and dead in a browser, and
;; nothing has to be pressed to make that true.
;;
;; THE GENERAL FORM IS A PREDICATE, not a tag:
;; (hl-keymap-attach-to-window-condition! KEYMAP (lambda (w) ...)), and the tag
;; call is written in terms of it. The distinction is not decoration. The
;; condition a real config wants is usually NOT a membership test — "armed unless
;; this app speaks emacs" is a NEGATION, "armed in browsers or terminals" is a
;; DISJUNCTION, "armed unless emacs OR unless this is on" mixes the two, and
;; "armed while my mode flag is on" is not about the window at all. A tag is a
;; string a window either has or has not, so none of those can be a tag; a lambda
;; takes the window and can say anything Scheme can say.
;;
;; THE REGISTRY IS OURS, AND THE LISTENER ONLY EVER TOUCHES IT. This is the
;; property that makes it safe to disarm everything on a mismatch: the only
;; keymaps that go down are ones an attach put under this policy. A keymap the
;; config arms itself — including global-map — is not in the registry and is
;; never reached, so attaching one keymap cannot silently disarm another.
;;
;; THE EVENT IS window.active, WHOSE PAYLOAD IS #f WHEN FOCUS IS CLEARED — see
;; events.md for why that event is delivered rather than dropped. #f is passed to
;; the conditions like any other window; that is the reason the payload rule
;; matters, because with a dropped clear the last window's mode would stay armed
;; with nothing focused to explain it.
;;
;; THE TAGS IT COMPARES ARE hl-window-tags', i.e. the STRIPPED spelling, and
;; that is load-bearing rather than cosmetic. A tag applied by a window rule —
;; which is how a config normally tags an app's windows — is stored with a
;; trailing "*", so the raw list holds "browser*" while the config wrote
;; "browser". Comparing raw would make every tag condition silently never match,
;; and the failure is invisible: no error, the keymap just never arms.

(define (hl--condition-apply window) "Arm exactly the attached keymaps whose
condition is true of WINDOW and disarm the rest of them. WINDOW may be #f —
focus cleared — and is handed to the conditions as it is, so each of them
answers for that case itself.

Every attached keymap is visited on every evaluation and simply told what it
should be now; that is what hl-keymap-enabled? is for, and it is why this needs
no memory of what it armed last time. The alternative — diffing against the
previous window — would be a second piece of state that can be wrong about the
first."
  (for-each
    (lambda (entry)
      (if ((cdr entry) window)
          (hl-keymap-enable! (car entry))
          (hl-keymap-disable! (car entry))))
    hl--window-conditions)
  #t)

(define (hl-keymaps-recheck!) "Re-evaluate every attached keymap's condition
against the window focused RIGHT NOW, and arm or disarm it accordingly. Returns
#t.

This is for conditions that read something BESIDES the window — a mode flag, a
variable of your own. The registry is re-evaluated whenever the focused window
changes, and that is all a condition about the window needs; a condition that
also reads a global would otherwise stay stale until the next focus change, and
the keymap would be armed for the wrong thing in between. Call this from
whatever changes that global:

  (define (set-ekeys-mode on)
    (set! ekeys-mode on)
    (hl-keymaps-recheck!))

Cheap when nothing changed: a keymap that is already right is simply told again."
  (hl--condition-apply (hl-active-window)))

(define (hl-keymap-attach-to-window-condition! keymap condition) "Arm KEYMAP
exactly while CONDITION is true of the focused window, and disarm it while it is
not. Returns KEYMAP.

  (hl-keymap-attach-to-window-condition! emacs-map
    (lambda (w) (not (and w (member \"terminal\" (hl-window-tags w))))))

CONDITION is a procedure of ONE argument — the focused window, which is a window
handle or #f when NOTHING is focused. It must answer for #f like any other
value, and the usual answer is to disarm: no window means no application to have
an opinion about.

This is the general form, and a tag is just the common case of it:
hl-keymap-attach-to-tag! is this function with a condition written for you. Reach
for this one when the condition is NOT a membership test — a NEGATION, a
disjunction, a set of tags that must all hold, or a mode flag of your own — since
none of those is a tag a window can carry.

Attaching again REPLACES the condition rather than adding a second one, so the
last call is the keymap's whole condition. A config that means \"this tag OR that
tag\" writes the or into its lambda instead of attaching twice, which keeps the
two readings from being mistaken for each other.

The listener is installed on the FIRST attach and not before, so a config that
never attaches keymaps pays nothing for it, and the condition is evaluated once
here as well, so a config that attaches during load is right about the window
already focused rather than waiting for a focus change to notice.

Returns KEYMAP, so the call can sit in a definition that also reads well."
  (hl--keymap! "hl-keymap-attach-to-window-condition!" keymap)
  (unless (procedure? condition)
    (error "hl-keymap-attach-to-window-condition!: condition must be a procedure of one argument (a window, or #f when nothing is focused), got:"
           condition))
  (set! hl--window-conditions
        (cons (cons keymap condition)
              ;; the keymap's PREVIOUS condition goes, so a re-attach replaces
              (filter (lambda (e) (not (eq? (car e) keymap))) hl--window-conditions)))
  (unless hl--condition-listener
    (set! hl--condition-listener (hl-window-active-notification-add! hl--condition-apply)))
  (hl--condition-apply (hl-active-window))
  keymap)

(define (hl--tag-name tag) "TAG as the string a window's tags are compared
against. A symbol is the natural way to write a tag at a call site —
(hl-keymap-attach-to-tag! m 'browser) — and hl--str is the tree's one coercion
for a name, the same one the workspace and monitor resolvers use."
  (let ((s (hl--str tag)))
    (unless (and (string? s) (> (string-length s) 0))
      (error "hl-keymap-attach-to-tag!: tag must be a non-empty string or symbol, got:" tag))
    s))

(define (hl-keymap-attach-to-tag! keymap tag) "Arm KEYMAP exactly while the
focused window carries TAG, and disarm it while it does not.

  (define-keymap browser-map \"browser\" ...)
  (hl-keymap-attach-to-tag! browser-map \"browser\")

This is hl-keymap-attach-to-window-condition! with the condition written for
you — the keymap is armed while the window carries the tag — so nothing here is
a separate mechanism, and a config that outgrows the tag form can move to the
general one without changing anything else.

TAG is the tag as you WROTE it — the name in a window rule's #:tag, or the
string hl-window-tag-add! was given — a string, or a symbol for convenience. It
is matched against hl-window-tags, so it does not matter whether the tag
reached the window from a rule or by hand: the trailing \"*\" the compositor
puts on a rule-applied tag is not part of the name here.

A window carrying the tag is the whole condition, so a window tagged \"browser\"
arms a map attached to \"browser\" and leaves one attached to \"work\" alone. A
keymap that should be armed for EITHER tag is a lambda in
hl-keymap-attach-to-window-condition!, not two calls here — the second call
replaces the first.

The condition tests W before reading its tags, which is what makes focus being
cleared disarm the keymap: with no window there is no tag, and that case is the
one hl-window-active-notification-add! delivers as #f.

Returns KEYMAP, so the call can sit in a definition that also reads well."
  (hl--keymap! "hl-keymap-attach-to-tag!" keymap)
  (let ((name (hl--tag-name tag)))
    (hl-keymap-attach-to-window-condition!
      keymap
      (lambda (w) (and w (if (member name (hl-window-tags w)) #t #f))))))
