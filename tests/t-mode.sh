# Modes: a named flag with a colour, a state file, and two hooks.
#
# A mode change is nearly always also a KEYMAP change — a condition that reads a
# mode flag has to be re-evaluated when the flag moves — so the facility lives in
# emacs-keymaps and does the recheck itself. What is asserted here is that a mode
# change really is one action: the flag, the callback, the file, the colour and
# the recheck all happen from (mode-active-set! m #t), because the config this
# replaced had to write those five things out per mode and always forgot one.
#
# The colour is a WINDOW RULE matched on `focus`, born disabled. That is the
# part worth pinning: it is not a per-window paint, so "un-colouring" is not a
# thing anyone has to do — the rule stops matching when focus moves.
FAILED=0
ok()  { out=$($SCHEME "$1" 2>&1); [[ "$out" == "#t" ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
val() { out=$($SCHEME "$1" 2>&1); [[ "$out" == "$2" ]] || { echo "FAIL: $1 => [$out] want [$2]"; FAILED=1; }; }
err() { out=$($SCHEME "$1" 2>&1); [[ "$out" == *"$2"* ]] || { echo "FAIL (want error mentioning $2): $1 => [$out]"; FAILED=1; }; }

# ---- the record --------------------------------------------------------------
ok  '(hl-mode? (make-mode "m-basic"))'
ok  '(not (hl-mode? 5))'
val '(mode-name (make-mode "m-basic"))' '"m-basic"'
# a symbol name is accepted and normalised to a string — (define-mode x …) passes
# a symbol, and the name is also the state file's basename, so it has to be one
val '(mode-name (make-mode (quote m-sym)))' '"m-sym"'
# a mode is born OFF: arming one as a side effect of the config being READ is the
# trap hl-make-keymap and define-keymap both avoid
ok  '(not (mode-active? (make-mode "m-off")))'
# no colour is allowed
ok  '(not (mode-color (make-mode "m-nocolor")))'
val '(mode-color (make-mode "m-col" #:color #xffffa500))' '4294944000'

# a mode's file is DERIVED from its name, under $XDG_STATE_HOME/hyprscheme —
# NOT under omarchy/, which belongs to another program
val '(string-suffix? "/hyprscheme/m-basic" (mode-file (make-mode "m-basic")))' '#t'
ok  '(string-prefix? "/" (mode-file (make-mode "m-basic")))'

# a name has to be usable AS a name
err '(make-mode "")' "name must be a non-empty string or symbol"
err '(make-mode (list 1 2))' "name must be a non-empty string or symbol"
# and these refuse a non-mode rather than reading a random struct field
err '(mode-active? 5)' "not a mode"
err '(mode-active-set! 5 #t)' "not a mode"

# ---- the state file: exists exactly while the mode is on ---------------------
# The file is the mode's persisted state AND its colour, so a reload can restore
# it and a status bar can read it. Both are asserted, because either one alone
# would pass with the file being merely touched or merely written.
#
# The colour is read back as a NUMBER, which is the point of writing it as
# Scheme hex: whatever reads the file gets the integer, not text to parse.
# (read-line is the obvious thing to reach for and is NOT available in the
# generation — init.scm imports (rnrs io ports) and it lives in (ice-9 rdelim).)
ok  '(let ((m (make-mode "m-persist" #:color #xffffa500)))
       (and (not (file-exists? (mode-file m)))
            (mode-active-set! m #t)
            (file-exists? (mode-file m))
            (let ((v (call-with-input-file (mode-file m) read)))
              (and (integer? v)
                   (= v #xffffa500)
                   ;; turn it off again and the file goes
                   (mode-active-set! m #f)
                   (not (file-exists? (mode-file m)))))))'

# a mode with NO colour still gets a state file — the file is the STATE, the
# colour is a bonus — but the file is empty rather than containing "#xf"
ok  '(let ((m (make-mode "m-nocolorfile")))
       (mode-active-set! m #t)
       (let ((eof? (call-with-input-file (mode-file m)
                     (lambda (p) (eof-object? (read p))))))
         (mode-active-set! m #f)
         eof?))'

# ---- the hooks take the MODE, so one lambda can serve several ---------------
# This is what makes (define-mode …) read as data: the callback reads its own
# name and colour out of the argument instead of closing over them.
#
# The expected list is the whole history in reverse: a activated, b activated,
# a deactivated — three calls, one procedure, and the names prove the MODE
# arrived rather than a captured value.
ok  '(let ((seen (quote ())))
       (define (note m) (set! seen (cons (mode-name m) seen)) #t)
       (let ((a (make-mode "m-hook-a" #:activate note #:deactivate note))
             (b (make-mode "m-hook-b" #:activate note #:deactivate note)))
         (mode-active-set! a #t)
         (mode-active-set! b #t)
         (mode-active-set! a #f)
         (and (equal? seen (quote ("m-hook-a" "m-hook-b" "m-hook-a")))
              (not (mode-active? a))
              (mode-active? b))))'

# ---- mode-active-toggle! flips, and agrees with mode-active? -----------------
ok  '(let ((m (make-mode "m-toggle")))
       (and (not (mode-active? m))
            (mode-active-toggle! m)
            (mode-active? m)
            (mode-active-toggle! m)
            (not (mode-active? m))))'

# ---- the colour is a RULE, born disabled, armed only for the winner ----------
# A mode with a colour owns a window rule matched on `focus` — that is the whole
# "colour the focused window" mechanism, and it is why nothing has to be
# un-coloured later: the rule stops matching when focus moves.
#
# Asserted through the COMPOSITOR's view of the rule (hl-rule-enabled?), not
# through our own bookkeeping. A facility that "chose" a winner in Scheme and
# never armed anything would pass a Scheme-only check; this would not.
ok  '(let ((m (make-mode "m-ruled" #:color #xffffa500)))
       (and (hl-rule? (hl--mode-rule m))
            ;; born DISABLED: a mode defined but never turned on paints nothing
            (not (hl-rule-enabled? (hl--mode-rule m)))
            (mode-active-set! m #t)
            (hl-rule-enabled? (hl--mode-rule m))
            (mode-active-set! m #f)
            (not (hl-rule-enabled? (hl--mode-rule m)))))'

# a mode with NO colour has no rule at all, rather than a colourless one
ok  '(not (hl--mode-rule (make-mode "m-norule")))'

# FIRST REGISTERED WINS. Two modes, both with colours, both on: the one defined
# first keeps the armed rule and the second's stays off. This is the precedence
# rule the design settled on — the walk is in registration order — and it is
# asserted on BOTH rules, because a test that only looked at the first would
# pass with the second's also armed (which is the collision: two enabled rules
# writing the same border, winner decided by the compositor's own order).
ok  '(let ((first  (make-mode "m-win-first"  #:color #xffff0000))
           (second (make-mode "m-win-second" #:color #xff00ff00)))
       (mode-active-set! first #t)
       (mode-active-set! second #t)
       (and (hl-rule-enabled? (hl--mode-rule first))
            (not (hl-rule-enabled? (hl--mode-rule second)))
            ;; and turning the FIRST off hands the colour to the second, which
            ;; is the half a "first wins" walk gets wrong if it only ever arms
            (mode-active-set! first #f)
            (hl-rule-enabled? (hl--mode-rule second))))'

# ---- a mode change rechecks the keymaps --------------------------------------
# The whole reason this lives in emacs-keymaps. A condition that reads a mode
# flag is evaluated when the FOCUSED WINDOW changes and at no other time, so
# without the recheck the keys stay in their pre-toggle state until you change
# window — which is exactly the bug the config this replaced had to work around
# by hand, per mode, with a comment telling the reader not to forget.
ok  '(let ((m (make-mode "m-recheck")))
       (define km (hl-make-keymap))
       (hl-keymap-attach-to-window-condition! km (lambda (w) (mode-active? m)))
       (mode-active-set! m #t)
       (hl-keymap-enabled? km))'
ok  '(let ((m (make-mode "m-recheck2")))
       (define km2 (hl-make-keymap))
       (hl-keymap-attach-to-window-condition! km2 (lambda (w) (mode-active? m)))
       (mode-active-set! m #t)
       (mode-active-set! m #f)
       ;; and turning it OFF disarms the map again, in the same call
       (not (hl-keymap-enabled? km2)))'

# ---- define-mode binds AND registers ----------------------------------------
# The syntax-rule form: the name it binds is the mode, so a config writes its
# modes as definitions rather than as a make-mode call plus a set!.
ok  '(begin (define-mode t-defined #:color #xffffa500)
            (and (hl-mode? t-defined)
                 (string=? (mode-name t-defined) "t-defined")
                 (= (mode-color t-defined) #xffffa500)))'
ok  '(begin (define-mode t-defined2)
            (not (mode-active? t-defined2)))'

# ---- put the world back ------------------------------------------------------
# A mode left ON leaves its colour rule ARMED for the rest of the suite, and the
# rules a test created are never removed — the compositor keeps every one of them
# and re-evaluates the set on each focus change and each rule pass. So the modes
# this file turned on are turned off again, which disarms their rules and takes
# their state files away. (The rules themselves stay registered; there is no
# unbind for a rule, and the compositor discards them with the generation.)
#
# This is not only tidiness. With these left armed, t-zzz-monitors — which
# changes the monitor topology and polls a 10-second timeout for the teardown —
# failed intermittently under the extra load, naming the topology rather than
# anything here.
ok  '(begin (for-each (lambda (m) (when (mode-active? m) (mode-active-set! m #f)))
                      (list t-defined t-defined2))
            #t)'

exit $FAILED
