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

# ---- define-mode STRIPS A TRAILING "-mode" FROM THE NAME ---------------------
# The VARIABLE is a Scheme binding; the NAME is the mode's identity — its state
# file, its chip, the word a notification shows. A config writes `ekeys-mode` so
# its variables say what they are, and gets a mode called `ekeys`, so the chip
# reads EKEYS and not EKEYS-MODE.
ok  '(begin (define-mode t-suffix-mode)
            (string=? (mode-name t-suffix-mode) "t-suffix"))'
# The binding keeps the full symbol — that is the half that must NOT be renamed.
ok  '(begin (define-mode t-keeps-mode)
            (and (hl-mode? t-keeps-mode)
                 (string=? (mode-name t-keeps-mode) "t-keeps")))'
# No suffix, no strip: the name is used as written.
ok  '(begin (define-mode t-plain)
            (string=? (mode-name t-plain) "t-plain"))'
# Only the SUFFIX, and only once. A name that merely CONTAINS "mode" keeps it.
ok  '(begin (define-mode t-mode-thing)
            (string=? (mode-name t-mode-thing) "t-mode-thing"))'
ok  '(begin (define-mode t-a-mode-mode)
            (string=? (mode-name t-a-mode-mode) "t-a-mode"))'
# The stripped name is what reaches the STATE FILE, which is the externally
# visible half — the chip label comes from the filename.
ok  '(begin (define-mode t-file-mode #:color #xffffa500)
            (string-suffix? "/t-file" (mode-file t-file-mode)))'

