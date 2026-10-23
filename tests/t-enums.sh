# The enum machinery: a NAME for a set of values.
#
# An <enum-values> carries an ass list of symbol -> integer AND an rnrs enum
# built from the same symbols. What is asserted here is the property the API
# depends on: enum-value takes EITHER spelling and always hands back the
# integer, and anything outside the set is an error rather than a value.
#
# The names are unprefixed on purpose (define-enum-values, enum-value, ...) —
# they are definitional machinery a config author writes against, like
# define-key!, not hl-* API calls.
FAILED=0
ok()  { out=$($SCHEME "$1" 2>&1); [[ "$out" == "#t" ]] || { echo "FAIL: $1 => [$out]"; FAILED=1; }; }
val() { out=$($SCHEME "$1" 2>&1); [[ "$out" == "$2" ]] || { echo "FAIL: $1 => [$out] want [$2]"; FAILED=1; }; }
err() { out=$($SCHEME "$1" 2>&1); [[ "$out" == *"$2"* ]] || { echo "FAIL (want error mentioning $2): $1 => [$out]"; FAILED=1; }; }

# ---- make-enum-values and the predicate -------------------------------------
ok  '(enum-values? (make-enum-values (quote ((a . 1) (b . 2)))))'
ok  '(not (enum-values? (quote ((a . 1)))))'
ok  '(not (enum-values? 5))'

# define-enum-values is sugar for the same thing
ok  '(begin (define-enum-values t-enum (quote ((low . 1) (high . 9))))
            (enum-values? t-enum))'

# ---- enum-value: symbol OR integer in, integer out --------------------------
ok  '(begin (define-enum-values t-enum (quote ((low . 1) (high . 9))))
            (and (= 9 (enum-value t-enum (quote high)))
                 (= 9 (enum-value t-enum 9))
                 (= 1 (enum-value t-enum (quote low)))
                 (= 1 (enum-value t-enum 1))))'
# a value may be arbitrary, not just 0..n — that is the point of the ass list
ok  '(begin (define-enum-values t-enum (quote ((neg . -7) (zero . 0))))
            (and (= -7 (enum-value t-enum (quote neg)))
                 (= 0 (enum-value t-enum (quote zero)))))'
# 0 is a VALUE, not falsiness: it must come back as 0, not #f
val '(begin (define-enum-values t-enum (quote ((zero . 0))))
            (enum-value t-enum (quote zero)))' '0'

# ---- and refuses what is not in the set -------------------------------------
err '(begin (define-enum-values t-enum (quote ((low . 1))))
            (enum-value t-enum (quote nope)))' "not a value of this enum"
err '(begin (define-enum-values t-enum (quote ((low . 1))))
            (enum-value t-enum 99))' "not a value of this enum"
err '(begin (define-enum-values t-enum (quote ((low . 1))))
            (enum-value t-enum "low"))' "expected a symbol or an integer"
# the error NAMES the set, and names it in the caller's own currency: a bad
# SYMBOL is answered with the symbols, a bad INTEGER with the integers
err '(begin (define-enum-values t-enum (quote ((low . 1) (high . 9))))
            (enum-value t-enum (quote nope)))' "(low high)"
err '(begin (define-enum-values t-enum (quote ((low . 1) (high . 9))))
            (enum-value t-enum 99))' "(1 9)"

# ---- enum-symbol: the inverse, and what a getter returns --------------------
ok  '(begin (define-enum-values t-enum (quote ((low . 1) (high . 9))))
            (and (eq? (enum-symbol t-enum 9) (quote high))
                 (eq? (enum-symbol t-enum 1) (quote low))))'
err '(begin (define-enum-values t-enum (quote ((low . 1))))
            (enum-symbol t-enum 99))' "not a value of this enum"

# ---- enum-values->enum: the rnrs enum behind it -----------------------------
# it IS an rnrs enum of the same members — compared by membership, since two
# independently built enum-sets are different objects and eq? would be #f
ok  '(begin (define-enum-values t-enum (quote ((low . 1) (high . 9))))
            (equal? (enum-set->list (enum-values->enum t-enum)) (quote (low high))))'
# the rnrs toolkit applies to it
ok  '(begin (define-enum-values t-enum (quote ((low . 1) (high . 9))))
            (and (enum-set-member? (quote high) (enum-values->enum t-enum))
                 (not (enum-set-member? (quote nope) (enum-values->enum t-enum)))
                 (equal? (enum-set->list (enum-values->enum t-enum)) (quote (low high)))))'
# and its indices are 0..n-1 in declaration order, INDEPENDENT of the ass values
val '(begin (define-enum-values t-enum (quote ((low . 1) (high . 9))))
            ((enum-set-indexer (enum-values->enum t-enum)) (quote high)))' '1'

# ---- an enum used ACROSS modules --------------------------------------------
# A concrete enum used by ONE family lives with that family (fullscreen-mode is
# in window.scm) and is not exported, so it needs no entry here: it is exercised
# through hl-window-fullscreen and friends in t-api.
#
# A concrete enum used by TWO families lives in (hyprscheme values) and MUST be
# exported for both to import it — which makes it reachable API, and this file
# is where its own values are pinned. The behaviour (that a function refuses a
# bad value) is asserted in t-api; what is asserted here is the SET: the names,
# and the integers the compositor actually wants.

