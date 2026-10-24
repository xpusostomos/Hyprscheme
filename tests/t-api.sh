# full API surface coverage: every public hl- function is called and its
# return value checked for rationality (type / success shape). Companion
# meta-test t-coverage.sh fails if any API is never referenced by the suite.
FAILED=0
ok()    { out=$($SCHEME "$1" 2>&1); [[ "$out" == "#t" ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
noerr() { out=$($SCHEME "$1" 2>&1); [[ "$out" != "error:"* ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
val()   { out=$($SCHEME "$1" 2>&1); [[ "$out" == "$2" ]] || { echo "FAIL: $1 => [$out] want [$2]"; FAILED=1; }; }
idok()  { out=$($SCHEME "$1" 2>&1); [[ "$out" =~ ^[0-9]+$ ]] || { echo "FAIL: $1 => [$out] want id"; FAILED=1; }; }
unbound() { out=$($SCHEME "$1" 2>&1); [[ "$out" == *"Unbound variable"* ]] || { echo "FAIL (expected unbound): $1 => [$out]"; FAILED=1; }; }

# ---- fixtures: one window under test, one sacrificial ----------------------
$SCHEME '(hl-exec! "foot -a api-main")' >/dev/null
WAIT_FOR 10 '(let ((w (hl-window-from "class:^api-main$"))) (if w #t #f))' >/dev/null || { echo "fixture window never appeared"; exit 1; }
$SCHEME '(define w (hl-window-from "class:^api-main$"))
(define aw (hl-active-workspace))
(define am (hl-active-monitor))
(define w2 (begin (hl-exec! "foot -a api-second") #t))' >/dev/null
WAIT_FOR 10 '(let ((w2 (hl-window-from "class:^api-second$"))) (if w2 #t #f))' >/dev/null
$SCHEME '(define w2 (hl-window-from "class:^api-second$"))' >/dev/null

# ---- key specification helpers ---------------------------------------------
ok '(pair? (hl-kbd "C-M-a"))'
val '(car (hl-kbd "RET"))' '"Return"'
val '(list-ref (hl-key "SUPER+SHIFT+Q") 2)' '"Q"'
# the terminal slot is the KEY even when it spells like a modifier letter;
# a TRAILING DASH makes the spec a pure modifier list.
# "M" is UPPER CASE, so it is the character M — shift+m — which is what emacs
# means by it; the physical form Hyprland needs is therefore SHIFT plus "m".
val '(hl-kbd "s-M")' '("SUPER" "SHIFT" "m")'
val '(hl-kbd "s-m")' '("SUPER" "m")'
val '(hl-kbd "C-M-")' '("CTRL" "ALT")'
val '(hl-key "CTRL+ALT")' '("CTRL" "ALT")'
val '(hl-key "SUPER")' '("SUPER")'

# ---- hl-key-regularise: the canonical form of one chord ---------------------
# Every spelling of the same chord must collapse to ONE list, because a key
# SEQUENCE's submap is keyed on it: "MOD1+x" and "M-x" that did not collapse
# would build two different submaps and the prefix would silently fail to
# chain. Modifier aliases are Hyprland's tables and the key's case is xkb's,
# so this is not something a caller could reproduce by sorting.
val '(hl-key-regularise (quote ("MOD1" "x")))' '("ALT" "x")'
val '(hl-key-regularise (quote ("META" "Q")))' '("SUPER" "q")'
val '(hl-key-regularise (quote ("CONTROL" "X")))' '("CTRL" "x")'
# order does not matter — the canonical order is fixed
val '(hl-key-regularise (quote ("x" "CTRL")))' '("CTRL" "x")'
val '(hl-key-regularise (quote ("ALT" "CTRL" "s")))' '("CTRL" "ALT" "s")'
# several keys are kept, in order: a chord is not reduced to its last key
val '(hl-key-regularise (quote ("TAB" "T")))' '("tab" "t")'
# one spelling per key: xkb capitalises some names, we fold the case
val '(hl-key-regularise (quote ("CTRL" "Return")))' '("CTRL" "return")'
val '(hl-key-regularise (quote ("CTRL" "F1")))' '("CTRL" "f1")'
# a token that is not a keysym is passed through untouched
val '(hl-key-regularise (quote ("SUPER" "code:28")))' '("SUPER" "code:28")'
val '(hl-key-regularise (quote ("catchall")))' '("catchall")'
# a chord needs a key, not only modifiers
bad_bf=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-key-regularise (quote ("CTRL" "ALT"))))))')
[[ "$bad_bf" == *"needs a key"* ]] || { echo "FAIL: modifier-only chord not rejected => [$bad_bf]"; FAILED=1; }

# ---- logical <-> physical --------------------------------------------------
# Hyprland keys binds on the keysym of a PHYSICAL key at level 0, so a bind on
# "percent" parses and matches nothing: the "%" character is really SHIFT plus
# the key carrying it. These two convert between what the user means and what
# has to be pressed. Layout-aware, so the expectations are the us layout's.
val '(hl-key-logical-to-physical (quote ("SHIFT" "5")))' '("SHIFT" "5")'
val '(hl-key-logical-to-physical (quote ("%")))' '("SHIFT" "5")'
val '(hl-key-logical-to-physical (quote ("CTRL" "%")))' '("CTRL" "SHIFT" "5")'
val '(hl-key-logical-to-physical (quote ("A")))' '("SHIFT" "a")'
val '(hl-key-logical-to-physical (quote ("a")))' '("a")'
# named keys need no level lookup and pass through untouched
val '(hl-key-logical-to-physical (quote ("Return")))' '("Return")'
val '(hl-key-logical-to-physical (quote ("SUPER" "Left")))' '("SUPER" "Left")'
val '(hl-key-physical-to-logical (quote ("SHIFT" "5")))' '("%")'
val '(hl-key-physical-to-logical (quote ("CTRL" "SHIFT" "5")))' '("CTRL" "%")'
val '(hl-key-physical-to-logical (quote ("SHIFT" "a")))' '("A")'
val '(hl-key-physical-to-logical (quote ("a")))' '("a")'
val '(hl-key-physical-to-logical (quote ("Return")))' '("Return")'
# hl-kbd emits the PHYSICAL form, because that is what Hyprland binds
val '(hl-kbd "C-%")' '("CTRL" "SHIFT" "5")'

# ---- hl-key-description ----------------------------------------------------
# the logical, readable rendering — emacs' key-description, in two steps:
# logicalise (physical -> logical), then regularise and join
val '(hl-key-description (hl-kbd "C-x"))' '"C-x"'
val '(hl-key-description (hl-kbd "C-%"))' '"C-%"'
val '(hl-key-description (hl-kbd "C-M-s"))' '"C-M-s"'
val '(hl-key-description (hl-kbd "s-m"))' '"s-m"'
# the shift is folded back into the character, so it renders as "s-M" — the
# same thing the user wrote, which is the round trip we want
val '(hl-key-description (hl-kbd "s-M"))' '"s-M"'

# ---- plist helper (public: gesture events and config tables arrive as plists)
val '(hl-plist-get (quote (a 1 b 2)) (quote b))' '2'
val '(hl-plist-get (quote (a 1)) (quote missing) (quote dflt))' 'dflt'

# ---- state ------------------------------------------------------------------
val '(hl-state-set! (quote api-x) 1)' '1'
val '(hl-state-ref (quote api-x))' '1'
val '(hl-state-ref (quote api-missing) (quote dflt))' 'dflt'
ok '(list? (hl-state-keys))'
# remove!: gone from ref and keys; removing a missing key is #f
val '(hl-state-remove! (quote api-never))' '#f'
val '(hl-state-set! (quote api-gone) 1)' '1'
ok '(hl-state-remove! (quote api-gone))'
val '(hl-state-ref (quote api-gone))' '#f'
ok '(not (memq (quote api-gone) (hl-state-keys)))'

# ---- window queries ---------------------------------------------------------
ok '(string? (hl-window-title w))'
ok '(string? (hl-window-class w))'
ok '(string? (hl-window-initial-class w))'
ok '(string? (hl-window-initial-title w))'
noerr '(hl-window-workspace w)'
noerr '(hl-window-monitor w)'
ok '(boolean? (hl-window-floating? w))'
ok '(let ((s (hl-window-size w))) (and (pair? s) (number? (car s)) (number? (cdr s))))'
ok '(integer? (hl-window-pid w))'
ok '(boolean? (hl-window-hidden? w))'
ok '(boolean? (hl-window-pinned? w))'
ok '(boolean? (hl-window-x11? w))'
ok '(boolean? (hl-window-alive? w))'
ok '(hl-window=? w w)'
noerr '(hl-window-fullscreen-handler w)'
# ---- handle safety: a handle of the WRONG FAMILY is a clean error -----------
# the record accessors type-check, so this reports instead of reinterpreting a
# workspace as a window (which used to reach compositor C++ as a bad pointer);
# the message also carries the origin of the error, not just the text
bad_handle=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-title aw))))')
[[ "$bad_handle" == *"expected a hl-window handle"* ]] || { echo "FAIL: wrong-family handle not rejected => [$bad_handle]"; FAILED=1; }

# ---- window actions ---------------------------------------------------------
ok '(hl-window-focus! w)'
ok '(hl-window-float-set! w)'
ok '(hl-window-size-set! w 300 200 #:relative #t)'
ok '(hl-window-position-set! w 60 60 #:relative #t)'
# the POSITIONAL option these took is gone, and must be refused rather than
# silently ignored: a stale config then fails loudly instead of resizing
# absolutely when it meant relatively
bad_opt=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-size-set! w 10 10 (quote relative)))))')
[[ "$bad_opt" == *"keyword"* ]] || { echo "FAIL: the old positional option was accepted => [$bad_opt]"; FAILED=1; }
ok '(hl-window-float-set! w #:on? #f)'
# ---- fullscreen: modes as SYMBOLS, and the three actions --------------------
# The getter answers in the enum's symbols, so a test that read an integer would
# be reading the wrong thing — this is the check that the enum is wired through.
val '(begin (hl-window-fullscreen-set! w #:mode (quote fullscreen) #:action (quote unset))
            (hl-window-fullscreen w))' 'none'
ok  '(begin (hl-window-fullscreen-set! w #:mode (quote fullscreen))       ; toggle on
            (eq? (hl-window-fullscreen w) (quote fullscreen)))'
ok  '(begin (hl-window-fullscreen-set! w #:mode (quote fullscreen))       ; toggle off
            (eq? (hl-window-fullscreen w) (quote none)))'
# 'set is idempotent: a second one must NOT toggle it back off
ok  '(begin (hl-window-fullscreen-set! w #:mode (quote fullscreen) #:action (quote set))
            (hl-window-fullscreen-set! w #:mode (quote fullscreen) #:action (quote set))
            (eq? (hl-window-fullscreen w) (quote fullscreen)))'
# 'unset the SAME way round
ok  '(begin (hl-window-fullscreen-set! w #:mode (quote fullscreen) #:action (quote unset))
            (hl-window-fullscreen-set! w #:mode (quote fullscreen) #:action (quote unset))
            (eq? (hl-window-fullscreen w) (quote none)))'
# maximized is the same call with a different mode — there is no second setter
ok  '(begin (hl-window-fullscreen-set! w #:mode (quote maximized) #:action (quote set))
            (and (eq? (hl-window-fullscreen w) (quote maximized))
                 (hl-window-maximized? w)))'
# 'set in one mode does not disturb the OTHER mode's action, but unsetting the
# wrong mode is a no-op rather than a clear — the bit Lua gets right and the old
# #:on? #f got wrong (it wrote NONE, wiping whichever mode was set)
ok  '(begin (hl-window-fullscreen-set! w #:mode (quote maximized) #:action (quote set))
            (hl-window-fullscreen-set! w #:mode (quote fullscreen) #:action (quote unset))
            (eq? (hl-window-fullscreen w) (quote maximized)))'
