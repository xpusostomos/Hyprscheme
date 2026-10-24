# hl-kbd -> hl-key-description round trip.
#
# hl-kbd parses emacs notation into the PHYSICAL token list Hyprland binds;
# hl-key-description renders tokens back into emacs notation. So for most
# specs the round trip is the identity:
#
#     (hl-key-description (hl-kbd SPEC))  ==  SPEC
#
# The exceptions are exactly two, and both are canonicalisation rather than
# loss:
#
#   1. modifier ORDER — the description prints modifiers in emacs' order, so
#      "M-C-s" comes back as "C-M-s" (see the REORDER list below).
#   2. SHIFT folded into a character — "C-S-t" is the character T with CTRL,
#      which emacs writes "C-T" (see the FOLD list below).
#
# The spec lists are Scheme data evaluated in the compositor, deliberately:
# specs contain $, !, %, " and \ and would be mangled by shell quoting.
#
# Each list is evaluated as one expression returning #t, or the list of specs
# that failed — so a failure NAMES the spec rather than just failing.

FAILED=0

# assert-rt NAME EXPR — the expression returns #t, or a list of failures
assert-rt() {
  out=$($SCHEME "$2" 2>&1)
  if [[ "$out" != "#t" ]]; then
    echo "FAIL ($1): $out"
    FAILED=1
  fi
}

