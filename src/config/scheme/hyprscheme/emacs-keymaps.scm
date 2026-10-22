;; hyprscheme/emacs-keymaps.scm — emacs key notation, and the machinery built on it
;;
;; This is SUGAR over the core API, not core API itself, which is why it is its
;; own module rather than part of (hyprscheme core): nothing here is needed to
;; drive the compositor, it is a way of SPELLING keys that configs may prefer.
;;
;; It is a composer, like (hyprscheme extras) — the second module allowed to
;; reach across families — because key sequences have to register binds
;; (hl-bind-add!), scope them (hl-submap) and switch between them
;; (hl-submap-activate!). A family may import only the kernel and core.
;;
;; Everything emacs-shaped lives here: the notation (hl-kbd/hl-key and the
;; hl--emacs-* tables), the canonicalisation (hl-key-regularise), the
;; logical/physical conversions, key-description, and the keymap layer built on
;; them (define-key!, global-set-key!, hl-submap!).

(define-module (hyprscheme emacs-keymaps)
  #:export (hl-key-regularise hl-key-logical-to-physical
            hl-key-physical-to-logical hl-key-description
            hl-kbd hl-key hl-submap! define-key! global-set-key!))

(use-modules (hyprscheme kernel))
(use-modules (hyprscheme core))
(use-modules (hyprscheme bind))

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