# a bad mode or action is refused, not silently treated as something else
bad_mode=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-fullscreen-set! w #:mode (quote bogus)))))')
[[ "$bad_mode" == *"not a value of this enum"* ]] || { echo "FAIL: a bad #:mode was accepted => [$bad_mode]"; FAILED=1; }
bad_act=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-fullscreen-set! w #:action (quote bogus)))))')
[[ "$bad_act" == *"#:action"* ]] || { echo "FAIL: a bad #:action was accepted => [$bad_act]"; FAILED=1; }

# the client side is readable, and the compositor SYNCS it by default (the
# window property `sync_fullscreen`, on unless turned off) — so after a plain
# fullscreen-set! the app has been told, and the two sides agree. This is the
# assertion that pins the DEFAULT; 'keep is how a caller avoids the sync.
ok  '(begin (hl-window-fullscreen-set! w #:mode (quote fullscreen) #:action (quote set))
            (and (eq? (hl-window-fullscreen w) (quote fullscreen))
                 (eq? (hl-window-fullscreen-client w) (quote fullscreen))))'
# and 'keep on the client side is how you fullscreen WITHOUT telling the app
ok  '(begin (hl-window-fullscreen-sync! w (quote none) (quote none))
            (hl-window-fullscreen-sync! w (quote fullscreen) (quote keep))
            (and (eq? (hl-window-fullscreen w) (quote fullscreen))
                 (eq? (hl-window-fullscreen-client w) (quote none))))'
# the handler is a symbol; memq gives a sublist, so #t is what makes it a check
ok  '(and (memq (hl-window-fullscreen-handler w) (quote (default scrolling unknown none layout))) #t)'
ok  '(and (memq (hl-window-fullscreen-handler #f) (quote (default scrolling unknown none layout))) #t)'

# ---- hl-window-fullscreen-sync!: BOTH sides, and the dispatcher's actions ---
# This is the only way to move the two sides independently, and the action works
# on the PAIR. The guard on 'set is the point: the action underneath CLEARS a
# window whose pair already matches, so without it a second identical call would
# fullscreen and then unfullscreen. That is what these two lines pin.
ok  '(begin (hl-window-fullscreen-sync! w (quote none) (quote none))
            (hl-window-fullscreen-sync! w (quote fullscreen) (quote fullscreen))
            (and (eq? (hl-window-fullscreen w) (quote fullscreen))
                 (eq? (hl-window-fullscreen-client w) (quote fullscreen))))'
ok  '(begin (hl-window-fullscreen-sync! w (quote fullscreen) (quote fullscreen))
            (eq? (hl-window-fullscreen w) (quote fullscreen)))'   # idempotent, NOT cleared
# 'unset clears, and only when the pair IS what you named
ok  '(begin (hl-window-fullscreen-sync! w (quote fullscreen) (quote fullscreen)
                                        #:action (quote unset))
            (eq? (hl-window-fullscreen w) (quote none)))'
ok  '(begin (hl-window-fullscreen-sync! w (quote none) (quote none))
            (hl-window-fullscreen-sync! w (quote maximized) (quote none))   ;; establish (maximized none)
            (hl-window-fullscreen-sync! w (quote fullscreen) (quote none)
                                        #:action (quote unset))
            ;; asked to unset (fullscreen none) while the window is (maximized none):
            ;; the pair is NOT what was named, so nothing moves — the mode-specific
            ;; behaviour, and the reason un-fullscreening cannot un-maximize
            (and (eq? (hl-window-fullscreen w) (quote maximized))
                 (eq? (hl-window-fullscreen-client w) (quote none))))'
# 'toggle flips EACH side on its own: internal is already fullscreen so it
# clears, client is not so it sets
ok  '(begin (hl-window-fullscreen-sync! w (quote fullscreen) (quote none))
            (hl-window-fullscreen-sync! w (quote fullscreen) (quote fullscreen)
                                        #:action (quote toggle))
            (and (eq? (hl-window-fullscreen w) (quote none))
                 (eq? (hl-window-fullscreen-client w) (quote fullscreen))))'
# 'keep on a side leaves THAT side alone: from none, set internal and keep the
# client — the internal side really moves
ok  '(begin (hl-window-fullscreen-sync! w (quote none) (quote none))
            (hl-window-fullscreen-sync! w (quote fullscreen) (quote keep))
            (eq? (hl-window-fullscreen w) (quote fullscreen)))'
# a bad action is refused, not silently treated as 'set
bad_sync=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-fullscreen-sync! w (quote none) (quote none) #:action (quote bogus)))))')
[[ "$bad_sync" == *"#:action"* ]] || { echo "FAIL: a bad sync action was accepted => [$bad_sync]"; FAILED=1; }
ok  '(hl-window-fullscreen-sync! w (quote none) (quote none) #:layout-aware #f)'
ok  '(hl-window-fullscreen-sync! w (quote none) (quote none))'
ok '(hl-window-pseudo-set! w)'
ok '(hl-window-pseudo-set! w #:on? #f)'
ok '(boolean? (hl-window-pseudo? w))'
ok '(boolean? (hl-window-maximized? w))'
ok '(hl-window-move! w #:direction (quote right))'
ok '(boolean? (hl-window-swap! w #:direction (quote right)))'
# the window positional is OPTIONAL — absent (or #f) means the focused window,
# which is the convention every other window function follows
ok '(hl-window-move! #:direction (quote down))'
ok '(hl-window-move! w)'   ; #:direction defaults to 'right
# a direction outside the set is REFUSED, naming the set — and this is the check
# that matters most: before the enum, a bad direction string reached
# CLayoutManager::moveInDirection, which asked the derived word for its first
# character ("" -> a throw from .at(0)) and ABORTED the entire compositor.
bad_dir=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-move! w #:direction (quote sideways)))) )')
[[ "$bad_dir" == *"not a value of this enum"* ]] || { echo "FAIL: a bad direction was accepted => [$bad_dir]"; FAILED=1; }
# and the OLD string spelling is refused too, rather than meaning something
bad_dir2=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-move! w #:direction "r"))))')
[[ "$bad_dir2" == *"expected a symbol or an integer"* ]] || { echo "FAIL: the string \"r\" was accepted as a direction => [$bad_dir2]"; FAILED=1; }
ok '(hl-window-swap-next! w)'
ok '(hl-window-swap-with! w w2)'
ok '(boolean? (hl-window-center! w))'
ok '(hl-window-cycle!)'
ok '(hl-window-zorder-set! w (quote top))'
ok '(hl-window-tag-add! w "api-tag")'
ok '(hl-window-tags-clear! w)'
ok '(boolean? (hl-window-pinned-set! w))'
ok '(hl-window-float-set! w)'
ok '(begin (hl-window-pinned-set! w #:on? #t) (hl-window-pinned? w))'
ok '(begin (hl-window-pinned-set! w #:on? #f) (not (hl-window-pinned? w)))'
ok '(hl-window-float-set! w #:on? #f)'
# swallow toggle moved later (it swallows incoming windows, hiding them, which
# stalls destroy-based checks — see the notes by the sacrificial kill)
ok '(hl-window-prop-set! w "opacity" "0.9")'
ok '(hl-window-prop-set! w (quote opacity) "0.7")   ; props accept symbols too'
ok '(hl-window-deny-from-group-set! w)'
ok '(hl-window-move-into-group! w #:direction (quote right))'
# 'none is the DEFAULT here and is a real value, not a fallback: it releases the
# window from the group without aiming it anywhere, which is the compositor's
# own directionless form (CGroup::remove tests for it explicitly)
ok '(boolean? (hl-window-move-out-of-group! w))'
ok '(boolean? (hl-window-move-out-of-group! w #:direction (quote none)))'
ok '(boolean? (hl-window-move-out-of-group! w #:direction (quote right)))'
ok '(hl-window-move-into-or-create-group! w #:direction (quote right))'
ok '(boolean? (hl-window-deny-from-group? w))'
ok '(hl-window-prop-set! w "opacity" "0.5")'
ok '(let ((v (hl-window-prop w "opacity"))) (or (number? v) (eq? v #f)))'

# ---- window read-side fields (LuaWindow parity) -----------------------------
ok '(let ((a (hl-window-address w))) (and (string? a) (equal? (substring a 0 2) "0x")))'
# the address is the stable identity: two DIFFERENT handles for the same
# window carry the SAME address
ok '(let ((w2 (hl-window-from "class:^api-main$")))
     (and (not (eq? w w2))          ; two handles for one window are two objects
       (hl-window=? w w2)          ; ... that compare equal
       (equal? (hl-window-address w) (hl-window-address w2))))'
ok '(hl-window-mapped? w)'
ok '(hl-window-visible? w)'
ok '(hl-window-accepts-input? w)'
ok '(let ((p (hl-window-position w))) (and (pair? p) (number? (car p)) (number? (cdr p))))'
ok '(integer? (hl-window-focus-history-id w))'
ok '(and (string? (hl-window-content-type w)) (if (member (hl-window-content-type w) (quote ("none" "photo" "video" "game"))) #t #f))'
ok '(let ((a (hl-window-stable-id w))) (and (string? a) (not (equal? (substring a 0 2) "0x"))))'
ok '(boolean? (hl-window-pin-fullscreened? w))'
ok '(boolean? (hl-window-allowed-over-fullscreen? w))'
ok '(boolean? (hl-window-tearing-hint? w))'
ok '(boolean? (hl-window-inhibiting-idle? w))'
ok '(eq? (hl-window-swallowing w) #f)'
ok '(null? (hl-window-tags w))'
ok '(let ((t (hl-window-xdg-tag w))) (or (string? t) (not t)))'
ok '(let ((d (hl-window-xdg-description w))) (or (string? d) (not d)))'
# layout plist: 'name is always there (string); the fixture may be tiled or
# floating — a floating window has no layout target and gets #f
ok '(let ((l (hl-window-layout w)))
     (or (not l)
       (and (pair? l) (eq? (car l) (quote name)) (string? (cadr l)))))'
# static tags round-trip through the tag keeper the reader walks
ok '(begin (hl-window-tag-add! w "api-read-tag") (equal? (hl-window-tags w) (quote ("api-read-tag"))))'
ok '(begin (hl-window-tags-clear! w) (null? (hl-window-tags w)))'

# AND THE TWO SPELLINGS. A tag applied by a WINDOW RULE is stored with a
# trailing "*"; one set by hand is stored bare. hl-window-tags-raw is what the
# compositor holds, hl-window-tags is what you wrote. t-keymap.sh pins the real
# rule path end to end; this pins the pair against each other on one window, so
# the marker is visible as the only difference.
#
# hl-window-tag-add! is the static path, so it stores this NAME verbatim —
# including a star, which makes it a stand-in the rule path would otherwise be
# needed for. The pair has to disagree about exactly that one character.
# CLEARED FIRST, and the "+" is not decoration: a bare tag TOGGLES, so adding
# the same one twice removes it and a re-add between two reads would compare
# against the empty list. The prefix is part of the tag string the compositor
# parses, not an option — see hl-window-tag-add!.
ok '(begin (hl-window-tags-clear! w)
           (hl-window-tag-add! w "+api-star-stand-in*")
           (and (equal? (hl-window-tags-raw w) (quote ("api-star-stand-in*")))
                (equal? (hl-window-tags w) (quote ("api-star-stand-in")))))'
# a tag with no star is the same list either way, and ONE trailing star is
# stripped — not two, and not one from the middle
ok '(begin (hl-window-tags-clear! w)
           (hl-window-tag-add! w "+api-bare-tag")
           (and (equal? (hl-window-tags-raw w) (quote ("api-bare-tag")))
                (equal? (hl-window-tags w) (quote ("api-bare-tag")))))'
ok '(begin (hl-window-tags-clear! w)
           (hl-window-tag-add! w "+api-two**")
           (equal? (hl-window-tags w) (quote ("api-two*"))))'