# direction-enum: the four directions, at Math::eDirection's own numbers.
val '(enum-value direction-enum (quote up))'    '0'
val '(enum-value direction-enum (quote right))' '1'
val '(enum-value direction-enum (quote down))'  '2'
val '(enum-value direction-enum (quote left))'  '3'
ok  '(equal? (enum-set->list (enum-values->enum direction-enum))
             (quote (up right down left)))'
# 'none belongs to direction-or-none-enum and NOT to this one — the whole reason
# the two are separate sets: a function taking a direction must refuse it.
err '(enum-value direction-enum (quote none))' "not a value of this enum"

# direction-or-none-enum: the same four plus 'none, which IS the compositor's
# DIRECTION_DEFAULT (-1) — the value CGroup::remove tests for to mean
# "release the window without aiming it anywhere".
val '(enum-value direction-or-none-enum (quote none))'  '-1'
val '(enum-value direction-or-none-enum (quote right))' '1'
ok  '(equal? (enum-set->list (enum-values->enum direction-or-none-enum))
             (quote (up right down left none)))'

# corner-enum: named after the compositor's eRectCorner, but numbered after
# Actions::moveCursorToCorner's switch — where the arms run bottom-left,
# bottom-right, top-right, top-left, NOT in reading order. Getting this wrong
# would warp the cursor to the wrong corner, so it is pinned exactly.
val '(enum-value corner-enum (quote bottom-left))'  '0'
val '(enum-value corner-enum (quote bottom-right))' '1'
val '(enum-value corner-enum (quote top-right))'    '2'
val '(enum-value corner-enum (quote top-left))'     '3'
err '(enum-value corner-enum (quote middle))' "not a value of this enum"

# key-state-enum: 0 up, 1 down, 2 repeat — the comment beside
# Actions::sendKeyState's parameter, and what its dispatcher maps
# "down"/"up"/"repeat" to.
val '(enum-value key-state-enum (quote up))'     '0'
val '(enum-value key-state-enum (quote down))'   '1'
val '(enum-value key-state-enum (quote repeat))' '2'
err '(enum-value key-state-enum (quote sideways))' "not a value of this enum"

# ---- an enum used by CORE itself --------------------------------------------
# bind-flag-enum belongs to (hyprscheme core): the BIND_FLAG_* bits are core's
# own data — hl--bind-flags turns an hl-bind-add! option set into the one
# integer the boundary takes, and core builds the table from it. Core is a leaf
# like (hyprscheme enums), so it may import the machinery; core is NOT in
# (hyprscheme values), which is for a set more than one FAMILY speaks.
#
# The bits are pinned against Bind.hpp's own shifts, because a table shifted by
# one would still be a perfectly coherent enum and would put every option on its
# neighbour's flag. The ORDER matters as much as the values — Bind.hpp lists
# them locked (1 << 0) through mouse (1 << 15), and this must read the same way,
# which is what makes the two lists comparable line by line.
val '(enum-value bind-flag-enum (quote locked))'                '1'
val '(enum-value bind-flag-enum (quote release))'               '2'
val '(enum-value bind-flag-enum (quote repeat))'                '4'
val '(enum-value bind-flag-enum (quote long-press))'            '8'
val '(enum-value bind-flag-enum (quote non-consuming))'         '16'
val '(enum-value bind-flag-enum (quote auto-consuming))'        '32'
val '(enum-value bind-flag-enum (quote transparent))'           '64'
val '(enum-value bind-flag-enum (quote ignore-mods))'           '128'
val '(enum-value bind-flag-enum (quote dont-inhibit))'          '256'
val '(enum-value bind-flag-enum (quote click))'                 '512'
val '(enum-value bind-flag-enum (quote drag))'                  '1024'
val '(enum-value bind-flag-enum (quote submap-universal))'      '2048'
val '(enum-value bind-flag-enum (quote allow-input-capture))'   '4096'
val '(enum-value bind-flag-enum (quote device-inclusive))'      '8192'
val '(enum-value bind-flag-enum (quote catch-all))'             '16384'
val '(enum-value bind-flag-enum (quote mouse))'                 '32768'
# the declaration ORDER, which is Bind.hpp's order — the whole point of writing
# the shifts out rather than computing them. Read through enum-values->enum, so
# this uses only public API and is the same route a config has.
ok  '(equal? (enum-set->list (enum-values->enum bind-flag-enum))
             (quote (locked release repeat long-press non-consuming
                     auto-consuming transparent ignore-mods dont-inhibit
                     click drag submap-universal allow-input-capture
                     device-inclusive catch-all mouse)))'
# ...and the set is COMPLETE at 16. A flag added in the compositor and not here
# is an option that cannot be expressed at all; one removed and left here is a
# bit that silently means something else. Both are this line.
val '(length (enum-set->list (enum-values->enum bind-flag-enum)))' '16'
err '(enum-value bind-flag-enum (quote sideways))' "not a value of this enum"

exit $FAILED