# the runner: (rt-check '(spec ...)) -> #t | (failing-spec ...)
RT_CHECK='
(define (rt-check specs)
  (let loop ((s specs) (bad (quote ())))
    (cond ((null? s) (if (null? bad) #t (reverse bad)))
          ((equal? (hl-key-description (hl-kbd (car s))) (car s)) (loop (cdr s) bad))
          (else (loop (cdr s) (cons (car s) bad))))))'

# ---- single keys ------------------------------------------------------------
assert-rt "single lowercase letters" "$RT_CHECK
(rt-check (quote (\"a\" \"b\" \"g\" \"h\" \"q\" \"x\" \"z\")))"

assert-rt "single uppercase letters" "$RT_CHECK
(rt-check (quote (\"A\" \"B\" \"G\" \"Q\" \"X\" \"Z\")))"

assert-rt "digits" "$RT_CHECK
(rt-check (quote (\"0\" \"1\" \"5\" \"9\")))"

# ---- modifier combinations --------------------------------------------------
# NOTE: no "S-" on a LETTER here. "S-a" is the character A — SHIFT folds into
# it and the description writes "A", so those live in the FOLD group below.
# S- round-trips only on keys with no shifted character form (function keys,
# named keys, arrows), which is why they carry it here.
assert-rt "one modifier" "$RT_CHECK
(rt-check (quote (\"C-a\" \"M-a\" \"s-a\" \"C-x\" \"s-q\" \"M-w\" \"S-<f1>\" \"S-RET\")))"

assert-rt "two modifiers, canonical order" "$RT_CHECK
(rt-check (quote (\"C-M-a\" \"C-s-a\" \"M-s-a\" \"C-M-<f1>\" \"C-s-<left>\" \"M-s-<f1>\" \"C-S-<f1>\")))"

assert-rt "three modifiers" "$RT_CHECK
(rt-check (quote (\"C-M-s-a\" \"C-M-S-<f1>\" \"C-s-<f1>\" \"C-M-s-<left>\" \"C-M-S-s-<f1>\")))"

assert-rt "four modifiers" "$RT_CHECK
(rt-check (quote (\"C-M-S-s-<f1>\")))"

# ---- special characters -----------------------------------------------------
# the shifted ones are the interesting case: "!" is SHIFT+1 physically, and the
# description has to fold that back into the character
assert-rt "unshifted punctuation" "$RT_CHECK
(rt-check (quote (\"-\" \"=\" \"[\" \"]\" \";\" \",\" \".\" \"/\" \"\`\")))"

assert-rt "shifted punctuation" "$RT_CHECK
(rt-check (quote (\"!\" \"@\" \"#\" \"$\" \"%\" \"^\" \"&\" \"*\" \"(\" \")\" \"_\" \"+\" \"{\" \"}\" \":\" \"<\" \">\" \"?\" \"~\" \"|\")))"

assert-rt "shifted punctuation with modifiers" "$RT_CHECK
(rt-check (quote (\"C-%\" \"s-!\" \"M-@\" \"C-M-#\" \"C-s-$\" \"s-?\" \"C-|\")))"

# ---- named keys -------------------------------------------------------------
assert-rt "named keys" "$RT_CHECK
(rt-check (quote (\"RET\" \"SPC\" \"TAB\" \"ESC\" \"DEL\")))"

assert-rt "named keys with modifiers" "$RT_CHECK
(rt-check (quote (\"C-RET\" \"M-SPC\" \"C-M-TAB\" \"s-ESC\" \"C-DEL\")))"

assert-rt "function keys" "$RT_CHECK
(rt-check (quote (\"<f1>\" \"<f2>\" \"<f5>\" \"<f12>\" \"C-<f1>\" \"s-<f5>\" \"C-M-<f12>\")))"

assert-rt "arrow and navigation keys" "$RT_CHECK
(rt-check (quote (\"<left>\" \"<right>\" \"<up>\" \"<down>\" \"<home>\" \"<end>\")))"

assert-rt "arrows with modifiers" "$RT_CHECK
(rt-check (quote (\"C-<left>\" \"M-<right>\" \"S-<up>\" \"s-<down>\" \"C-M-<left>\" \"C-S-s-<right>\")))"

# ---- multi-key sequences ----------------------------------------------------
assert-rt "two-key sequences" "$RT_CHECK
(rt-check (quote (\"C-x C-f\" \"C-x C-s\" \"C-x C-c\" \"s-a s-b\")))"

assert-rt "three-key sequences" "$RT_CHECK
(rt-check (quote (\"C-x C-f C-s\" \"C-x C-e a\" \"s-a s-b s-c\")))"

assert-rt "four-key sequences" "$RT_CHECK
(rt-check (quote (\"C-x C-f C-s C-g\" \"C-c C-e C-a x\")))"

assert-rt "sequences mixing kinds" "$RT_CHECK
(rt-check (quote (\"C-x <f1>\" \"C-x %\" \"C-x C-%\" \"C-x RET\" \"s-x C-y <left>\")))"

# ---- the two documented NON-round-trips -------------------------------------
# modifier order is canonicalised
assert-rt "modifier order is canonicalised" "$RT_CHECK
(and (equal? (hl-key-description (hl-kbd \"M-C-s\")) \"C-M-s\")
     (equal? (hl-key-description (hl-kbd \"s-C-M-x\")) \"C-M-s-x\")
     (equal? (hl-key-description (hl-kbd \"S-M-a\")) \"M-A\")
     (equal? (hl-key-description (hl-kbd \"s-S-a\")) \"s-A\"))"

# SHIFT folds into the character, for letters AND for punctuation
assert-rt "SHIFT folds into a character" "$RT_CHECK
(and (equal? (hl-key-description (hl-kbd \"C-S-t\")) \"C-T\")
     (equal? (hl-key-description (hl-kbd \"S-a\")) \"A\")
     (equal? (hl-key-description (hl-kbd \"S-s-a\")) \"s-A\")
     (equal? (hl-key-description (hl-kbd \"C-S-a\")) \"C-A\")
     (equal? (hl-key-description (hl-kbd \"M-S-a\")) \"M-A\")
     (equal? (hl-key-description (hl-kbd \"C-M-S-a\")) \"C-M-A\")
     (equal? (hl-key-description (hl-kbd \"C-S-5\")) \"C-%\")
     (equal? (hl-key-description (hl-kbd \"M-S-1\")) \"M-!\")
     (equal? (hl-key-description (hl-kbd \"C-S-3\")) \"C-#\"))"

# ---- hl-bind-add! takes ONE chord, never a sequence -------------------------
# hl-kbd returns a list of chords for a multi-key spec; that belongs to
# global-set-key!. hl-bind-add! must refuse it BY NAME rather than dying as
# "A bind requires a trigger" (which is what the C++ says when it skips the
# non-string tokens).
#
# The name it points at was WRONG until this was fixed: the message said
# "hl-bind-keys!", a function that has never existed anywhere in the tree (not
# in bind.scm, emacs-keymaps.scm or API.snapshot) — while the function that
# actually builds a submap chain for a sequence is global-set-key!. A message
# naming a function you cannot call is worse than naming none, and this test
# pinned the wording, so it was checking the bug IN rather than out.
#
# the message is captured and matched in SHELL, the way t-api.sh does it —
# call-with-string-output-port returns the accumulated STRING, not the thunk's
# value, so it cannot be asserted as #t
seq_msg=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-bind-add! (hl-make-bind (hl-kbd "C-x C-f") (lambda () #f))))))' 2>&1)
[[ "$seq_msg" == *"global-set-key!"* ]] || { echo "FAIL: a sequence fed to hl-bind-add! did not name global-set-key! => [$seq_msg]"; FAILED=1; }
# ...and the function it names EXISTS: the whole point of naming one
[[ "$($SCHEME '(procedure? global-set-key!)')" == "#t" ]] || { echo "FAIL: hl-bind-add!'s message names global-set-key!, which is not a procedure"; FAILED=1; }

# a single chord from hl-kbd still binds fine
assert-rt "hl-bind-add! accepts a single chord" "$RT_CHECK
(let ((b (hl-bind-add! (hl-make-bind (hl-kbd \"C-<f24>\") (lambda () #f))))) (hl-unbind! b))"

# ---- global-set-key! ---------------------------------------------------------
# hl-submap-activate! only succeeds if the submap EXISTS (setSubmap refuses
# otherwise), so activating a name and reading it back is a real check that the
# chain was built with the right canonical names.
# NOTE: a sequence of N keys makes N-1 submaps, one per PREFIX — the last key
# is bound in the final prefix's submap, not in a submap of its own. So
# "C-z C-z C-z" creates "C-z" and "C-z C-z" and nothing else.
assert-rt "global-set-key! builds a submap chain, named canonically" "$RT_CHECK
(begin
  (global-set-key! (hl-kbd \"C-z C-z C-z\") (lambda () #f))
  (and (hl-submap-activate! \"C-z\")         (equal? (hl-current-submap) \"C-z\")
       (hl-submap-activate! \"C-z C-z\")     (equal? (hl-current-submap) \"C-z C-z\")
       ;; the whole sequence is NOT a submap — its last key is the action
       (not (hl-submap-activate! \"C-z C-z C-z\"))
       (begin (hl-submap-exit!) #t)))"

# the submap is named by the LOGICAL description, so a shifted character in the
# sequence names it by the character and not by the key that carries it
assert-rt "sequence submap names are logical" "$RT_CHECK
(begin
  (global-set-key! (hl-kbd \"C-% C-f\") (lambda () #f))
  (and (hl-submap-activate! \"C-%\") (equal? (hl-current-submap) \"C-%\")
       (begin (hl-submap-exit!) #t)))"

# a one-key spec makes no submap at all — it is just a bind
assert-rt "global-set-key! accepts a single key" "$RT_CHECK
(let ((b (global-set-key! (hl-kbd \"<f22>\") (lambda () #f)))) (hl-unbind! b))"

# the action really is the thunk we passed: the returned handle can be fired
assert-rt "the returned handle carries the action" "$RT_CHECK
(let* ((fired #f)
       (b (global-set-key! (hl-kbd \"C-w C-w\") (lambda () (set! fired #t)))))
  (hl--bind-fire-rec b)
  (and fired (hl-unbind! b)))"

# two sequences sharing a prefix share the one submap
assert-rt "a shared prefix is registered once" "$RT_CHECK
(begin
  (global-set-key! (hl-kbd \"C-v C-a\") (lambda () #f))
  (global-set-key! (hl-kbd \"C-v C-b\") (lambda () #f))
  (and (hl-submap-activate! \"C-v\") (equal? (hl-current-submap) \"C-v\")
       (begin (hl-submap-exit!) #t)))"

# a four-key sequence chains four deep
assert-rt "four-key sequence" "$RT_CHECK
(begin
  (global-set-key! (hl-kbd \"C-u C-u C-u C-u\") (lambda () #f))
  (and (hl-submap-activate! \"C-u C-u C-u\") (equal? (hl-current-submap) \"C-u C-u C-u\")
       (begin (hl-submap-exit!) #t)))"

# ---- the emacs spellings bindings.scm relies on ------------------------------
# These pin the expansion the config assumes: Hyprland matches the UNSHIFTED
# keysym, so a shifted character must reach it as base key + explicit SHIFT.
# Written in emacs notation the config stays readable and this does the work.
assert-rt "shifted characters expand to base key + SHIFT" "$RT_CHECK
(and (equal? (hl-kbd \"M-<\") (quote (\"ALT\" \"SHIFT\" \"comma\")))
     (equal? (hl-kbd \"M->\") (quote (\"ALT\" \"SHIFT\" \"period\")))
     (equal? (hl-kbd \"M-%\") (quote (\"ALT\" \"SHIFT\" \"5\")))
     (equal? (hl-kbd \"C-/\") (quote (\"CTRL\" \"slash\")))
     (equal? (hl-kbd \"C-<\") (quote (\"CTRL\" \"SHIFT\" \"comma\"))))"

# and they round trip, so a submap name reads the way it was written
assert-rt "shifted emacs spellings round trip" "$RT_CHECK
(rt-check (quote (\"M-<\" \"M->\" \"M-%\" \"C-/\" \"C-<\")))"

# ---- define-key! and modal submaps ------------------------------------------
# define-key!'s first argument is a KEYMAP OBJECT, not a submap NAME. A submap
# is a name stamped on the binds that carry it; the keymap is what HOLDS them,
# so it is what gets passed. A string — the old spelling — is REFUSED BY NAME,
# like every other stale spelling in this tree: a config that was not converted
# must fail loudly rather than bind into a map nobody holds.
kmap_msg=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (define-key! "resize" (hl-kbd "h") (lambda () #f)))))' 2>&1)
[[ "$kmap_msg" == *"expected a keymap"* ]] || { echo "FAIL: define-key! accepted a submap NAME => [$kmap_msg]"; FAILED=1; }
[[ "$kmap_msg" == *"hl-make-keymap"* ]] || { echo "FAIL: the refusal does not say how to make a keymap => [$kmap_msg]"; FAILED=1; }

# a sequence defined IN a submap names its prefixes by EXPANDING that submap's
# name, so a modal submap's "C-a" cannot collide with the top level's
assert-rt "define-key! expands the base submap's name" "$RT_CHECK
(begin
  (define-key! (hl-make-keymap #:submap \"resize\") (hl-kbd \"C-a C-b\") (lambda () #f))
  (and (hl-submap-activate! \"resize C-a\") (equal? (hl-current-submap) \"resize C-a\")
       (begin (hl-submap-exit!) #t)))"

# define-key! on global-map IS global-set-key! — global-map is the default map,
# so both the exit and the target are ""
assert-rt "define-key! in the default map is global-set-key!" "$RT_CHECK
(begin
  (define-key! global-map (hl-kbd \"C-t C-t\") (lambda () #f))
  (and (hl-submap-activate! \"C-t\") (equal? (hl-current-submap) \"C-t\")
       (begin (hl-submap-exit!) #t)))"

# a modal submap is escapable: define-key! gives the BASE an s-G out
assert-rt "a modal submap gets its own s-G" "$RT_CHECK
(begin
  (define-key! (hl-make-keymap #:submap \"modal\") (hl-kbd \"x\") (lambda () #f))
  (hl-submap-activate! \"modal\")
  (equal? (hl-current-submap) \"modal\"))"

# ENTERING a submap from a bind is a plain lambda — the thunk a bind takes, and
# (hl-submap-activate! NAME) inside it. Written out rather than having a curried
# helper for it: the helper was one line and prevented nothing.
#
# Note the asymmetry, which is why there is no helper: hl-submap-exit! takes NO
# argument, so IT can be handed to a bind directly and really is the action,
# while entering needs an argument and so would need currying.
assert-rt "a bind can enter a submap" "$RT_CHECK
(begin
  (define-key! (hl-make-keymap #:submap \"enter-me\") (hl-kbd \"y\") (lambda () #f))
  (let ((b (hl-bind-add! (hl-make-bind (hl-kbd \"C-<f21>\")
                                       (lambda () (hl-submap-activate! \"enter-me\"))))))
    (hl--bind-fire-rec b)
    (let ((in? (equal? (hl-current-submap) \"enter-me\")))
      (hl-submap-exit!)
      (and (hl-bind? b) (hl-unbind! b) in?))))"

# ...and the exit needs no lambda at all: it takes no argument, so the
# procedure itself is the thunk
assert-rt "hl-submap-exit! is usable directly as a bind action" "$RT_CHECK
(let ((b (hl-bind-add! (hl-make-bind (hl-kbd \"C-<f23>\") hl-submap-exit!))))
  (and (hl-bind? b) (hl-unbind! b)))"

# THE point of #:top-level, tested by firing the action and reading the submap
# back: #f returns to the base (you stay in the mode), #t ejects to the default
# map. Firing the returned handle runs the real action, exit included.
assert-rt "define-key! #:top-level #f returns to the base submap" "$RT_CHECK
(let ((b (define-key! (hl-make-keymap #:submap \"stayer\") (hl-kbd \"a b\") (lambda () #f) #:top-level #f)))
  (hl-submap-activate! \"stayer a\")
  (hl--bind-fire-rec b)
  (equal? (hl-current-submap) \"stayer\"))"

assert-rt "define-key! #:top-level #t ejects to the default map" "$RT_CHECK
(let ((b (define-key! (hl-make-keymap #:submap \"ejector\") (hl-kbd \"a b\") (lambda () #f))))
  (hl-submap-activate! \"ejector a\")
  (hl--bind-fire-rec b)
  (equal? (hl-current-submap) \"\"))"

# ---- an unmatched key abandons a key-sequence map ---------------------------
# A submap is a MODE: Hyprland keeps ONE current submap and every bind outside
# it is dead, so a key the map does not bind would otherwise do nothing AND
# leave you sitting in the map, with nothing on screen to say so. define-key!
# therefore gives every key-SEQUENCE map a catchall that abandons to the map's
# exit, exactly as its s-G does. A TOP-LEVEL map (#:top-level #f) is a mode you
# entered deliberately, so it gets the s-G but NOT the catchall.
#
# Nothing exposed "the binds in a submap" before the keymap existed. Now the map
# the sequence was built in IS the thing in hand, so each case asserts twice:
# hl-keymap-marked? says the map gave itself that exit, and hl-unbind-key! says
# the compositor really holds it — which is the half that makes it fire.
assert-rt "a key-sequence map gets a catchall" "$RT_CHECK
(let ((km (hl-make-keymap #:submap \"seq-catch\")))
  (hl-unbind-key! (hl-kbd \"catchall\"))          ; drain
  (define-key! km (hl-kbd \"a\") (lambda () #f))
  (and (hl-keymap-marked? km (quote catchall))    ; the map knows it gave one
       (hl-unbind-key! (hl-kbd \"catchall\"))))"

assert-rt "a top-level map gets an s-G but no catchall" "$RT_CHECK
(let ((km (hl-make-keymap #:submap \"modal-nocatch\")))
  (hl-unbind-key! (hl-kbd \"catchall\"))          ; drain
  (define-key! km (hl-kbd \"a\") (lambda () #f) #:top-level #f)
  (and (hl-keymap-marked? km (quote exit))        ; it got its s-G...
       (not (hl-keymap-marked? km (quote catchall)))   ; ...and no catchall
       (hl-unbind-key! (hl-kbd \"s-G\"))          ; the s-G is really registered
       (not (hl-unbind-key! (hl-kbd \"catchall\")))))"

assert-rt "the default map gets neither" "$RT_CHECK
(begin
  (hl-unbind-key! (hl-kbd \"catchall\"))          ; drain both
  (hl-unbind-key! (hl-kbd \"s-G\"))
  (define-key! global-map (hl-kbd \"C-<f19>\") (lambda () #f))
  (and (not (hl-keymap-marked? global-map (quote exit)))
       (not (hl-keymap-marked? global-map (quote catchall)))
       (not (hl-unbind-key! (hl-kbd \"catchall\")))
       (not (hl-unbind-key! (hl-kbd \"s-G\")))))"

# WHERE the catchall abandons to is not observable here — reaching it needs the
# bind's handle, and nothing hands it out. It is the same `exit` the action path
# uses, which the #:top-level pair above tests by firing and reading the submap
# back, so the target is covered there rather than duplicated.

# ---- the physical/logical pair itself ---------------------------------------
assert-rt "logical -> physical -> logical is stable" "$RT_CHECK
(let loop ((specs (quote (\"a\" \"A\" \"%\" \"!\" \"5\" \".\" \"Return\" \"F1\")))
           (bad (quote ())))
  (cond ((null? specs) (if (null? bad) #t (reverse bad)))
        ((equal? (hl-key-physical-to-logical
                   (hl-key-logical-to-physical (list (car specs))))
                 (list (car specs)))
         (loop (cdr specs) bad))
        (else (loop (cdr specs) (cons (car specs) bad)))))"

assert-rt "hl-kbd output is the PHYSICAL form Hyprland needs" "$RT_CHECK
(and (equal? (hl-kbd \"C-%\") (quote (\"CTRL\" \"SHIFT\" \"5\")))
     (equal? (hl-kbd \"s-M\") (quote (\"SUPER\" \"SHIFT\" \"m\")))
     (equal? (hl-kbd \"a\") (quote (\"a\")))
     (equal? (hl-kbd \"s-period\") (quote (\"SUPER\" \"period\")))
     (equal? (hl-kbd \"C-M-s\") (quote (\"CTRL\" \"ALT\" \"s\"))))"

exit $FAILED