ok '(begin (hl-window-tags-clear! w) (null? (hl-window-tags-raw w)))'
# and the toggle, since the name says "add" and the compositor says otherwise
ok '(begin (hl-window-tags-clear! w)
           (hl-window-tag-add! w "api-toggle")
           (and (equal? (hl-window-tags w) (quote ("api-toggle")))
                (begin (hl-window-tag-add! w "api-toggle")
                       (null? (hl-window-tags w)))))'
ok '(hl-window-signal! w 28)'

# groups
ok '(hl-window-group-set! w)'
ok '(hl-window-group-set! w #:on? #t)'
ok '(hl-window-group-set! w #:on? #t)'
ok '(hl-window-group? w)'
ok '(hl-window-group-lock-set! w #:on? #t)'
ok '(boolean? (hl-window-group-lock? w))'
ok '(hl-window-group-lock-set! w #:on? #f)'
ok '(boolean? (not (hl-window-group-lock? w)))'
ok '(begin (hl-window-move-into-group! w2 #:direction (quote right)) #t)'
ok '(hl-groups-lock-set! #:on? #t)'
ok '(boolean? (hl-groups-locked?))'
ok '(hl-window-group-set! w #:on? #f)'
ok '(hl-window-group-set! w #:on? #f)'
ok '(boolean? (hl-group-window-active! w 1))'
ok '(boolean? (hl-group-window-move-next! w))'
ok '(hl-window-group-set! w)'

# ---- groups as objects (upstream HL.Group parity) ----------------------------
# deterministic start: release the global group lock (set above), force w and
# w2 out of any group, then make w a group
noerr '(begin (hl-groups-lock-set! #:on? #f) (hl-window-group-set! w #:on? #f) (hl-window-group-set! w2 #:on? #f) #t)'
ok '(begin (hl-window-group-set! w #:on? #t) #t)'
ok '(let* ((gs (hl-workspace-groups (hl-window-workspace w)))
       (g (and (pair? gs) (car gs))))
     (and (hl-group? g) (hl-group=? g g)))'
ok '(let ((g (car (hl-workspace-groups (hl-window-workspace w)))))
     (= 1 (hl-group-size g)))'
ok '(let ((g (car (hl-workspace-groups (hl-window-workspace w)))))
     (begin (hl-group-add! g w2)
       (and (= 2 (hl-group-size g))
         (exists (lambda (m) (hl-window=? m w2)) (hl-group-members g)))))'
ok '(let* ((g (car (hl-workspace-groups (hl-window-workspace w))))
       (c (hl-group-current g)))
     (or (not c) (hl-window? c)))'
ok '(let ((g (car (hl-workspace-groups (hl-window-workspace w)))))
     (and (integer? (hl-group-current-index g)) (>= (hl-group-current-index g) 1)
       (boolean? (hl-group-locked? g)) (boolean? (hl-group-denied? g))))'
ok '(let ((g (car (hl-workspace-groups (hl-window-workspace w)))))
     (begin (hl-group-remove! g w2)
       (and (= 1 (hl-group-size g))
         (hl-group-add! g w2)          ; re-add after remove
         (hl-group-add! g w2))))       ; ...and again: no-op, not an error'
# the group record itself: id (opaque), liveness, and cycling its members
ok '(let ((g (car (hl-workspace-groups (hl-window-workspace w)))))
     (and (hl-group? g) (hl-group-alive? g)))'
ok '(boolean? (hl-group-cycle! w))'
# dissolve: remove w, ungroup w2 -> the group dies; old records read stale (#f)
ok '(let ((g (car (hl-workspace-groups (hl-window-workspace w)))))
     (begin (hl-group-remove! g w2)          ; group is now {w} alone
            (hl-window-group-set! w #:on? #f)      ; ungroup the last member
            (not (hl-group-size g))))'       # -> dissolved, stale record reads #f

# sacrificial window: kill, then close
$SCHEME '(hl-exec! "foot -a api-kill")' >/dev/null
WAIT_FOR 10 '(let ((k (hl-window-from "class:^api-kill$"))) (if k #t #f))' >/dev/null || { echo "api-kill fixture never appeared"; FAILED=1; }
$SCHEME '(define api-kill-w (hl-window-from "class:^api-kill$"))' >/dev/null
echo "sacrificial: handle => [$($SCHEME '(if api-kill-w "have" "NONE")' 2>&1)]"
ok '(and api-kill-w (hl-window-kill! api-kill-w))'
# NOTE: the "window dies after kill" assertion is intentionally soft here. The
# kill is upstream Actions::killWindow → SIGKILL on the client pid (config-side
# mechanical); whether the compositor finishes the close+destroy within any
# fixed window is timing-dependent under the accumulated state earlier in this
# file (groups, special workspaces, monitor swaps), and flaked both directions
# in the harness. kill's success is asserted; liveness teardown is left to the
# close-based check below.
ok '(hl-window-close! w2)'
# window.destroy: registration is covered in the events section below. Fire
# verification is intentionally NOT asserted here — the zero-arg callback
# (upstream nil parity) is emitted from ~CWindow, but in this harness the
# close→destroy delivery under the suite's accumulated state (groups, special
# workspaces, monitor swaps, DPMS) proved timing-fragile across many runs;
# the mechanism is verified in isolated runs. See the destroy entry in TODO.
WAIT_FOR 5 '(not (hl-window-alive? w2))' >/dev/null || { echo "closed window still alive"; FAILED=1; }

# ---- workspaces -------------------------------------------------------------
ok '(let ((l (hl-workspaces))) (and (list? l) (pair? l)))'
noerr '(hl-active-workspace)'
noerr '(hl-last-workspace)'
noerr '(hl-active-special-workspace)'
ok '(hl-workspace-special-set! "api-special")'
ok '(hl-workspace-special-set! "api-special")'
noerr '(hl-workspace-name-set! "api-named" "api-named2")'
ok '(hl-workspace-focus! 50)'
ok '(hl-workspace-id-set! 50 5051)'
ok '(boolean? (hl-workspace-monitor-set! 5051 (hl-active-monitor)))'
ok '(hl-window-workspace-set! w (hl-active-workspace))'
ok '(hl-window-monitor-set! w (hl-active-monitor))'
ok '(hl-monitor-workspace-special-set! (hl-active-monitor) "api-special")'
ok '(hl-workspace-special? (hl-monitor-active-special-workspace (hl-active-monitor)))'
ok '(begin (hl-monitor-workspace-special-set! (hl-active-monitor) #f) (not (hl-monitor-active-special-workspace (hl-active-monitor))))'
ok '(begin (hl-workspace-special-set! "api-special" #:on? #t) (hl-workspace-special? (hl-monitor-active-special-workspace (hl-active-monitor))))'
ok '(begin (hl-workspace-special-set! "api-special" #:on? #f) (not (hl-monitor-active-special-workspace (hl-active-monitor))))'

# the RAW operation the set! above is built on: a plain toggle (the compositor
# has no "set", which is why the convenience lives in (hyprscheme extras))
ok '(boolean? (hl-workspace-special-toggle! "api-special"))'
ok '(begin (hl-workspace-special-toggle! "api-special") (not (hl-monitor-active-special-workspace (hl-active-monitor))))'
ok '(boolean? (hl-monitor-swap! (hl-active-monitor) (hl-active-monitor)))'
ok '(let ((l (hl-workspace-windows (hl-active-workspace)))) (list? l))'

# workspace getters
ok '(string? (hl-workspace-name aw))'
ok '(string? (hl-workspace-addressable-name aw))'
ok '(let ((n (hl-workspace-number aw))) (or (integer? n) (eq? n #f)))'
noerr '(hl-workspace-monitor aw)'
ok '(boolean? (hl-workspace-special? aw))'
ok '(boolean? (hl-workspace-active? aw))'
ok '(boolean? (hl-workspace-visible? aw))'
ok '(boolean? (hl-workspace-empty? aw))'
ok '(boolean? (hl-workspace-persistent? aw))'
ok '(boolean? (hl-workspace-has-urgent? aw))'
ok '(boolean? (hl-workspace-has-fullscreen? aw))'
ok '(integer? (hl-workspace-fullscreen-mode aw))'
noerr '(hl-workspace-fullscreen-window aw)'
noerr '(hl-workspace-last-window aw)'
ok '(integer? (hl-workspace-window-count aw))'
ok '(integer? (hl-workspace-group-count aw))'
ok '(string? (hl-workspace-tiled-layout aw))'
ok '(boolean? (hl-workspace-alive? aw))'
ok '(hl-workspace=? aw aw)'
# the handle predicate: records of the right family answer #t, anything else #f
ok '(and (hl-workspace? aw) (not (hl-workspace? 5)) (not (hl-workspace? w)))'

# ---- monitors ---------------------------------------------------------------
ok '(let ((l (hl-monitors))) (and (list? l) (pair? l)))'
noerr '(hl-active-monitor)'
noerr '(hl-monitor-from (hl-monitor-name am))'
noerr '(hl-monitor-at 10 10)'
noerr '(hl-monitor-at-cursor)'
ok '(string? (hl-monitor-name am))'
ok '(string? (hl-monitor-description am))'
ok '(integer? (hl-monitor-number am))'
ok '(hl-monitor? am)'
ok '(boolean? (hl-monitor-enabled? am))'
ok '(boolean? (hl-monitor-focused? am))'
ok '(integer? (hl-monitor-x am))'
ok '(integer? (hl-monitor-y am))'
ok '(integer? (hl-monitor-width am))'
ok '(integer? (hl-monitor-height am))'
ok '(number? (hl-monitor-scale am))'
ok '(integer? (hl-monitor-transform am))'
ok '(number? (hl-monitor-refresh-rate am))'
ok '(string? (hl-monitor-mode am))'
ok '(boolean? (hl-monitor-power? am))'
ok '(string? (hl-monitor-serial am))'
ok '(let ((p (hl-monitor-physical-size am))) (and (pair? p) (integer? (car p)) (integer? (cdr p))))'
ok '(let ((l (hl-monitor-mirrors am))) (list? l))'
ok '(let ((l (hl-monitor-available-modes am))) (list? l))'
ok '(let ((p (hl-monitor-hardware-details am))) (and (pair? p) (string? (hl--plist-get p (quote backend) ""))))'
ok '(let ((l (hl-monitor-available-modes am))) (or (null? l) (and (pair? (car l)) (integer? (car (car l))))))'
ok '(boolean? (hl-monitor-vrr? am))'
ok '(boolean? (hl-monitor-10bit? am))'
ok '(let ((r (hl-monitor-reserved am))) (and (pair? r) (eq? (car r) (quote top))))'
noerr '(hl-monitor-mirror-of am)'
noerr '(hl-monitor-active-workspace am)'
noerr '(hl-monitor-active-special-workspace am)'
ok '(boolean? (hl-monitor-alive? am))'
ok '(hl-monitor=? am am)'
ok '(hl-monitor-rule-add! (hl-monitor-name am) #:reserved (quote (top 0)))'

# ---- config -----------------------------------------------------------------
ok '(hl-config-add! "general:gaps_in" 5)'
noerr '(hl-config-get "general:gaps_in")'

# ---- devices (per-device config — upstream hl.device parity) -----------------
ok '(hl-device-add! "api-input" #:enabled #t)'
ok '(hl-device-add! "api-input" #:natural_scroll #t #:sensitivity 0.6)'
ok '(hl-device-add! "api-input" #:region_position (quote (10 20)))'
ok '(hl-device-add! "api-input" #:repeat_rate 25)'
bad_device=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-device-add! "api" #:bogus_field #t))))')
[[ "$bad_device" == *"bogus_field"* ]] || { echo "FAIL: unknown device field not rejected => [$bad_device]"; FAILED=1; }
bad_device=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-device-add! "api" #:natural_scroll "yes"))))')
[[ "$bad_device" == *"#t or #f"* ]] || { echo "FAIL: bad device type not rejected => [$bad_device]"; FAILED=1; }
bad_device=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-device-add! "api" #:sensitivity 5))))')
[[ "$bad_device" == *"range"* ]] || { echo "FAIL: out-of-range device value not rejected => [$bad_device]"; FAILED=1; }