;; the modifier token names, for telling a modifier from a key: hl-kbd emits a
;; pure modifier list for a trailing-dash spec, and that must not be run through
;; the logical -> physical conversion (it has no key to convert)
(define hl--modifier-tokens
  '("CTRL" "ALT" "SHIFT" "SUPER" "MOD2" "MOD3" "MOD5" "CAPS"))

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
;; Two tables, both rebuilt every generation (the wipe re-reads this file, so a
;; fresh table is what a fresh generation gets). Hyprland's own hasSubmap() is
;; not reachable from a plugin, and we do not need it: we are the only thing
;; creating these submaps, so remembering them here is sufficient and cheaper.
(define hl--submaps-made (make-hash-table))   ; prefix submaps already built
(define hl--exit-bound (make-hash-table))     ; submaps already given our s-G
(define hl--catchall-bound (make-hash-table)) ; submaps already given our catchall

(define (hl--seq-chords sequence) "Normalise SEQUENCE to a list of chords:
hl-kbd returns a flat token list for a one-key spec and a list of chords for
a multi-key one, and both are accepted here."
  (if (and (pair? sequence) (pair? (car sequence)))
      sequence
      (list sequence)))

(define (hl-submap! name) "Return an ACTION that switches to submap NAME —
a thunk, so it can be handed to define-key!, hl-bind-add! or a timer:

  (global-set-key! (hl-kbd \"s-r\") (hl-submap! \"resize\"))

The submap must have at least one bind in it to exist, so define something in
it with define-key! first. Leave it again with s-G."
  (lambda () (hl-submap-activate! name)))

(define (hl--expand submap chord) "The name of the submap one key DEEPER: the
current submap's name extended by this key's canonical logical description, so
each level is an expansion of the one before. \"\" + C-a -> \"C-a\";
\"C-a\" + C-b -> \"C-a C-b\"; \"resize\" + C-a -> \"resize C-a\".

The base contributes nothing when it is the default map, so a top-level
sequence is named \"C-a\" rather than \" C-a\"."
  (let ((desc (hl-key-description chord)))
    (if (string=? submap "") desc (string-append submap " " desc))))

(define (hl--ensure-exit! submap target catchall?) "Give SUBMAP its ways out,
each once: an s-G that switches to TARGET, and — when CATCHALL? — a catchall
that switches there too, on any key SUBMAP does not bind.

The catchall is what makes a prefix behave like a prefix. A submap is a MODE:
Hyprland keeps one current submap and every bind outside it is dead, so a key
the map does not bind would otherwise do nothing AND leave you sitting in the
map, with nothing on screen to say so. A catchall fires only when no other bind
matched, and it consumes the key, so a bound key is unaffected.

The DEFAULT map is skipped: it is top level, so there is nothing to abandon to,
and binding s-G there would hijack the key globally rather than cancel out of a
mode."
  (unless (string=? submap "")
    ;; tracked separately: a TOP-LEVEL map still wants its s-G (to leave the
    ;; mode for good) but must not get a catchall (a key it does not bind must
    ;; not eject you from a mode you entered deliberately)
    (let ((want-g (not (hash-ref hl--exit-bound submap)))
          (want-c (and catchall? (not (hash-ref hl--catchall-bound submap)))))
      (when (or want-g want-c)
        (when want-g (hash-set! hl--exit-bound submap #t))
        (when want-c (hash-set! hl--catchall-bound submap #t))
        (hl-submap submap
                   (lambda ()
                     (when want-g
                       (hl-bind-add! (hl-kbd "s-G") (lambda () (hl-submap-activate! target))))
                     (when want-c
                       (hl-bind-add! (hl-kbd "catchall")
                                     (lambda () (hl-submap-activate! target))
                                     #:description "unmatched key: abandon the sequence"))))))))

(define* (define-key! submap sequence thunk #:key (top-level #t)) "Bind a key
SEQUENCE to THUNK inside SUBMAP — emacs' define-key, scoped to a map.

  (define-key! \"resize\" (hl-kbd \"h\") (lambda () (hl-window-size-set! ...)))

The sequence is walked from SUBMAP, one submap per prefix, each named by
EXPANDING the previous name with the key that reaches it (hl--expand), so
\"C-a C-b\" inside \"resize\" builds \"resize C-a\" and hangs C-b off it. The
prefix key is bound in the enclosing submap to enter the next; the final key is
bound in the last prefix's submap to THUNK. Two sequences sharing a prefix
share the submap, because the name is derived from it.

#:top-level says where the chain LEAVES to — both when s-G cancels it and when
THUNK completes:

  #t (default)  to the default map, \"\". You are ejected when it ends.
  #f            back to SUBMAP. You stay in the mode.

So (define-key! \"\" seq thunk) is exactly (global-set-key! seq thunk): with an
empty base both targets are \"\". A modal submap is one whose sequences are
defined #:top-level #f; it also gets its own s-G, to leave it for good.

Every map the chain builds is a KEY-SEQUENCE map, and abandons to EXIT on a key
it does not bind — as if you had pressed s-G — so a mistyped prefix does not
strand you in it. The one exception is a TOP-LEVEL map, which is SUBMAP itself
when #:top-level is #f: you entered that one deliberately, as a mode, so an
unrecognised key must not eject you from it. Its own s-G still leaves.

Returns the final bind's handle, the one carrying THUNK, so a binding can be
removed again with hl-unbind!."
  (let ((exit (if top-level "" submap)))
    ;; SUBMAP always gets its s-G, so a mode can be left for good. It gets the
    ;; catchall only when it is a key-SEQUENCE map: SUBMAP is a top-level map
    ;; exactly when it is its own exit, and you entered that one deliberately,
    ;; so a key it does not bind must not eject you from it.
    (hl--ensure-exit! submap "" (not (string=? submap exit)))
    (let loop ((remaining (hl--seq-chords sequence)) (current submap))
      (if (null? remaining)
          #f
        (let ((chord (car remaining)))
          (if (null? (cdr remaining))
              ;; the action: run it, then leave the chain however it ends — on
              ;; return OR on raise, so a throwing action cannot strand you
              (hl-submap current
                         (lambda ()
                           (hl-bind-add! chord
                                         (lambda ()
                                           (dynamic-wind (lambda () #t)
                                                         thunk
                                                         (lambda () (hl-submap-activate! exit)))))))
              (let ((next (hl--expand current chord)))
                (unless (hash-ref hl--submaps-made next)
                  (hash-set! hl--submaps-made next #t)
                  ;; the prefix key, in the enclosing submap, enters the next
                  (hl-submap current
                             (lambda ()
                               (hl-bind-add! chord (lambda () (hl-submap-activate! next)))))
                  (hl--ensure-exit! next exit #t))
                (loop (cdr remaining) next))))))))   ; the record propagates up

(define (global-set-key! sequence thunk) "Bind a key SEQUENCE to THUNK, the way
emacs' global-set-key does. SEQUENCE is what hl-kbd returns — a one-key spec
gives a chord, a multi-key one gives a list of chords:

  (global-set-key! (hl-kbd \"C-x C-f\") (lambda () (hl-exec! \"firefox\")))

This is define-key! in the default map, leaving it when the sequence ends —
see define-key! for the submap chain it builds and for #:top-level, which
binds the same sequence inside a MODAL submap instead.

A key you did not bind abandons the sequence and returns you to the default
map, the same as s-G — so a mistyped prefix costs you the keypress, not the
session."
  (define-key! "" sequence thunk))