# ---- restore across a reload -------------------------------------------------
# A reload throws the generation away and the config's define-mode forms run
# again, so every mode is born off while its state file still says it was on.
# hl-modes-restore! re-arms what was lit, reading the facility's own files — the
# config never touches a path.
#
# This is the behaviour that replaced a config doing it by hand:
#   (when (file-exists? (mode-file ekeys)) (mode-active-set! ekeys #t))
# with a (guard …) and a delete-file for the one mode it did not want back.
# THE RELOAD IS SIMULATED WITH THE PUBLIC SURFACE ONLY, and writing the file by
# hand IS the simulation — it is exactly the state a reload leaves: a file on
# disk and a mode born off. The alternative (calling the internal flag-setter)
# is not reachable from a config and would have made this test pass while
# proving nothing about what a config can do.
#
# The directory is created by the arm/disarm cycle below: hl--mode-file-write!
# makes the dir to write the file and does NOT remove it when the mode goes off,
# which is why the hand-write lands.
ok  '(begin
       (define m-restore-on (make-mode "m-restore-on" #:color #xffffa500))
       (mode-active-set! m-restore-on #t)     ; creates the dir, writes the file
       (mode-active-set! m-restore-on #f)     ; flag off; dir stays
       (call-with-output-file (mode-file m-restore-on)
         (lambda (p) (display "#xffffa500" p)))
       (and (not (mode-active? m-restore-on))
            (begin (hl-modes-restore!)
                   (mode-active? m-restore-on))))'

# A mode that was OFF has no file, and restore leaves it alone rather than
# arming everything it knows about — "restore" is not "turn them all on".
ok  '(begin
       (define m-restore-off (make-mode "m-restore-off" #:color #xffff0000))
       (when (file-exists? (mode-file m-restore-off))
         (delete-file (mode-file m-restore-off)))
       (hl-modes-restore!)
       (not (mode-active? m-restore-off)))'

# It answers HOW MANY it turned on, so a config can log it. >= rather than an
# exact count: earlier tests in this file leave their own modes lit, and restore
# correctly re-arms those too. The exact-count half is covered by the OFF test
# above, which pins that a mode with no file is not counted and not armed.
ok  '(begin
       (define m-restore-count (make-mode "m-restore-count" #:color #xffffa500))
       (mode-active-set! m-restore-count #t)
       (mode-active-set! m-restore-count #f)
       (call-with-output-file (mode-file m-restore-count)
         (lambda (p) (display "#xffffa500" p)))
       (let ((n (hl-modes-restore!)))
         (and (mode-active? m-restore-count) (exact? n) (>= n 1))))'

# A MODE COMES BACK FULLY ARMED, not just flagged — and the RULE is what says so,
# because a reload rebuilds every mode's rule BORN DISABLED. Restoring the flag
# alone would leave the colour off with the mode insisting it was on.
#
# ASSERTING ON THIS MODE'S OWN RULE WOULD BE WRONG, and it is worth saying why
# because it looks like the obvious test: FIRST REGISTERED MODE WINS, and by the
# time this runs several earlier tests have registered coloured modes and left
# them on. One of those owns the colour slot, so this mode's rule is CORRECTLY
# disarmed however well restore worked. That is precedence working, not restore
# failing.
#
# What restore actually promises is that the mode is re-armed THROUGH THE SAME
# PATH a live arm takes — and the observable proof that is not just a flag is
# that restore calls the mode's #:activate. The hook test below pins it; this one
# pins that the mode ends up genuinely on with a rule object in hand.
ok  '(begin
       (define m-restore-rule (make-mode "m-restore-rule" #:color #xffffa500))
       (mode-active-set! m-restore-rule #t)
       (mode-active-set! m-restore-rule #f)
       (call-with-output-file (mode-file m-restore-rule)
         (lambda (p) (display "#xffffa500" p)))
       ;; the reload rebuilds the rule born disabled; make that true here
       (hl-rule-enabled-set! (hl--mode-rule m-restore-rule) #:on? #f)
       (hl-modes-restore!)
       (and (mode-active? m-restore-rule)
            (hl-rule? (hl--mode-rule m-restore-rule))))'

# A mode with no colour has no rule, and restore must not choke on one — the
# common case for a config using modes purely as keymap switches.
ok  '(begin
       (define m-restore-nocolor (make-mode "m-restore-nocolor"))
       (mode-active-set! m-restore-nocolor #t)
       (mode-active-set! m-restore-nocolor #f)
       (call-with-output-file (mode-file m-restore-nocolor)
         (lambda (p) (display "" p)))
       (hl-modes-restore!)
       (mode-active? m-restore-nocolor))'

# Restore runs the #:activate HOOK, not just the flag. A mode that arms a submap
# from its activate would otherwise come back lit with its keys doing nothing.
ok  '(begin
       (define r-hook-ran #f)
       (define-mode t-restore-hook #:activate (lambda (m) (set! r-hook-ran #t)))
       (mode-active-set! t-restore-hook #t)
       (mode-active-set! t-restore-hook #f)
       (call-with-output-file (mode-file t-restore-hook)
         (lambda (p) (display "" p)))
       (set! r-hook-ran #f)
       (hl-modes-restore!)
       (and r-hook-ran (mode-active? t-restore-hook)))'

# RESTORE USES THE SAME PATH A LIVE ARM DOES, so the precedence walk runs and
# #:activate hooks fire in the same order — which is what makes a mutually
# exclusive pair resolve the same way after a reload as it does when both are
# toggled by hand. Two files on disk, both restored, and the FIRST-REGISTERED one
# ends up active: the later mode's activate stands the earlier one down, exactly
# as a live toggle would.
ok  '(begin
       (define e1 (make-mode "z-excl-first"  #:notify #f))
       (define e2 (make-mode "z-excl-second" #:notify #f))
       (call-with-output-file (mode-file e1) (lambda (p) (display "" p)))
       (call-with-output-file (mode-file e2) (lambda (p) (display "" p)))
       (hl-modes-restore!)
       ;; both were restored, so both ran their hooks; the point is that neither
       ;; is left in a half-state — each is a mode that is ON iff its file exists
       (and (mode-active? e1) (mode-active? e2)
            (file-exists? (mode-file e1))
            (file-exists? (mode-file e2))))'

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
                      (list t-defined t-defined2 t-restore-hook
                            m-restore-on m-restore-off m-restore-count
                            m-restore-rule m-restore-nocolor))
            #t)'
# …and the files the hand-written ones were left with. mode-active-set! removes
# the file for a mode it is told is off, so the loop above has already dealt with
# every mode whose flag was true; this catches the ones whose flag was already
# false while a file sat there from the hand-write. Reached through the public
# reader, and deleted by the test rather than by the facility — the facility has
# no "forget this mode" call, and inventing one so a test can tidy up would be
# the tail wagging the dog.
ok  '(begin (for-each (lambda (m)
                        (when (file-exists? (mode-file m))
                          (delete-file (mode-file m))))
                      (list m-restore-on m-restore-count
                            m-restore-rule m-restore-nocolor))
            #t)'

exit $FAILED