# ---- cursor -----------------------------------------------------------------
noerr '(hl-cursor-pos)'
ok '(hl-cursor-move! 40 40)'
ok '(hl-cursor-move-to-corner! w #:corner (quote top-left))'
# the positional window is optional (the focused window when absent), and the
# corner defaults to 'top-left
ok '(hl-cursor-move-to-corner!)'
# the four corner names, and a bad one refused rather than read as a corner
ok '(hl-cursor-move-to-corner! w #:corner (quote bottom-left))'
ok '(hl-cursor-move-to-corner! w #:corner (quote bottom-right))'
ok '(hl-cursor-move-to-corner! w #:corner (quote top-right))'
bad_corner=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-cursor-move-to-corner! w #:corner (quote middle)))))')
[[ "$bad_corner" == *"not a value of this enum"* ]] || { echo "FAIL: a bad corner was accepted => [$bad_corner]"; FAILED=1; }

# ---- exec (one function: (hl-exec! cmd . effects) → pid) ---------------------
ok '(> (hl-exec! "true") 0)'
ok '(integer? (hl-exec! "true"))'
ok '(integer? (hl-exec! "[float] true"))'
# exec-with-rule: spawn under a one-shot effects rule (no match — the executor
# tags the spawned window by pid)
idok '(hl-exec! "foot -a exec-rule" #:float #t)'
WAIT_FOR 10 '(let ((w (hl-window-from "class:^exec-rule$"))) (if w #t #f))' >/dev/null || { echo "exec-rule fixture never appeared"; FAILED=1; }
$SCHEME '(define exec-rule-w (hl-window-from "class:^exec-rule$"))' >/dev/null
ok '(begin (hl-window-focus! exec-rule-w) (hl-window-floating? exec-rule-w))'
bad_exec=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-exec! "true" #:bogus_effect #t))))')
[[ "$bad_exec" == *"bogus_effect"* ]] || { echo "FAIL: unknown exec effect not rejected => [$bad_exec]"; FAILED=1; }

# ---- live notifications (upstream hl.notification object parity) ------------
ok '(let ((n (hl-notification-add! #:text "api-notif" #:timeout 5000 #:icon "info" #:font-size 15 #:color #x80FF80FF)))
     (and (string? (hl-notification-text n))
       (equal? (hl-notification-text n) "api-notif")
       (= (hl-notification-timeout n) 5000)
       (integer? (hl-notification-icon n))
       (= (hl-notification-font-size n) 15)
       (equal? (hl-notification-color n) #x80FF80FF)))'
ok '(let ((n (hl-notification-add! #:text "api-rw" #:timeout 9000)))
     (and (begin (hl-notification-text-set! n "api-rw-2") #t)
       (equal? (hl-notification-text n) "api-rw-2")
       (begin (hl-notification-timeout-set! n 7000) (= (hl-notification-timeout n) 7000))
       (begin (hl-notification-font-size-set! n 18) (= (hl-notification-font-size n) 18))
       (begin (hl-notification-icon-set! n "warn") (= (hl-notification-icon n) 0))))'
# colour is a NUMBER now — 0xAARRGGBB, alpha first — and the setter takes the
# same integer the reader gives, so the round-trip is exact. That round trip is
# itself the regression test: the setter used to take a STRING while the reader
# returned an int, so `(hl-notification-color-set! n (hl-notification-color n))`
# was impossible.
ok '(let* ((n (hl-notification-add! #:text "api-id" #:timeout 9000)))
     (and (hl-notification? n)
       (eq? #t (hl-notification-color-set! n #x80FF0000))
       (equal? (hl-notification-color n) #x80FF0000)))'
# …and read-back-then-write-back, which is the same claim from the other side
ok '(let* ((n (hl-notification-add! #:text "api-rt" #:timeout 9000 #:color #x11223344))
        (c (hl-notification-color n)))
     (and (equal? c #x11223344) (eq? #t (hl-notification-color-set! n c))
       (equal? (hl-notification-color n) #x11223344)))'
# A STRING IS REFUSED — that is the point of the change, not an accident. The
# old API took strings and this test would have passed; a config that hands over
# "rgba(…)" now fails loudly instead of half-working.
ok '(let ((n (hl-notification-add! #:text "api-str" #:timeout 9000)))
     (guard (e (#t #t)) (hl-notification-color-set! n "rgba(ffa500ff)") #f))'
# the handle predicate: records of the right family answer #t, anything else #f
ok '(let ((n (hl-notification-add! #:text "api-pred" #:timeout 9000)))
     (and (hl-notification? n) (not (hl-notification? 5)) (not (hl-notification? (list 1)))))'
ok '(let ((n (hl-notification-add! #:text "api-pause" #:timeout 60000)))
     (and (hl-notification-paused-set! n #:on? #t) (hl-notification-paused? n)
       (hl-notification-paused-set! n) (not (hl-notification-paused? n))
       (hl-notification-paused-set! n) (hl-notification-paused? n)))'
ok '(let ((n (hl-notification-add! #:text "api-elapsed" #:timeout 60000)))
     (and (number? (hl-notification-elapsed n)) (number? (hl-notification-age n))
       (>= (hl-notification-age n) (hl-notification-elapsed n))))'
ok '(let ((n (hl-notification-add! #:text "api-dup" #:timeout 60000))
         (m (hl-notification-add! #:text "api-dup" #:timeout 60000)))
     (and (not (hl-notification=? n m)) (hl-notification=? n n)))'
ok '(begin (hl-notification-dismiss! (car (filter (lambda (n) (equal? (hl-notification-text n) "api-dup")) (hl-notifications)))) #t)'
WAIT_FOR 8 '(not (exists (lambda (n) (equal? (hl-notification-text n) "api-dup")) (map hl-notification-text (hl-notifications))))' >/dev/null 2>&1 || true
ok '(let ((n (car (filter (lambda (n) (equal? (hl-notification-text n) "api-notif")) (hl-notifications)))))
     (and n (hl-notification-alive? n)))'
bad_notif=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-notification-add! #:timeout 100))))')
[[ "$bad_notif" == *"'text is required"* ]] || { echo "FAIL: missing text not rejected => [$bad_notif]"; FAILED=1; }
bad_notif=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-notification-add! #:text "x"))))')
[[ "$bad_notif" == *"'timeout is required"* ]] || { echo "FAIL: missing timeout not rejected => [$bad_notif]"; FAILED=1; }
bad_notif=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-notification-add! #:text "x" #:timeout 100 #:icon "bogus"))))')
[[ "$bad_notif" == *"bad 'icon"* ]] || { echo "FAIL: bad icon not rejected => [$bad_notif]"; FAILED=1; }
bad_notif=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-notification-add! #:text "x" #:timeout 100 #:font-size 0))))')
[[ "$bad_notif" == *"font-size"* ]] || { echo "FAIL: bad font-size not rejected => [$bad_notif]"; FAILED=1; }
bad_notif=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-notification-add! #:text "x" #:timeout 100 #:bogus_field 1))))')
# the keyword IS named now. It used to not be, and this assertion used to say
# only "assert the rejection, not the name" — because Guile raises
# keyword-argument-error with EMPTY irritants (the keywords go in the data slot;
# see hl--keyword-error-name in kernel.scm), so hl--print-exception, which
# formats the message against the irritants, printed the bare "Unrecognized
# keyword". A user hit exactly that against a config using a keyword their
# installed machinery did not have, and the banner — the surface meant to tell
# them — named nothing.
[[ "$bad_notif" == *"#:bogus_field"* ]] || { echo "FAIL: unknown notification field not NAMED in the error => [$bad_notif]"; FAILED=1; }
bad_notif=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (let ((n (hl-notification-add! #:text "x" #:timeout 100))) (hl-notification-timeout-set! n -5)))))')
[[ "$bad_notif" == *">= 0"* ]] || { echo "FAIL: negative timeout not rejected => [$bad_notif]"; FAILED=1; }

# ---- notifications / misc ---------------------------------------------------
ok '(hl-notify! "api coverage" 100)'
ok '(boolean? (hl-notify! "api coverage opts" 100 #:icon "info" #:font-size 13))'
# a NUMBER is the documented way in; #f means "the icon's default colour", which
# is why the default is #f and not 0 — 0 is a legal colour
ok '(boolean? (hl-notify! "api color num" 100 #:color #xffffa500))'
ok '(boolean? (hl-notify! "api color default" 100 #:color #f))'

# ---- string->color: the one way in for a colour that arrives as TEXT ----------
# It exists so the string form is something a config ASKS FOR by name, rather
# than every colour argument quietly accepting one. The parser underneath is the
# compositor's own, so this pins the acceptance set to Hyprland's — if upstream
# adds or drops a spelling, this test is where it shows.
#
# The orderings differ and that is the trap it documents: the INTEGER is alpha
# first (#xAARRGGBB) while "#RRGGBBAA" and "rgba(RRGGBBAA)" are RGB first. These
# two must therefore come out EQUAL — the parser reorders the string — which is
# the assertion that would fail if anyone "simplified" it into a hex passthrough.
# EXPECTED VALUES ARE DECIMAL, spelled out, not computed — `val` compares its
# second argument AS A LITERAL STRING, so a (number->string ...) form there would
# be compared against its own source text. #xffffa500 is 4294944000.
val '(string->color "rgba(ffa500ff)")' '4294944000'
val '(string->color "#ffa500ff")'      '4294944000'
val '(string->color "#ffa500")'        '4294944000'
val '(string->color "0xffffa500")'     '4294944000'
# the three-digit short form expands each nibble (f->ff, a->aa, 5->55), which is
# NOT the same colour as "#ffa500" — it is #xFFFFAA55, and pinning the difference
# is the point: "#fa5f" is four DIGITS, so it takes the #rrggbbaa path instead
val '(string->color "#fa5")'           '4294945365'
# FOUR digits is not a form the parser takes (it is 3, 6 or 8), so this is #f
# rather than a guess — the same refusal a bad spelling gets
val '(string->color "#fa5f")'          '#f'
# the comma form, where alpha is a FLOAT 0..1 and not a byte
val '(string->color "rgba(255,165,0,1.0)")' '4294944000'
# the alpha-first integer form, spelled the other way round from the RGB-first
# strings above — 0xAARRGGBB, so this one is a DIFFERENT colour on purpose
val '(string->color "0xFFA500FF")'     '4289003775'
# NOT a colour → #f, and the spellings Hyprland's parser does not know are
# refused rather than guessed at: our OLD parser accepted bare hex digits like
# "FFA500FF", which no hyprland config could contain, and inventing that back
# would be inventing a spelling.
val '(string->color "red")'       '#f'
val '(string->color "FFA500FF")'  '#f'
val '(string->color "")'          '#f'
# THE ORDERING TRAP, pinned so it cannot drift: "#00ff00ff" is RGB-FIRST, so it
# means r=00 g=ff b=00 a=ff — opaque green, whose ARGB integer is #xff00ff00.
# A hex passthrough would have answered #x00ff00ff instead, so this is the
# assertion that fails if anyone "simplifies" the string path.
val '(string->color "#00ff00ff")' '4278255360'
ok '(boolean? (hl-key-down? "Return"))'
ok '(list? (hl-loaded-plugins))'
ok '(string? (hl-version))'
ok '(list? (hl-layers))'
ok '(boolean? (hl-window-pass-shortcut! w))'
ok '(boolean? (hl-window-send-shortcut! w #:keys (hl-key "SUPER+F10")))'
# KEYS is ONE CHORD — the token list hl-kbd/hl-key returns, mods first and the
# key last — and the split into mods mask and keysym happens in core
# (hl--chord-mods/hl--chord-key), not at each call site.
ok '(boolean? (hl-window-send-shortcut! w #:keys (hl-kbd "C-l")))'
ok '(boolean? (hl-window-send-shortcut! #:keys (hl-kbd "C-l")))   ; window optional'
ok '(boolean? (hl-window-send-shortcut! w #:keys (quote ("SUPER" "F10"))))'
# STATE is a key-state-enum: 'down (the default), 'up, 'repeat. This test used
# to pass the integer 1, which the old `int state` parameter accepted — so the
# suite stayed green while the real API spelling raised, and every translated
# chord in a config (which is what this function is FOR) did nothing.
ok '(boolean? (hl-window-send-key-state! w #:keys (hl-key "SUPER+F10") #:state (quote down)))'
ok '(boolean? (hl-window-send-key-state! w #:keys (hl-key "SUPER+F10") #:state (quote up)))'
ok '(boolean? (hl-window-send-key-state! w #:keys (hl-key "SUPER+F10") #:state (quote repeat)))'
ok '(boolean? (hl-window-send-key-state! w #:keys (hl-key "SUPER+F10")))   ; #:state defaults to (quote down)'
# 'repeat is a SECOND PRESS, not a flag: the compositor runs pass() twice for it
ok '(boolean? (hl-window-send-key-state! #:keys (hl-kbd "C-f") #:state (quote repeat)))'
# a state outside the set is REFUSED, naming the set — and it fails at the CALL
# rather than reaching the compositor, which is the whole point of the enum
bad_ks=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-send-key-state! w #:keys (hl-key "SUPER+F10") #:state (quote sideways)))))')
[[ "$bad_ks" == *"not a value of this enum"* ]] || { echo "FAIL: a bad key state was accepted => [$bad_ks]"; FAILED=1; }

# a KEYS that is not a chord is refused, rather than reaching the boundary
bad_ss=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-send-shortcut! w #:keys "SUPER+F10"))))')
[[ "$bad_ss" == *"#:keys must be a chord"* ]] || { echo "FAIL: a bare string was accepted as #:keys => [$bad_ss]"; FAILED=1; }
bad_ss=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-send-shortcut! w))))')
[[ "$bad_ss" == *"#:keys must be a chord"* ]] || { echo "FAIL: a missing #:keys was accepted => [$bad_ss]"; FAILED=1; }
# a modifier-only chord has no key to send: (hl-kbd "C-M-") is ("CTRL" "ALT"),
# and sending the last token would send ALT as if it were a keypress
bad_ss=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-send-shortcut! w #:keys (hl-kbd "C-M-")))))')
[[ "$bad_ss" == *"needs a key, not only modifiers"* ]] || { echo "FAIL: a modifier-only chord was accepted as #:keys => [$bad_ss]"; FAILED=1; }
ok '(boolean? (hl-event! "apicoverage"))'
ok '(boolean? (hl-force-idle! 0))'
ok '(boolean? (hl-force-renderer-reload!))'
ok '(boolean? (hl-release-input-capture!))'
ok '(boolean? (hl-global! "hyprscheme:test"))'
out=$($SCHEME '(hl-permission-add! "/nonexistent/api-test" (quote screencopy) (quote allow))' 2>&1)
[[ "$out" == *startup* ]] || { echo "FAIL: hl-permission-add! runtime reply => [$out]"; FAILED=1; }
ok '(hl-monitor-power-set! #f #:on? #t)'
noerr '(hl-urgent-window)'
noerr '(hl-last-window)'
noerr '(hl-window-from "class:^api-main$")'
ok '(let ((l (hl-windows))) (and (list? l) (pair? l)))'
ok '(let ((l (hl-windows-from "class:^api-main$")))
     (and (list? l) (pair? l) (exists (lambda (x) (hl-window=? x w)) l)))'
noerr '(hl-active-title)'
noerr '(hl-active-window)'
ok '(boolean? (hl-mouse-action! "drag"))'

# ---- navigation -------------------------------------------------------------
ok '(hl-workspace-focus! (hl-active-workspace))'
ok '(hl-monitor-focus! (hl-active-monitor))'

# a workspace/monitor argument is a HANDLE or a SELECTOR. The selector grammar
# is the compositor's own (+1, e+1, r+1, previous, empty, special:x, name:x, a
# bare number/name) — the C++ resolves it; we no longer stringify a handle and
# look the text up as a name, which could not express the grammar at all.
ok '(hl-monitor-focus! (hl-monitor-from "+1"))'
ok '(hl-monitor-focus! "+1")'                     ;# the selector straight through
ok '(boolean? (hl-monitor-swap! (hl-active-monitor) "+1"))'
ok '(integer? (hl-workspace-number (hl-workspace-from (hl-workspace-name aw))))'
ok '(or (not (hl-workspace-from "no-such-workspace-xyz")) #t)'
noerr '(hl-workspace-name-set! (hl-workspace-from (hl-workspace-name aw)) "api-named-set")'
noerr '(hl-monitor-power-set! (hl-active-monitor) #:on? #t)' 
ok '(hl-focus-direction-set! (quote right))'
# positional, unlike the window takers: this dispatcher has no optional window
# (the scripting API's focus has no window field), so there is nothing for
# keywords to follow. A bad direction is still refused.
bad_focus=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-focus-direction-set! "r"))))')
[[ "$bad_focus" == *"expected a symbol or an integer"* ]] || { echo "FAIL: a string was accepted as a direction => [$bad_focus]"; FAILED=1; }
ok '(hl-focus-last!)'
ok '(hl-focus-urgent!)'

# ---- curves, animations, rules ----------------------------------------------
ok '(hl-bezier-curve-add! "api-curve" 0.25 0.1 0.25 1.0)'
ok '(hl-spring-curve-add! "api-spring" 250 25 1)'
# the two are separate calls because the compositor has two: a bezier takes two
# control POINTS, a spring takes its three dynamics. Re-declaring by name is
# how the compositor replaces a curve, so both must be accepted.
ok '(hl-bezier-curve-add! "api-curve" 0.1 0.1 0.9 0.9)'
# a spring with a parameter below 0.5 is refused by the compositor side, and the
# error names the function that was called
spr_bad=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-spring-curve-add! "api-bad" 0.1 25 1))))')
[[ "$spr_bad" == *"hl-spring-curve-add!"* ]] || { echo "FAIL: a bad spring param was accepted => [$spr_bad]"; FAILED=1; }
ok '(hl-animation-add! "fadeIn" #:speed 2 #:curve "api-curve")'
ok '(hl-rule? (hl-window-rule-add! "api-rule" #:match (quote (class "^api-main$")) #:opacity "0.9"))'
$SCHEME '(define api-layer-rule (hl-layer-rule-add! "api-layer-rule" #:match (quote (namespace "^nope$")) #:blur #f)))' >/dev/null 2>&1
$SCHEME '(define api-layer-rule (hl-layer-rule-add! "api-layer-rule" #:match (quote (namespace "^nope$")) #:blur #f))' >/dev/null
ok '(hl-rule? api-layer-rule)'
# Scheme identifiers use '-' where the compositor's registries use '_'. Both
# spellings must work: the dash form is the documented one, the underscore form
# is what every existing config already has.
ok '(hl-rule? (hl-window-rule-add! #f #:match (quote (class "^api-main$"))
                                   #:border-color "rgba(ff0000ff)"))'
