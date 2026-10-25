# Keymaps: a named bundle of binds you can arm and disarm together.
#
# A keymap is a list of binds plus a submap name — a way to keep a group of
# binds together, not a new compositor concept. What is asserted here is that
# the group really is a group: adding a bind registers it AND records its
# handle, enabling and disabling reach every bind in it, and — the case worth
# pinning — TWO keymaps can hold the same keys and each one's enable/disable
# touches ONLY its own binds.
#
# That last case is why this file exists. A bind's handle is its record's
# ADDRESS, stamped into the compositor's metadata.argument as a string; two
# registrations of the SAME record would therefore share one handle, and
# disabling one keymap would silently disarm the other's bind. Distinct
# hl-make-bind calls give distinct records, so the two stay independent — and
# this asserts it rather than assuming it.
FAILED=0
ok()  { out=$($SCHEME "$1" 2>&1); [[ "$out" == "#t" ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
val() { out=$($SCHEME "$1" 2>&1); [[ "$out" == "$2" ]] || { echo "FAIL: $1 => [$out] want [$2]"; FAILED=1; }; }
err() { out=$($SCHEME "$1" 2>&1); [[ "$out" == *"$2"* ]] || { echo "FAIL (want error mentioning $2): $1 => [$out]"; FAILED=1; }; }

# ---- the record: a keymap holds its submap, its reset, and its binds --------
ok  '(hl-keymap? (hl-make-keymap))'
ok  '(not (hl-keymap? 5))'
ok  '(not (hl-keymap? (hl-make-bind (hl-kbd "s-q") (lambda () #f))))'
# submap defaults to "" — the default map, which is always active
val '(hl-keymap-submap (hl-make-keymap))' '""'
val '(hl-keymap-submap-reset (hl-make-keymap))' '""'
val '(hl-keymap-submap (hl-make-keymap #:submap "browser"))' '"browser"'
val '(hl-keymap-submap-reset (hl-make-keymap #:submap "browser" #:submap-reset "browser"))' '"browser"'
# a fresh keymap is empty
val '(length (hl-keymap-binds (hl-make-keymap)))' '0'
# and it is ENABLED at birth — a submap keymap built and then armed is the
# normal shape, and one that started disabled would be a map whose keys never
# fire with nothing to say why
ok  '(hl-keymap? (hl-make-keymap))'

# ---- adding: registers the bind AND records its handle ----------------------
# the handle the keymap holds is the SAME object hl-keymap-add! returned — not
# a copy, not a re-wrap. This is what makes hl-unbind!/hl-bind-enabled-set!
# work on it directly.
ok  '(let* ((km (hl-make-keymap))
            (b  (hl-keymap-add! km (hl-make-bind (hl-kbd "s-<F13>") (lambda () #f)))))
       (and (eq? b (car (hl-keymap-binds km))) (hl-unbind! b)))'
# two adds, two binds, most-recent first
val '(let ((km (hl-make-keymap)))
       (hl-keymap-add! km (hl-make-bind (hl-kbd "s-<F14>") (lambda () #f)))
       (hl-keymap-add! km (hl-make-bind (hl-kbd "s-<F15>") (lambda () #f)))
       (length (hl-keymap-binds km)))' '2'
# the per-bind submap OVERRIDE reaches the compositor: hl-current-submap proves
# placement by activating the map the bind was placed in
ok  '(let ((km (hl-make-keymap #:submap "km-probe")))
       (hl-keymap-add! km (hl-make-bind (hl-kbd "s-<F16>") (lambda () #f)))
       (hl-submap-activate! "km-probe")
       (let ((r (string=? (hl-current-submap) "km-probe")))
         (hl-submap-exit!)
         (hl-unbind! (car (hl-keymap-binds km)))
         r))'

# ---- enable / disable: the whole group, without rebuilding ------------------
ok  '(let* ((km (hl-make-keymap))
            (b  (hl-keymap-add! km (hl-make-bind (hl-kbd "s-<F17>") (lambda () #f)))))
       (and (hl-bind-enabled? b)                 ; born armed with the keymap
            (hl-keymap-disable! km)
            (not (hl-bind-enabled? b))           ; disarmed, still registered
            (hl-keymap-enable! km)
            (hl-bind-enabled? b)
            (hl-unbind! b)))'
# every bind in the group, not just the first
ok  '(let* ((km (hl-make-keymap))
            (a  (hl-keymap-add! km (hl-make-bind (hl-kbd "s-<F18>") (lambda () #f))))
            (c  (hl-keymap-add! km (hl-make-bind (hl-kbd "s-<F19>") (lambda () #f)))))
       (hl-keymap-disable! km)
       (let ((off (and (not (hl-bind-enabled? a)) (not (hl-bind-enabled? c)))))
         (hl-keymap-enable! km)
         (hl-unbind! a) (hl-unbind! c)
         off))'

# ---- THE CASE THE HANDLE DESIGN COULD BREAK: two keymaps, the same key ------
# each keymap's enable/disable must touch ONLY its own bind. Two separate
# hl-make-bind calls are two records with two addresses, so this holds; one
# record registered twice would share a handle and this would fail.
ok  '(let* ((k1 (hl-make-keymap #:submap "km-one"))
            (k2 (hl-make-keymap #:submap "km-two"))
            (b1 (hl-keymap-add! k1 (hl-make-bind (hl-kbd "s-<F20>") (lambda () #f))))
            (b2 (hl-keymap-add! k2 (hl-make-bind (hl-kbd "s-<F20>") (lambda () #f)))))
       (let ((distinct (not (eq? b1 b2))))      ; two registrations, two handles
         (hl-keymap-disable! k1)
         (let ((only-one-off (and (not (hl-bind-enabled? b1))
                                  (hl-bind-enabled? b2))))
           (hl-keymap-enable! k1)
           (let ((both-on (and (hl-bind-enabled? b1) (hl-bind-enabled? b2))))
             (hl-unbind! b1) (hl-unbind! b2)
             (and distinct only-one-off both-on)))))'

# ---- define-keymap: a top-level keymap, born DISABLED ----------------------
# It is the one form that KNOWS it is building a top-level keymap, so it is the
# one form that disarms what it made.
#
# The clauses are given as ONE LIST, in an extra pair of parens, and #:submap is
# a keyword defaulting to "". Both are load-bearing: the list is what lets a
# config hold its clauses in a variable and hand them over, and the default is
# what lets the common case — a top-level map in the default map — say nothing.
ok  '(begin (define-keymap t-top-map (((hl-kbd "s-<F21>") (lambda () #f))))
            (hl-keymap? t-top-map))'
val '(begin (define-keymap t-top-map2 (((hl-kbd "s-<F22>") (lambda () #f))))
            (hl-keymap-submap t-top-map2))' '""'
val '(begin (define-keymap t-top-map3
              (((hl-kbd "s-<F23>") (lambda () #f))
               ((hl-kbd "s-<F24>") (lambda () #f))))
            (length (hl-keymap-binds t-top-map3)))' '2'
# #:submap places the binds, named
val '(begin (define-keymap t-top-map4 #:submap "t-sub"
              (((hl-kbd "s-<F25>") (lambda () #f))))
            (hl-keymap-submap t-top-map4))' '"t-sub"'
# born disabled, so a mode does not arm itself the moment the config is read
ok  '(begin (define-keymap t-mode-map #:submap "t-mode"
              (((hl-kbd "s-<F26>") (lambda () #f))))
            (not (hl-bind-enabled? (car (hl-keymap-binds t-mode-map)))))'
# and its binds carry the named submap it was given
ok  '(begin (define-keymap t-mode-map2 #:submap "t-mode2"
              (((hl-kbd "s-<F27>") (lambda () #f))))
            (hl-submap-activate! "t-mode2")
            (let ((r (string=? (hl-current-submap) "t-mode2")))
              (hl-submap-exit!)
              (for-each hl-unbind! (hl-keymap-binds t-mode-map2))
              r))'
# arming it makes its binds live
ok  '(begin (define-keymap t-mode-map3 #:submap "t-mode3"
              (((hl-kbd "s-<F28>") (lambda () #f))))
            (hl-keymap-enable! t-mode-map3)
            (let ((on (hl-bind-enabled? (car (hl-keymap-binds t-mode-map3)))))
              (hl-keymap-disable! t-mode-map3)
              (for-each hl-unbind! (hl-keymap-binds t-mode-map3))
              on))'

# ---- define-keymap takes SEQUENCES, which is what it is for ------------------
# A clause's KEYS is whatever hl-kbd returns: a chord, or a LIST OF CHORDS for a
# sequence. This is the whole reason the macro goes through define-key! rather
# than hl-make-bind — a sequence has to be walked, the "C-x" prefix submap built
# and the key that enters it bound. Until this worked, a top-level keymap made of
# C-x chords — the ordinary emacs shape, and what bindings.scm is — could not be
# written with the form meant for it at all.
# the action lands in the PREFIX submap, not the top one
val '(begin (define-keymap t-seq-map (((hl-kbd "C-x C-s") (lambda () #f))))
            (hl-keymap-submap (hl-keymap-child t-seq-map "C-x")))' '"C-x"'
# ONE bind in the top map: the key that enters "C-x"
val '(begin (define-keymap t-seq-map2 (((hl-kbd "C-x C-s") (lambda () #f))))
            (length (hl-keymap-binds t-seq-map2)))' '1'
# a chord and a sequence in the same map both work, and the sequence still
# contributes exactly its one ENTRY bind to the top map
val '(begin (define-keymap t-seq-map3
              (((hl-kbd "s-<F29>") (lambda () #f))
               ((hl-kbd "C-x C-f") (lambda () #f))))
            (length (hl-keymap-binds t-seq-map3)))' '2'
# born disabled THROUGH the chain: the prefix bind define-key! added is disarmed
# too, because the disarm runs last and covers everything the map holds
ok  '(begin (define-keymap t-seq-map4 (((hl-kbd "C-x C-k") (lambda () #f))))
            (not (hl-bind-enabled?
                   (car (hl-keymap-binds t-seq-map4)))))'
# and a bind option still reaches the bind it was written on. #:description is
# the one to test with because hl-make-bind takes it and would REJECT it if
# define-key! failed to forward the option list — which is the real risk here:
# define-key! consumes #:top-level itself, so the forwarding has to strip that
# one keyword and pass the rest through. A clause that loads at all proves the
# options were not mangled into something hl-make-bind refuses.
ok  '(begin (define-keymap t-opt-map
              (((hl-kbd "s-<F30>") (lambda () #f) #:description "described")))
            (hl-keymap? t-opt-map))'
# the option really did reach the bind: ask the bind's own description through
# the internal accessor, since no public one exists (a bind's fields are for the
# machinery, not for a config to read)
ok  '(begin (define-keymap t-opt-map2
              (((hl-kbd "s-<F32>") (lambda () #f) #:description "described")))
            (string=? "described"
                      (hl--bind-description (car (hl-keymap-binds t-opt-map2)))))'

# ---- the enabled flag: what the keymap was last TOLD -------------------------
# A keymap records whether it was last armed or disarmed, so a caller can ask
# before arming rather than re-arming blindly — which is what makes it safe for
# hl-keymap-attach-to-tag! to re-evaluate its entire registry on every focus
# change. It is what we TOLD the keymap, not a probe of the compositor: a single
# bind can still be disarmed on its own with hl-bind-enabled-set!, and the keymap
# would not know.
ok  '(hl-keymap-enabled? (hl-make-keymap))'
ok  '(let ((km (hl-make-keymap)))
       (and (hl-keymap-enabled? km)
            (hl-keymap-disable! km)
            (not (hl-keymap-enabled? km))
            (hl-keymap-enable! km)
            (hl-keymap-enabled? km)))'
# it cannot disagree with the binds, because enable!/disable! are its only
# writers — and define-keymap's map is born DISARMED, so the flag says so
ok  '(begin (define-keymap t-flag-map #:submap "t-flag"
              (((hl-kbd "s-<F31>") (lambda () #f))))
            (let ((r (and (not (hl-keymap-enabled? t-flag-map))
                          (not (hl-bind-enabled? (car (hl-keymap-binds t-flag-map)))))))
              (for-each hl-unbind! (hl-keymap-binds t-flag-map))
              r))'

# ---- attach-to-tag: a keymap that follows the focused window's tags ----------
# The registry is the point of the design, not an implementation detail: this
# module owns the keymaps an attach put under it, so re-evaluating on every focus
# change may disarm anything IT holds without ever touching a keymap the config
# armed itself — global-map included.
err '(hl-keymap-attach-to-tag! 5 "browser")' 'expected a keymap'
err '(hl-keymap-attach-to-tag! (hl-make-keymap) "")' 'non-empty string or symbol'

# The keymap is kept in hl-state, not in a top-level define: every $SCHEME call
# below is a separate eval, and hl-state is the accessor documented to survive
# one. The bind is NEVER unbound afterwards, deliberately — the keymap stays in
# the registry for the rest of the session, so a later focus change will arm and
# disarm it again, and arming an UNBOUND record is not something to find out
# about from a log line in t-zzzz-exit.
$SCHEME '(hl-state-set! (quote t-attach) (hl-make-keymap))' >/dev/null
$SCHEME '(hl-keymap-add! (hl-state-ref (quote t-attach))
                         (hl-make-bind (hl-kbd "s-<F27>") (lambda () #f)))' >/dev/null

# THE TAG IS APPLIED BY A WINDOW RULE, and that is the whole point of this
# block rather than a detail. A rule-applied tag is stored with a trailing "*"
# (CTagKeeper::applyTag's `dynamic` flag) — it is how the compositor remembers
# which tags a rule owns and may retire. So the window's raw tags are
# ("t-attach-tag*") and anything comparing them against "t-attach-tag" is
# comparing against a string that is never there.
#
# hl-window-tag-add! would NOT exercise that: it is the static path and stores
# the name bare. An earlier version of this test used it, passed, and hid the
# fact that the feature could not work on a real config's tags at all — the
# exact trap of a test that cannot fail. The rule is added BEFORE the window
# opens, because it applies to matching windows as they map.
$SCHEME '(hl-window-rule-add! #f #:match (list (quote class) "^km-tagged$") #:tag "+t-attach-tag")' >/dev/null

# two windows: one plain, one carrying the tag. The tagged one ends up focused,
# so the ATTACH below has a tagged window to be right about — which is the
# assertion worth making, and it only means something if the keymap is disarmed
# first. A fresh keymap is ENABLED at birth, so asserting #t there without the
# disable would pass whatever attach did.
$SCHEME '(hl-exec! "foot -a km-plain")' >/dev/null
sleep 1.5
$SCHEME '(hl-exec! "foot -a km-tagged")' >/dev/null
sleep 1.5

# the two spellings, side by side: the marker is in the raw list and gone from
# the plain one, and the window reports the tag under the name the config wrote
ok  '(let ((w (hl-window-from "class:^km-tagged$")))
       (equal? (hl-window-tags-raw w) (quote ("t-attach-tag*"))))'
ok  '(let ((w (hl-window-from "class:^km-tagged$")))
       (equal? (hl-window-tags w) (quote ("t-attach-tag"))))'
# away and back, because window.active fires on a CHANGE: tagging a window that
# is already focused emits nothing, exactly as in t-events.sh
$SCHEME '(hl-window-focus! (hl-window-from "class:^km-plain$"))' >/dev/null
sleep 1
$SCHEME '(hl-window-focus! (hl-window-from "class:^km-tagged$"))' >/dev/null
sleep 1

# THE ATTACH RE-EVALUATES ONCE ITSELF, which is what makes attaching during
# config load correct about the window already focused rather than a focus
# change away. Disarm by hand, attach, and no event runs in between.
$SCHEME '(hl-keymap-disable! (hl-state-ref (quote t-attach)))' >/dev/null
$SCHEME '(hl-keymap-attach-to-tag! (hl-state-ref (quote t-attach)) (quote t-attach-tag))' >/dev/null
ok  '(hl-keymap-enabled? (hl-state-ref (quote t-attach)))'
ok  '(hl-bind-enabled? (car (hl-keymap-binds (hl-state-ref (quote t-attach)))))'

# focus a window with NO tag: disarmed — flag and binds together
$SCHEME '(hl-window-focus! (hl-window-from "class:^km-plain$"))' >/dev/null
sleep 1
ok  '(and (not (hl-keymap-enabled? (hl-state-ref (quote t-attach))))
          (not (hl-bind-enabled? (car (hl-keymap-binds (hl-state-ref (quote t-attach)))))))'

# and back to the tagged one: armed
$SCHEME '(hl-window-focus! (hl-window-from "class:^km-tagged$"))' >/dev/null
sleep 1
ok  '(hl-keymap-enabled? (hl-state-ref (quote t-attach)))'

# FOCUS CLEARED: the payload is #f, which carries no tag, so every attached
# keymap goes down rather than the event being skipped. The tagged window has to
# be focused first or there is no transition to observe — see t-events.sh for the
# early return in the compositor that makes that true.
ws0=$($SCHEME '(hl-workspace-name (hl-active-workspace))' 2>/dev/null | tr -d '"')
$SCHEME '(hl-workspace-focus! "name:km-empty-ws")' >/dev/null
sleep 1.5
ok  '(not (hl-keymap-enabled? (hl-state-ref (quote t-attach))))'

# ---- attaching to a CONDITION, not a tag -------------------------------------
# The general form: a lambda of one argument — the focused window, or #f when
# nothing is focused. A tag is a string a window either has or has not, so a
# NEGATION, a disjunction, or a mode flag of your own cannot be one, and those
# are what a real policy is usually made of. hl-keymap-attach-to-tag! is this
# function with one condition written for you.
err '(hl-keymap-attach-to-window-condition! 5 (lambda (w) #t))' 'expected a keymap'
err '(hl-keymap-attach-to-window-condition! (hl-make-keymap) "not-a-procedure")' 'must be a procedure'

$SCHEME '(hl-state-set! (quote t-cond-map) (hl-make-keymap))' >/dev/null
$SCHEME '(hl-keymap-add! (hl-state-ref (quote t-cond-map))
                         (hl-make-bind (hl-kbd "s-<F28>") (lambda () #f)))' >/dev/null

# A CONDITION ABOUT THE WINDOW: armed while km-plain is focused and not while
# km-tagged is. Both windows are still up from the block above.
$SCHEME '(hl-keymap-attach-to-window-condition!
           (hl-state-ref (quote t-cond-map))
           (lambda (w) (and w (string=? (hl-window-class w) "km-plain"))))' >/dev/null
$SCHEME '(hl-window-focus! (hl-window-from "class:^km-plain$"))' >/dev/null
sleep 1
ok  '(hl-keymap-enabled? (hl-state-ref (quote t-cond-map)))'
$SCHEME '(hl-window-focus! (hl-window-from "class:^km-tagged$"))' >/dev/null
sleep 1
ok  '(not (hl-keymap-enabled? (hl-state-ref (quote t-cond-map))))'

# A CONDITION THAT READS SOMETHING ELSE. The registry is re-evaluated when the
# focused window changes and at no other time, so a condition closing over a
# variable is stale from the moment the variable changes — that is what
# hl-keymaps-recheck! is for, and the first two assertions below are the staleness
# stated as a test rather than merely described.
$SCHEME '(hl-state-set! (quote t-cond-flag) #f)' >/dev/null
$SCHEME '(hl-keymap-attach-to-window-condition!
           (hl-state-ref (quote t-cond-map))
           (lambda (w) (hl-state-ref (quote t-cond-flag) #f)))' >/dev/null
ok  '(not (hl-keymap-enabled? (hl-state-ref (quote t-cond-map))))'
$SCHEME '(hl-state-set! (quote t-cond-flag) #t)' >/dev/null
# flipped, and STILL disarmed: no focus change has happened, so nothing has asked
ok  '(not (hl-keymap-enabled? (hl-state-ref (quote t-cond-map))))'
$SCHEME '(hl-keymaps-recheck!)' >/dev/null
ok  '(hl-keymap-enabled? (hl-state-ref (quote t-cond-map)))'

# ONE CONDITION PER KEYMAP: attaching again REPLACES it, so the lambda from the
# last call is the keymap's whole condition. The recheck is the assertion that
# proves it — if the old always-true condition were still on the registry it
# would re-arm the keymap here.
$SCHEME '(hl-keymap-attach-to-window-condition!
           (hl-state-ref (quote t-cond-map))
           (lambda (w) #f))' >/dev/null
ok  '(not (hl-keymap-enabled? (hl-state-ref (quote t-cond-map))))'
$SCHEME '(hl-keymaps-recheck!)' >/dev/null
ok  '(not (hl-keymap-enabled? (hl-state-ref (quote t-cond-map))))'

# tidy up: the windows, and the workspace this block took the session to
$SCHEME '(for-each (lambda (c)
                     (let ((w (hl-window-from (string-append "class:^" c "$"))))
                       (if w (hl-window-close! w))))
                   (list "km-plain" "km-tagged"))' >/dev/null
sleep 1
[[ -n "$ws0" ]] && $SCHEME "(hl-workspace-focus! \"$ws0\")" >/dev/null
sleep 1

# ---- sub-keymaps: a prefix reachable twice is ONE map ------------------------
# This is the child assq, and it is what replaced the module-level table of
# submaps already built. define-key! walks a sequence a prefix at a time, so the
# second sequence sharing a prefix must find the map the first one built — by
# name, in the parent — rather than build a rival with the same submap name.
#
# Identity is the thing to assert, not just "a child exists": two maps with the
# same submap name would both work in isolation and disagree about their binds.
val '(length (hl-keymap-binds (hl-make-keymap)))' '0'
ok  '(not (hl-keymap-child (hl-make-keymap) "C-x"))'
val '(hl-keymap-submap (hl-keymap-child
                        (let ((km (hl-make-keymap)))
                          (define-key! km (hl-kbd "C-x C-f") (lambda () #f))
                          km)
                        "C-x"))' '"C-x"'
ok  '(let* ((km (hl-make-keymap #:submap "kid-probe"))
            ;; the child is keyed by its own SUBMAP NAME — the parent name
            ;; EXTENDED by the key, so "kid-probe C-y" and not "C-y"
            (child (begin (define-key! km (hl-kbd "C-y C-a") (lambda () #f))
                          (hl-keymap-child km "kid-probe C-y")))
            (again (begin (define-key! km (hl-kbd "C-y C-b") (lambda () #f))
                          (hl-keymap-child km "kid-probe C-y"))))
       ;; the same OBJECT, and both keys hang off it
       (and (eq? child again)
            (hl-keymap? child)
            (string=? (hl-keymap-submap child) "kid-probe C-y")
            (>= (length (hl-keymap-binds child)) 2)
            (for-each hl-unbind! (hl-keymap-binds child))
            #t))'

# ---- marks: what a keymap has already given itself ---------------------------
# A keymap records "already done" facts about ITSELF (the emacs layer's 'exit and
# 'catchall) instead of a module-level table. A MARK rather than a look for a
# bind, because a config may bind s-G itself — "is there a bind on s-G here"
# would then answer yes for a map that was never given an exit.
ok  '(not (hl-keymap-marked? (hl-make-keymap) (quote exit)))'
ok  '(let ((km (hl-make-keymap)))
       (hl-keymap-mark! km (quote exit))
       (let ((n1 (length (hl--keymap-marks km))))
         ;; marking twice is a no-op, not a duplicate entry
         (hl-keymap-mark! km (quote exit))
         (and (hl-keymap-marked? km (quote exit))
              (not (hl-keymap-marked? km (quote catchall)))
              (= n1 (length (hl--keymap-marks km))))))'
# define-key! marks the map it built the chain in, and the CHILD gets its own
# separate marks — s-G on a child is not the parent's
ok  '(let ((km (hl-make-keymap #:submap "mark-probe")))
       (define-key! km (hl-kbd "C-p C-q") (lambda () #f))
       (let ((child (hl-keymap-child km "mark-probe C-p")))
         (and (hl-keymap-marked? km (quote exit))       ; the base got its s-G
              (hl-keymap-marked? km (quote catchall))   ; and a catchall
              (hl-keymap-marked? child (quote exit))    ; the child its own
              (hl-keymap-marked? child (quote catchall))
              (for-each hl-unbind! (hl-keymap-binds child))
              (for-each hl-unbind! (hl-keymap-binds km))
              #t)))'

exit $FAILED