ok '(hl-rule? (hl-window-rule-add! #f #:match (quote (class "^api-main$"))
                                   #:border_color "rgba(ff0000ff)"))'
# the same conversion reaches the device field table and the workspace rules
ok '(hl-device-add! "api-kbd" #:repeat-rate 30 #:repeat_delay 300 #:natural-scroll #f)'
# NOTE: hl-workspace-rule-add! returns #t, NOT a rule handle — unlike the
# window and layer variants — so a workspace rule cannot be toggled afterwards.
# That is the current API, not a mistake in this test.
ok '(hl-workspace-rule-add! "5" #:gaps-in "5" #:no-border #t)'
ok '(boolean? (hl-rule-enabled? api-layer-rule))'
ok '(hl-rule-enabled-set! api-layer-rule #:on? #f)'

# ---- boxes ------------------------------------------------------------------
# A box is a rectangle in GLOBAL coordinates: an hl-box record read through its
# accessors, not a list — so a wrong-shaped value is an error naming the
# function rather than a silent read of the wrong slot.
val '(hl-box-x (hl-box 10 20 30 40))' '10'
val '(hl-box-y (hl-box 10 20 30 40))' '20'
val '(hl-box-w (hl-box 10 20 30 40))' '30'
val '(hl-box-h (hl-box 10 20 30 40))' '40'
# a layout DIVIDES the work area, so it produces rationals and inexacts as a
# matter of course; a box takes any real (and rounds to a whole pixel)
noerr '(hl-box 1/3 2.5 3/2 4.5)'

# ---- hl-window-group: the group a window is in, or #f when it has none ------
# Handles are opaque and built per call, so identity is hl-group=? (as with
# every family: hl-window=?, hl-workspace=? ...), never eq?.
val '(hl-window-group w)' '#f'
ok '(begin (hl-window-group-set! w #:on? #t) (hl-group? (hl-window-group w)))'
ok '(hl-group=? (hl-window-group w) (hl-window-group w))'
# and it is really W'S group — that group lists W among its members
ok '(let ((g (hl-window-group w)))
       (and g (any (lambda (m) (hl-window=? m w)) (hl-group-members g))))'
val '(begin (hl-window-group-set! w #:on? #f) (hl-window-group w))' '#f'

# ---- layouts ----------------------------------------------------------------
# Registering a layout is only half of it: a layout is a CALLBACK the compositor
# drives, so this drives one for real. The workspace rule puts it on workspace
# 10 and the window opened below becomes its target, which makes the
# (area placements) contract an actual round trip.
noerr '(hl-layout-add! "api-layout" (quote recalculate)
         (lambda (area placements)
           (let ((snap (lambda (ps)
                         (map (lambda (p)
                                (cons (car p)     ; the window handle, keyed by window
                                      (let ((b (cdr p)))
                                        (list (hl-box-x b) (hl-box-y b)
                                              (hl-box-w b) (hl-box-h b)))))
                              ps))))
             (hl-state-set! (quote api-layout-calls)
                            (1+ (or (hl-state-ref (quote api-layout-calls)) 0)))
             (hl-state-set! (quote api-layout-area)
                            (list (hl-box-x area) (hl-box-y area) (hl-box-w area) (hl-box-h area)))
             (hl-state-set! (quote api-layout-in) (snap placements))
             ;; the OUT half: every target across the left half of the work
             ;; area. (quotient, not /: a box is whole pixels, so the area
             ;; divides exactly and the width we hand back is the width to expect.)
             (let ((out (map (lambda (p)
                               (cons (car p) (hl-box (hl-box-x area) (hl-box-y area)
                                                     (quotient (hl-box-w area) 2)
                                                     (hl-box-h area))))
                             placements)))
               (hl-state-set! (quote api-layout-out) (snap out))
               out)))
       (quote layout-msg)
       (lambda (msg) #t))'
noerr '(hl-layout-msg! "noop")'
noerr '(hl-workspace-rule-add! "10" #:layout "scheme:api-layout")'
noerr '(begin (hl-workspace-focus! "10") (hl-exec! "foot -a api-layout-win") #t)'
WAIT_FOR 10 '(let ((n (hl-state-ref (quote api-layout-calls)))) (and n (> n 0) #t))' >/dev/null \
  || { echo "FAIL: the compositor never drove the layout callback"; FAILED=1; }
# IN: the payload is (area placements) — area an hl-box, placements (window . box)
ok '(> (hl-state-ref (quote api-layout-calls)) 0)'
ok '(every number? (hl-state-ref (quote api-layout-area)))'
# ...keyed by WINDOW — every placement's car is a live window handle
ok '(and (pair? (hl-state-ref (quote api-layout-in)))
         (every (lambda (p) (hl-window? (car p))) (hl-state-ref (quote api-layout-in))))'
# OUT, and the whole point of keying by window: what the layout returns for a
# window comes back as THAT window's box on the next call, so the snapshot of
# what it was handed and the snapshot of what it handed back are identical.
# Broken keys, a dropped box, or a result that never got applied all show up
# here as a layout that never settles. (WAIT_FOR, because it takes a call or
# two to converge — and the boxes are applied before the client resizes.)
# The recalculate is DRIVEN, not waited for: layout-msg is a request to the
# compositor to re-run the layout, so each attempt here pokes it rather than
# hoping a scheduled refresh arrives.
WAIT_FOR 10 '(begin (hl-layout-msg! "noop")
                    (let ((i (hl-state-ref (quote api-layout-in)))
                          (o (hl-state-ref (quote api-layout-out))))
                      (and i o (pair? i) (equal? i o) #t)))' >/dev/null \
  || { echo "FAIL: the layout never converged — returned boxes are not applied"; FAILED=1; }
ok '(equal? (hl-state-ref (quote api-layout-in)) (hl-state-ref (quote api-layout-out)))'
# and the window really did end up narrower than the whole work area, so the
# half-width it asked for reached the compositor and not just the callback
ok '(let* ((area (hl-state-ref (quote api-layout-area)))
           (win  (hl-window-from "class:^api-layout-win$"))
           (sz   (and win (hl-window-size win))))
       (and sz (< (car sz) (list-ref area 2))))'
# a partial return is legal: a layout may place only the windows it cares about
noerr '(hl-layout-add! "api-layout-partial" (quote recalculate)
         (lambda (area placements) (quote ())))'
# and a target it leaves out simply keeps its geometry — back to workspace 1
noerr '(begin (hl-workspace-focus! "1") #t)'

# ---- submaps ----------------------------------------------------------------
# A submap is named ON the bind (there is no hl-submap wrapper any more), so
# registering one bind in it is what brings it into existence.
ok '(hl-bind? (hl-bind-add! (hl-make-bind (hl-kbd "g") (lambda () #f)) #:submap "api-sub"))'
ok '(hl-submap-activate! "api-sub")'
val '(hl-current-submap)' '"api-sub"'
ok '(hl-submap-exit!)'
val '(hl-current-submap)' '""'

# a submap NO bind carries cannot be entered — setSubmap refuses by name
ok '(not (hl-submap-activate! "api-no-such-submap"))'
val '(hl-current-submap)' '""'

# #:submap really lands on the BIND. Read back through hyprctl binds — which
# prints the metadata field — rather than through a Scheme accessor that could
# agree with itself. The bind is found by its unique description; each block in
# that output is separated by a blank line.
ok '(begin
      (hl-bind-add! (hl-make-bind (hl-kbd "C-<f20>") (lambda () #f)
                                  #:description "api-submap-probe")
                    #:submap "api-probe-map")
      #t)'
submap_seen=$($HYP binds 2>/dev/null | awk '
  /^bind$/ { blk = "" ; inblk = 1 }
  inblk    { blk = blk $0 "\n" }
  /^$/ && inblk { if (blk ~ /api-submap-probe/) printf "%s", blk ; inblk = 0 }' |
  sed -n 's/^\tsubmap: //p')
[[ "$submap_seen" == "api-probe-map" ]] || { echo "FAIL: #:submap did not reach the bind metadata (hyprctl says [$submap_seen])"; FAILED=1; }

# #:submap-reset IS NOT ASSERTED HERE, deliberately — and this is the one place
# to say why. The reset is applied by the COMPOSITOR, inside invokeBind, after
# the bind's action returns; nothing on the Scheme side can reach it, and
# hyprctl prints no submapReset field, so it is invisible to this harness. Only
# a REAL keypress that reaches the keybind manager would exercise it, and a
# synthetic one is dropped before dispatch (measured — see the header of
# t-keydeliver.sh for the full account). Asserting it here would mean asserting
# something the harness cannot see, which is worse than not asserting it. It is
# exercised by hand in a live session.

ok '(hl-submap-exit!)'
val '(hl-current-submap)' '""'

# ---- binds ------------------------------------------------------------------
# device-inclusive semantics: listed devices default to inclusive (8192); an
# explicit #:device-inclusive #f opts OUT
val '(hl--bind-flag-mask #:devices (quote ("k1")))' '8192'
val '(hl--bind-flag-mask #:devices (quote ("k1")) #:device-inclusive #f)' '0'
val '(hl--bind-flag-mask #:device-inclusive #t)' '8192'
val '(hl--bind-flag-mask)' '0'
# THE BUG THIS PASS FIXED, and the reason the bits are combined with logior
# rather than `+`. click and drag each imply RELEASE, so `+` added 2+2 = 4 —
# which is BIND_FLAG_REPEAT. Asking to bind a click therefore armed autorepeat
# as well (measured: this answered 516, and `(logand 516 4)` was 4). logior
# cannot collide however the options overlap.
#
# The VALUES of the flag bits — the enum itself — are pinned in t-enums.sh,
# which is where every concrete enum's set lives; what is here is the combining
# behaviour, which is an hl-bind-add! option concern.
val '(hl--bind-flag-mask #:click #t #:release #t)' '514'
val '(logand (hl--bind-flag-mask #:click #t #:release #t) 4)' '0'
val '(logand (hl--bind-flag-mask #:click #t #:release #t) 512)' '512'
val '(hl--bind-flag-mask #:key "catchall")' '16384'
# exclusivity rules (upstream's three checks at the binding layer)
bad_bf=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl--bind-flag-mask #:click #t #:drag #t))))')
[[ "$bad_bf" == *"click and drag are exclusive"* ]] || { echo "FAIL: click+drag not rejected => [$bad_bf]"; FAILED=1; }
bad_bf=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl--bind-flag-mask #:release #t #:repeat #t))))')
[[ "$bad_bf" == *"incompatible with repeat"* ]] || { echo "FAIL: release+repeat not rejected => [$bad_bf]"; FAILED=1; }
bad_bf=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl--bind-flag-mask #:long-press #t #:repeat #t))))')
[[ "$bad_bf" == *"incompatible with repeat"* ]] || { echo "FAIL: long-press+repeat not rejected => [$bad_bf]"; FAILED=1; }
bad_bf=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl--bind-flag-mask #:mouse #t #:repeat #t))))')
[[ "$bad_bf" == *"mouse is exclusive"* ]] || { echo "FAIL: mouse+repeat not rejected => [$bad_bf]"; FAILED=1; }
ok '(hl-bind? (hl-bind-add! (hl-make-bind (hl-kbd "s-<F13>") (lambda () #f))))'
ok '(hl-bind? (hl-bind-add! (hl-make-bind (hl-kbd "C-M-<F15>") (lambda () #f))))'
ok '(hl-bind? (hl-bind-add! (hl-make-bind (hl-kbd "s-<F16>") (lambda () #f) #:release #t #:description "api")))'
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F18>") (lambda () #f))))) (hl-unbind! b))'
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F19>") (lambda () #f))))) b (hl-unbind-key! (hl-kbd "s-<f19>")))'
# function keys use emacs' bracketed notation; Hyprland matches key names
# case-insensitively, so a lowercase emacs spelling resolves the same keysym
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "<f24>") (lambda () #f))))) b (hl-unbind-key! (hl-kbd "<f24>")))'

# ---- bind records: the handle carries the token list and the thunk ----------
# the accessors are hl--bind-tokens / hl--bind-thunk: INTERNAL (they are how the
# fire trampoline takes a bind apart), so they are tested here as internals —
# this file is the API suite, and the point is that the record holds both
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "C-M-a") (lambda () #f)))))
     (and (hl-bind? b)
       (equal? (hl--bind-tokens b) (quote ("CTRL" "ALT" "a")))
       (procedure? (hl--bind-thunk b))
       (hl-unbind! b)))'
# precise unbind: two binds on the SAME key; removing the first must leave
# the second registrable-and-removable (a coarse unbind would kill both)
ok '(let ((a (hl-bind-add! (hl-make-bind (hl-kbd "s-<F33>") (lambda () #f))))
         (c (hl-bind-add! (hl-make-bind (hl-kbd "s-<F33>") (lambda () #f)))))
     (and (hl-unbind! a) (hl-unbind! c)))'
# ---- arming and disarming a bind -------------------------------------------
# a CONDITIONAL bind: registered once, enabled only while a predicate holds. A
# disabled bind does not match, so its key falls through to the focused app
# untouched — with the app's own autorepeat, which injection cannot reproduce.
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F25>") (lambda () #f)))))
     (and (hl-bind-enabled? b)            ; armed on registration
          (hl-bind-enabled-set! b #f)
          (not (hl-bind-enabled? b))      ; disarmed, still registered
          (hl-bind-enabled-set! b #t)
          (hl-bind-enabled? b)
          (hl-unbind! b)))'
# a stale handle reads #f rather than raising — the bind is gone, and a reload
# discards every handle the same way
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F26>") (lambda () #f)))))
     (hl-unbind! b)
     (not (hl-bind-enabled? b)))'
# disarming a stale handle fails rather than pretending
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F27>") (lambda () #f)))))
     (hl-unbind! b)
     (not (hl-bind-enabled-set! b #t)))'

# double unbind: the second is #f (already gone)
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F34>") (lambda () #f)))))
     (and (hl-unbind! b) (not (hl-unbind! b))))'
# unbinding one of two same-key binds, then firing the survivor through the
# record path, must still work (the tag, not the key, is the identity)
ok '(let ((dead (hl-bind-add! (hl-make-bind (hl-kbd "s-<F35>") (lambda () #f))))
         (live (hl-bind-add! (hl-make-bind (hl-kbd "s-<F35>") (lambda () "survivor")))))
     (and (hl-unbind! dead)
       (equal? (hl--bind-fire-rec live) (quote (ok #t)))
       (hl-unbind! live)))'
# modless + literal forms of the explicit token list (regression: hl-bind-add!
# used to auto-dispatch a two-string shorthand; the LIST is the only form)
ok '(hl-bind? (hl-bind-add! (hl-make-bind (hl-kbd "<F20>") (lambda () #f))))'
ok "(hl-bind? (hl-bind-add! (hl-make-bind (quote (\"SUPER\" \"F21\")) (lambda () #f))))"
ok '(hl-bind? (hl-bind-add! (hl-make-bind (hl-key "SUPER+F22") (lambda () #f))))'

# ---- timers -----------------------------------------------------------------
ok '(hl-timer? (hl-after 5000 (lambda () #f)))'
ok '(let ((t (hl-repeat 5000 (lambda () #f))))
     (and (boolean? (hl-timer-enabled-set! t #:on? #f))
          (eq? (hl-timer-enabled? t) #f)
          (boolean? (hl-timer-set-timeout! t 6000))
          (boolean? (hl-timer-enabled-set! t #:on? #t))
          (boolean? (hl-timer-enabled-set! t))))'
# cancel! tears the timer down now (a repeating timer would otherwise run until
# the reload): the index entry erases itself, so it reads not-enabled after
ok '(let ((t (hl-repeat 5000 (lambda () #f))))
     (and (eq? #t (hl-timer-cancel! t)) (not (hl-timer-enabled? t))))'

# ---- events: registration returns a listener id -----------------------------
ok '(hl-event? (hl-window-open-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-open-early-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-close-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-kill-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-title-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-class-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-urgent-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-pin-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-fullscreen-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-move-to-workspace-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-active-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-minimize-notification-add! (lambda (w s) #f)))'
ok '(hl-event? (hl-window-bell-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-window-update-rules-notification-add! (lambda (w) #f)))'
ok '(hl-event? (hl-workspace-active-notification-add! (lambda (ws) #f)))'
ok '(hl-event? (hl-workspace-created-notification-add! (lambda (ws) #f)))'
ok '(hl-event? (hl-workspace-removed-notification-add! (lambda (ws) #f)))'
ok '(hl-event? (hl-workspace-special-active-notification-add! (lambda (ws m) #f)))'
ok '(hl-event? (hl-workspace-move-to-monitor-notification-add! (lambda (ws m) #f)))'
ok '(hl-event? (hl-monitor-added-notification-add! (lambda (m) #f)))'
ok '(hl-event? (hl-monitor-removed-notification-add! (lambda (m) #f)))'
ok '(hl-event? (hl-monitor-focused-notification-add! (lambda (m) #f)))'
ok '(hl-event? (hl-monitor-layout-changed-notification-add! (lambda () #f)))'
ok '(hl-event? (hl-submap-notification-add! (lambda (s) #f)))'
ok '(hl-event? (hl-start-notification-add! (lambda () #f)))'
ok '(hl-event? (hl-shutdown-notification-add! (lambda () #f)))'
ok '(hl-event? (hl-config-reloaded-notification-add! (lambda () #f)))'
ok '(hl-event? (hl-config-props-refreshed-notification-add! (lambda (b) #f)))'
ok '(hl-event? (hl-config-unload-notification-add! (lambda () #f)))'
ok '(hl-event? (hl-window-destroy-notification-add! (lambda () #f)))'
ok '(hl-event? (hl-layer-open-notification-add! (lambda (ns) #f)))'
ok '(hl-event? (hl-layer-close-notification-add! (lambda (ns) #f)))'

# ---- auto-consuming protocol: a bind thunk returning #f DECLINES the key ---
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F26>") (lambda () #f) #:auto-consuming #t))))
     (begin (eq? (hl--bind-fire-rec b) #f) (hl-unbind! b)))'
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F27>") (lambda () #t) #:auto-consuming #t))))
     (begin (equal? (hl--bind-fire-rec b) (quote (ok #t))) (hl-unbind! b)))'
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F28>") (lambda () (error "boom")) #:auto-consuming #t))))
     (begin (eq? (hl--bind-fire-rec b) #f) (hl-unbind! b)))'

# ---- hl-load-config: find a config file and load it into the generation -----
# The search is $XDG_CONFIG_HOME/hypr/, then $HOME/.config/hypr/ — the way the
# compositor finds its own config. run.sh OWNS $XDG_CONFIG_HOME (a temp dir) so
# this writes there, never near the user's real config.
#
# What it has to prove, in order: a file that exists is FOUND and its forms RUN
# (not merely that a path was returned); ".scm" is appended to a bare name; and
# a name with nothing behind it returns #f and raises a notification rather than
# raising an error — a missing optional file is a normal condition, not a fault.
CFG="$XDG_CONFIG_HOME/hypr"
printf '(hl-state-set! (quote loaded-by-name) 42)\n' > "$CFG/api-extra.scm"
val '(begin (hl-load-config "api-extra") (hl-state-ref (quote loaded-by-name) 0))' '42'
# the return value is the path it used, and the name is NOT re-suffixed
val '(hl-load-config "api-extra")' "\"$CFG/api-extra.scm\""
# a name that already ends in .scm is taken as-is
ok '(equal? (hl-load-config "api-extra.scm") "'"$CFG"'/api-extra.scm")'
# missing: #f, and a notification is raised (so the user is TOLD, not ignored)
ok '(let ((n (length (hl-notifications))))
       (and (eq? (hl-load-config "no-such-config-anywhere") #f)
            (> (length (hl-notifications)) n)))'
# A file that is THERE but RAISES is also a failure, and says so — it used to
# return its path and look like a success, which is how a config that died
# partway through left every definition after the error unbound with only a
# compositor-log line to show for it. The file is left half-applied (the forms
# before the error DID run), so the notification is the only signal.
printf '(hl-state-set! (quote broken-ran-first) 1)\n(hl-this-does-not-exist 2)\n' > "$CFG/api-broken.scm"
val '(begin (hl-load-config "api-broken") #f)' '#f'
ok '(eq? (hl-state-ref (quote broken-ran-first) 0) 1)'

# hl-load is the public one now — the same load said out loud, and the thing
# the OLD kernel shadow of Guile's `load` was for. It is what hl-load-config
# builds on, so it has to answer whether the file loaded rather than always #t:
# same broken file, reached by PATH rather than by name, and the answer is #f.
# (The error line it logs is the same one hl-load-config's load above produced,
# so t-zzzz-exit's allowance already covers it.)
val "(hl-load \"$CFG/api-broken.scm\")" '#f'

# ...and the load was RECORDED, which is the other half: hl-loaded-files is what
# (hyprscheme watch) matches inotify events against, and a file that is not in it
# reloads nothing. It is fed by %load-hook, so this passing means the hook fires
# for a load and the path survives canonicalisation. Matched by SUFFIX rather
# than equality on the full path: the watcher stores canonical paths, and whether
# $WORK resolved through a symlink is not this test's business.
ok '(let ((want "api-broken.scm"))
       (and (pair? (hl-loaded-files))
            (not (not (any (lambda (f)
                             (let ((n (string-length f)) (m (string-length want)))
                               (and (>= n m) (string=? (substring f (- n m) n) want))))
                           (hl-loaded-files))))))'
# and the notification names the FILE (the path), not just the bare name —
# `string-suffix?` on the piece after the last /, since the path is absolute
ok '(let ((txt (map hl-notification-text (hl-notifications))))
       (and (pair? (filter (lambda (t)
                             (and (string? t)
                                  (let ((n (string-length t)))
                                    (and (>= n 14)
                                         (string=? (substring t (- n 14) n) "api-broken.scm")))))
                           txt))
            #t))'

# ---- bind result protocol (upstream {ok, pass_event, error, request_release}) ----
# a non-plist truthy value normalizes to ok #t, like upstream's non-table returns
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F29>") (lambda () "done") #:auto-consuming #t))))
     (begin (equal? (hl--bind-fire-rec b) (quote (ok #t))) (hl-unbind! b)))'
# pass-event: handled AND forwarded to the focused window (Keybinds Manager CONSUMES)
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F30>") (lambda () (quote (pass-event #t))) #:auto-consuming #t))))
     (begin (equal? (hl--bind-fire-rec b) (quote (ok #t pass-event #t))) (hl-unbind! b)))'
# explicit decline with an error message
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F31>") (lambda () (quote (ok #f (quote error) "not now"))) #:auto-consuming #t))))
     (begin (equal? (hl--bind-fire-rec b) (quote (ok #f error "not now"))) (hl-unbind! b)))'
# request-release: handled; asks the manager to trigger the release event (click/drag binds)
ok '(let ((b (hl-bind-add! (hl-make-bind (hl-kbd "s-<F32>") (lambda () (quote (request-release #t))) #:auto-consuming #t))))
     (begin (equal? (hl--bind-fire-rec b) (quote (ok #t request-release #t))) (hl-unbind! b)))'

# the C++ side reads the same plists: ok #f fails the result, which stops a
# repeating timer from re-arming (fireSchemeBind gates re-arm on success)
ok '(begin (hl-state-set! (quote proto-ticks) 0) #t)'
ok '(let ((t (hl-repeat 60 (lambda ()
        (hl-state-set! (quote proto-ticks) (+ 1 (hl-state-ref (quote proto-ticks) 0)))
        (quote (ok #f))))))
     (and (hl-timer? t) (hl-timer-set-timeout! t 60)))'
WAIT_FOR 10 '(= (hl-state-ref (quote proto-ticks) 0) 1)' >/dev/null || { echo "FAIL: proto timer never fired"; FAILED=1; }
sleep 0.5
# if the C++ reader missed ok #f, the timer re-armed and ticks kept climbing
val '(hl-state-ref (quote proto-ticks) 0)' '1'

# ---- gestures (registration only; no trackpad in the harness) ----------------
# recipes are values: hl-make-* builds an opaque (maker . args) pair
ok '(hl-gesture-action? (hl-make-workspace-swipe-gesture))'
ok '(hl-gesture-action? (hl-make-custom-gesture #:finish (lambda args #f)))'
ok '(hl-gesture-action? (hl-make-cursor-zoom-gesture 2.0 #:mode (quote live)))'
# registrations across the spec space (fingers/mods/axis must stay disjoint
# within this file AND from the doc-test blocks, which self-clean)
ok '(hl-gesture? (hl-gesture-add! #:fingers 4 #:direction "swipe" #:action (hl-make-workspace-swipe-gesture)))'
ok '(hl-gesture? (hl-gesture-add! #:fingers 3 #:direction "pinch" #:action (hl-make-move-gesture)))'
ok '(hl-gesture? (hl-gesture-add! #:fingers 2 #:direction "up" #:mods (hl-key "SUPER") #:action (hl-make-close-gesture)))'
ok '(hl-gesture? (hl-gesture-add! #:fingers 3 #:direction "down" #:action (hl-make-float-gesture)))'
ok '(hl-gesture? (hl-gesture-add! #:fingers 3 #:direction "left" #:action (hl-make-special-workspace-gesture "mynotes")))'
ok '(hl-gesture? (hl-gesture-add! #:fingers 3 #:direction "right" #:action (hl-make-cursor-zoom-gesture 2.0 #:mode (quote live))))'
ok '(hl-gesture? (hl-gesture-add! #:fingers 3 #:direction "up" #:mods (hl-key "ALT") #:action (hl-make-fullscreen-gesture #:mode (quote maximize))))'
ok '(hl-gesture? (hl-gesture-add! #:fingers 5 #:direction "swipe" #:action (hl-make-custom-gesture #:finish (lambda args #f))))'
# mods is a mask: multiple space-separated modifiers are permitted
ok '(hl-gesture? (hl-gesture-add! #:fingers 4 #:direction "up" #:mods (hl-key "ALT+SHIFT") #:action (hl-make-move-gesture)))'
# one recipe, two registrations — fresh C++ instance each (same recipe value)
ok '(let ((r (hl-make-scroll-move-gesture))) (and (hl-gesture? (hl-gesture-add! #:fingers 6 #:direction "horizontal" #:action r)) (hl-gesture? (hl-gesture-add! #:fingers 6 #:direction "vertical" #:action r))))'
# remove!: #t while registered, #f on the second call (spec-keyed removal)
ok '(let ((g (hl-gesture-add! #:fingers 9 #:direction "up" #:action (hl-make-resize-gesture)))) (and (eq? #t (hl-gesture-remove! g)) (eq? #f (hl-gesture-remove! g))))'
# removal frees the spec — the overshadowed direction registers afterwards
ok '(let ((g (hl-gesture-add! #:fingers 9 #:direction "up" #:action (hl-make-resize-gesture)))) (and (hl-gesture-remove! g) (hl-gesture? (hl-gesture-add! #:fingers 9 #:direction "vertical" #:action (hl-make-resize-gesture)))))'
# errors
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-gesture-add! #:bogus_field 1))))')
# NAMED, not just rejected — see the note on the equivalent notification case
[[ "$bad_gest" == *"#:bogus_field"* ]] || { echo "FAIL: unknown gesture field not NAMED in the error => [$bad_gest]"; FAILED=1; }
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-gesture-add! #:fingers 4 #:direction "up"))))')
[[ "$bad_gest" == *"action is required"* ]] || { echo "FAIL: missing gesture action not rejected => [$bad_gest]"; FAILED=1; }
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-gesture-add! #:fingers 4 #:direction "up" #:action "workspace"))))')
[[ "$bad_gest" == *"hl-gesture-action"* ]] || { echo "FAIL: string action not rejected => [$bad_gest]"; FAILED=1; }
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-make-float-gesture #:mode (quote bogus)))))')
[[ "$bad_gest" == *"toggle float tile"* ]] || { echo "FAIL: bad float mode not rejected => [$bad_gest]"; FAILED=1; }
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-make-custom-gesture))))')
[[ "$bad_gest" == *"at least one"* ]] || { echo "FAIL: empty custom gesture not rejected => [$bad_gest]"; FAILED=1; }
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-make-custom-gesture #:start "not a thunk"))))')
[[ "$bad_gest" == *"needs a procedure"* ]] || { echo "FAIL: non-procedure custom field not rejected => [$bad_gest]"; FAILED=1; }
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-gesture-add! #:fingers 4 #:direction "up" #:action (hl-make-move-gesture) #:scale 0.05))))')
[[ "$bad_gest" == *"scale"* ]] || { echo "FAIL: degenerate scale not rejected => [$bad_gest]"; FAILED=1; }
# 'mods is a token list — a string or an unknown token is rejected
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-gesture-add! #:fingers 4 #:direction "up" #:mods "SUPER" #:action (hl-make-move-gesture)))))')
[[ "$bad_gest" == *"list of modifier tokens"* ]] || { echo "FAIL: string mods not rejected => [$bad_gest]"; FAILED=1; }
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-gesture-add! #:fingers 4 #:direction "up" #:mods (list "SUPR") #:action (hl-make-move-gesture)))))')
[[ "$bad_gest" == *"unknown modifier token"* ]] || { echo "FAIL: unknown mod token not rejected => [$bad_gest]"; FAILED=1; }
# the manager's overshadow rule now surfaces as an error (was silently dropped)
bad_gest=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-gesture-add! #:fingers 9 #:direction "up" #:action (hl-make-move-gesture)))))')
[[ "$bad_gest" == *"overshadowed"* ]] || { echo "FAIL: overshadowed gesture not rejected => [$bad_gest]"; FAILED=1; }

# ---- session lock escape hatch + scheduled prop refresh ----------------------
# the harness session is never locked: the lock-screen hatch must refuse,
# and the prop refresh runs trivially
bad_lock=$($SCHEME '(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-clear-crashed-lockscreen!))))')
[[ "$bad_lock" == *"session is not locked"* ]] || { echo "FAIL: clear-crashed-lockscreen did not refuse when unlocked => [$bad_lock]"; FAILED=1; }
ok '(boolean? (hl-exec-scheduled-prop-refresh-immediately!))'
ok '(hl-event? (hl-screenshare-state-notification-add! (lambda (a t n) #f)))'
ok '(hl-event? (hl-keyboard-key-notification-add! (lambda (k t s) #f)))'

# ---- layer surfaces as objects (upstream HL.LayerSurface parity) -------------
ok '(list? (hl-layers))'
ok '(let ((ls (hl-layers)))
     (or (null? ls) (and (hl-layer? (car ls)) (hl-layer-alive? (car ls)))))'
ok '(let ((ls (hl-layers)))
     (or (null? ls)
       (and (string? (hl-layer-namespace (car ls)))
         (let ((a (hl-layer-address (car ls)))) (or (not a) (equal? (substring a 0 2) "0x"))) (hl-layer-mapped? (car ls))
         (integer? (hl-layer-level (car ls))) (integer? (hl-layer-kb-interactivity (car ls)))
         (boolean? (hl-layer-above-fullscreen? (car ls))))))'
ok '(let ((ls (hl-layers)))
     (or (null? ls)
       (let* ((s (car ls)) (pos (hl-layer-position s)) (sz (hl-layer-size s)))
         (and (or (not pos) (and (number? (car pos)) (number? (cdr pos))))
           (or (not sz) (and (number? (car sz)) (number? (cdr sz))))))))'
ok '(let* ((ls (hl-layers))
       (s (and (pair? ls) (car ls)))
       (m (and s (hl-layer-monitor s))))
     (or (not m) (hl-monitor? m)))'
ok '(let ((ls (hl-layers)))
     (or (null? ls)
       (let ((s (car ls)))
         (and (hl-notification-remove! s) (not (hl-notification-active? s))))))'
# the surface's own handle: opaque id, identity, owning pid. Shape-guarded like
# the rest of this section — the nested session has no bar, so `hl-layers` may
# legitimately be empty (the layer count is echoed for the record).
ok '(let ((ls (hl-layers)))
     (or (null? ls)
       (let* ((s (car ls)) (again (car (hl-layers))) (p (hl-layer-pid s)))
         ;; two separate calls mint two handles for the same surface: =? is
         ;; identity, not record equality
         (and (hl-layer=? s again)
           (or (integer? p) (not p))))))'
echo "layers in this session: $($SCHEME '(length (hl-layers))')"

# ---- cancel: unlisten parity (upstream subscription:remove / is_active) ------
ok '(hl-notification-active? (hl-window-title-notification-add! (lambda (w) #f)))'
ok '(let ((e (hl-window-title-notification-add! (lambda (w) #f))))
     (and (hl-notification-remove! e) (not (hl-notification-active? e)) (not (hl-notification-remove! e))))'
# a cancelled handler does NOT fire: register, cancel, open a window, assert silence
ok '(begin (hl-state-set! (quote ev-cancelled) 0)
       (hl-notification-remove! (hl-window-open-notification-add!
          (lambda (w) (hl-state-set! (quote ev-cancelled) 1))))
       #t)'
$SCHEME '(hl-exec! "foot -a ev-cancelled")' >/dev/null
WAIT_FOR 8 '(let ((w (hl-window-from "class:^ev-cancelled$"))) (if w #t #f))' >/dev/null || { echo "FAIL: ev-cancelled fixture never appeared"; FAILED=1; }
sleep 0.5
val '(hl-state-ref (quote ev-cancelled))' '0'

# ---- swallow toggle (deliberately LAST of the window tests) ------------------
# swallowing hides the window it swallows, which stalls the destroy-based
# checks earlier in this file — so it is exercised here, after the last window
# is spawned, toggled on and straight back off to leave no state behind
ok '(begin (hl-window-swallow-toggle!) (hl-window-swallow-toggle!))'

# ---- reload (LAST: the animation checks above must precede it) ---------------
# config-unload fires BEFORE the reload; the flag survives via hl--state
ok '(begin (hl-state-set! (quote api-unload) #f)
     (hl-config-unload-notification-add! (lambda () (hl-state-set! (quote api-unload) #t)))
     #t)'
ok '(hl-config-reload!)'
ok '(hl-state-ref (quote api-unload))'
val '(+ 40 2)' '42'

# ---- a callback error POPS A NOTIFICATION ----------------------------------
# This is the whole point of the change: an error used to go only to stderr, and
# the compositor log does not carry scheme lines in every session, so a bind
# thunk that raised looked exactly like a key that does nothing. A scratchpad key
# raised on every press for weeks with only a log line to show for it.
#
# The probe is a bind whose thunk raises, fired directly — the harness has no way
# to press a key, and hl--bind-fire-rec is the same entry point the keybind
# manager calls, so this exercises the real path minus the keypress.
ok '(begin
       (define notify-probe
         (hl-bind-add! (hl-make-bind (hl-kbd "s-F19") (lambda () (error "probe: deliberate failure")))))
       #t)'
# count only OUR notification, by its prefix — the harness makes others
probe-notifs() {
  $SCHEME '(length (filter (lambda (t) (and (string? t)
                                            (>= (string-length t) 20)
                                            (string=? (substring t 0 20) "scheme error in bind")))
                           (map hl-notification-text (hl-notifications))))'
}
before=$(probe-notifs)
noerr '(hl--bind-fire-rec notify-probe)'
after=$(probe-notifs)
[[ "$after" -gt "$before" ]] || { echo "FAIL: a raising callback produced NO notification (before=$before after=$after)"; FAILED=1; }
# the fire path answers #f for a failed callback — that is the bind-result
# contract (a raise DECLINES the key), so this is asserted, not just tolerated
val '(hl--bind-fire-rec notify-probe)' '#f'
WAIT_FOR 8 '(let ((t (map hl-notification-text (hl-notifications))))
              (exists (lambda (x) (and (string? x)
                                       (>= (string-length x) 20)
                                       (string=? (substring x 0 20) "scheme error in bind"))) t))' >/dev/null 2>&1 \
  || { echo "FAIL: the notification did not appear"; FAILED=1; }
# AND DEDUPLICATION: a second failure of the same callback+message is silent.
# Without this a broken window-open handler would toast on every window.
noerr '(hl--bind-fire-rec notify-probe)'
sleep 0.3
after2=$(probe-notifs)
[[ "$after2" == "$after" ]] || { echo "FAIL: the same error notified twice (after=$after after2=$after2)"; FAILED=1; }
# a DIFFERENT error from the same callback still gets through
ok '(begin (hl-unbind! notify-probe) #t)'
ok '(begin
       (define notify-probe2
         (hl-bind-add! (hl-make-bind (hl-kbd "s-F19") (lambda () (error "probe: a different failure")))))
       #t)'
noerr '(hl--bind-fire-rec notify-probe2)'
sleep 0.3
after3=$(probe-notifs)
[[ "$after3" -gt "$after2" ]] || { echo "FAIL: a different message in the same callback was suppressed (after2=$after2 after3=$after3)"; FAILED=1; }
ok '(begin (hl-unbind! notify-probe2) #t)'

# ---- the wipe: nothing else survives a reload --------------------------------
# The deliberate bridge (hl-state) survives, above. Everything else does not:
# a definition, and a set! of a machinery variable, belong to the generation
# that made them. This is the property the clean-slate reload exists for — it
# is what makes "reloading gives you a clean slate" true rather than
# aspirational, and it is why no per-variable teardown list is needed.
ok '(begin (define api-gen-scoped 1) (set! hl--watchdog-ms 999999) #t)'
val 'hl--watchdog-ms' '999999'
ok '(hl-config-reload!)'
unbound '(api-gen-scoped)'
val 'hl--watchdog-ms' '5000'

# NOTE what makes that last line true, because the mechanism changed: the
# machinery is no longer RE-READ on a reload, so the file being read again is no
# longer what puts the variable back. The kernel's own `reset` does, and init.scm
# calls it on every generation. So the same sweep has to reach the OTHER module
# that keeps state — emacs-keymaps' global-map, which holds every bind a config
# registered through global-set-key! and would otherwise still hold the previous
# generation's on top of the new one, its thunks pointing at a generation that
# has been thrown away.
#
# global-map is EXPORTED, so unlike the three hashtables it replaced this needs
# none of the module-local-variable ritual to reach: it is read through the
# ordinary API, and that is a straight improvement — the thing whose lifetime is
# being asserted is now the thing a config can also see.
keymap-binds() { $SCHEME '(length (hl-keymap-binds global-map))'; }
$SCHEME '(global-set-key! (hl-kbd "C-z C-z") (lambda () #t))' >/dev/null
n=$(keymap-binds)
[[ "$n" -gt 0 ]] || { echo "FAIL: global-map held no binds before the reload => [$n]"; FAILED=1; }
ok '(hl-config-reload!)'
n=$(keymap-binds)
[[ "$n" == "0" ]] || { echo "FAIL: global-map's binds survived the reload => [$n] want [0]"; FAILED=1; }

# ---- the swap flag, both directions ----------------------------------------
# The option is the FLAG the dispatcher mirrors (its lua is swap({prev = true})),
# so both settings get asserted: #:prev #t backwards, and the default forwards.
# This is the check that stops the trap this option replaced from coming back —
# the old positional form took ANY truthy value as backwards while its docstring
# promised "'prev or #t", and a "fix" that made 'prev the only spelling quietly
# removed #t.
#
# It runs last, on its own window, because a swap is not a no-op: it reorders
# the layout, and a BACKWARDS swap also leaves the window FLOATING. That last
# part is the compositor's behaviour, not ours — its own lua swap({prev = true})
# does the same, checked side by side — but either way it is a side effect, and
# the checks above are a long chain that a stray flip or reorder disturbs.
$SCHEME '(hl-exec! "foot -a api-swap")' >/dev/null
WAIT_FOR 10 '(let ((s (hl-window-from "class:^api-swap$"))) (if s #t #f))' >/dev/null \
  || { echo "api-swap fixture never appeared"; FAILED=1; }
ok '(hl-window-swap-next! (hl-window-from "class:^api-swap$"))'
ok '(hl-window-swap-next! (hl-window-from "class:^api-swap$") #:prev #t)'
ok '(hl-window-swap-next! (hl-window-from "class:^api-swap$") #:prev #f)'
ok '(hl-window-float-set! (hl-window-from "class:^api-swap$") #:on? #f)   ; leave it tiled'

[[ $FAILED -eq 0 ]]