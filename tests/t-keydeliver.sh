# A SENT KEY MUST BE RESOLVED TO A KEYCODE, NOT A KEYSYM.
#
# THE BUG THIS PINS (live session, 2026-10-22, after "the keys still don't
# work"): the send-shortcut / send-key-state gsubrs resolved their key with
# xkb_keysym_from_name and handed the KEYSYM to Config::Actions::pass. But pass
# forwards its key straight to sendKeyboardKey as `key - 8` — an XKB/evdev
# KEYCODE, the same units as the keybind manager's m_lastCode. The two are
# unrelated numberings, so every sent key was the wrong number:
#
#   name     keysym   what pass() sent   result
#   f        0x0066   94                 not "f" (some other key)
#   Right    0xff53   65355              out of keycode range -> DROPPED
#   Next     0xff56   65358              out of range -> DROPPED
#   Prior    0xff55   65357              out of range -> DROPPED
#
# So the named keys — Right/Left/Next/Prior, i.e. every one of the five chords
# Chris reported (C-f, C-b, C-v, M-v, and C-x C-f's C-l) — were discarded
# silently, and the letter keys pressed the wrong letter. Nothing raised,
# because pass() never validates its keycode: actionResult answered 0 and the
# wrappers answered #t. The fix resolves through Keybinds::resolver(), exactly
# as upstream's own Lua dispatcher does.
#
# WHY THE SUITE MISSED IT, which is the reason this file exists: t-crashdir
# asserts that (hl-window-send-shortcut! ...) ANSWERS #t. It did — while sending
# garbage. An assertion on a RETURN VALUE cannot see a wrong PAYLOAD.
#
# ---- WHAT THIS FILE CAN AND CANNOT ASSERT, measured, not assumed -------------
# It CANNOT assert delivered bytes. pass() stamps the event with
# state()->m_timeLastMs, which is set by a REAL key event and is therefore 0
# under the harness; a synthetic press is dropped downstream. Measured in a
# nested compositor: a correct, explicitly-keycoded send still delivers 0 bytes
# to a focused `cat`, with a keyboard present and no error logged. So a
# byte-level test here would fail on correct code — vacuous in the other
# direction. Delivery is exercised by hand in a live session; this file pins the
# RESOLUTION, which is the half the bug was in and the half a harness can see.
#
# The assertions below are chosen because they SEPARATE the two
# implementations. Measured against both:
#
#   name            keysym path (old)      resolver path (new)
#   code:28         -1  (not a keysym)      0
#   mouse:272       -1  (not a keysym)      0
#   Cyrillic_a       0  (a real keysym)    -1  (not on this keymap)
#   Arabic_alef      0  (a real keysym)    -1
#   f / Right        0                      0   (both fine — not assertable)
#
# So the file asserts the two rows where they differ: Hyprland's own keycode
# tokens must be ACCEPTED (only the resolver knows them), and a keysym that is
# not on the keymap must be REFUSED (only the resolver checks). A return of -1
# from the gsubr is what every Scheme wrapper above it turns into a raised error.
#
# NOT asserted: a junk NAME like "notakey". resolveKeycode answers NoSymbol-matches
# -the-first-unmapped-keycode for it, i.e. upstream returns a keycode rather than
# an error. That is upstream's behaviour, not this bug, and pinning it here would
# encode a quirk as a contract.
set -u

fail=0
failures=()

alive() {
  if ! $SCHEME '(hl-version)' >/dev/null 2>&1; then
    echo "FATAL: the compositor is GONE — see the compositor log in \$WORK"
    exit 1
  fi
}

# the raw gsubr's status: 0 = a keycode was resolved and the action ran,
# -1 = refused before the compositor saw anything. Called with a window of #f
# so it does not need a fixture, and through the PUBLIC wrapper too, because the
# wrapper is what a config calls and it must raise rather than answer #t.
status() {   # status KEY -> the gsubr's integer status
  $SCHEME "(hl--c-send-key-state '(\"CTRL\") \"$1\" 1 #f)" 2>&1 | head -1
}
refused() {  # refused KEY -> the public wrapper must RAISE for KEY
  $SCHEME "(call-with-string-output-port (lambda (p) (guard (e (#t (hl--print-exception e p))) (hl-window-send-key-state! #:keys (list \"CTRL\" \"$1\") #:state 'down))))" 2>&1 | head -2 | tr '\n' ' '
}

alive

# ---- Hyprland's own keycode tokens: ONLY the resolver understands these ------
# xkb_keysym_from_name("code:28") is 0, so the old code refused them. A config
# that spells a key as a keycode is legal in Hyprland, so refusing it is a bug
# in its own right — and accepting it is proof the resolver is on the path.
for tok in "code:28" "code:38" "mouse:272"; do
  out=$(status "$tok")
  alive
  [[ "$out" == "0" ]] || {
    failures+=("keycode token '$tok' -> status [$out], wanted 0 — the token is resolvable by the compositor's own resolver, so a refusal means the key was resolved as a KEYSYM (the old bug)")
    fail=1
  }
done

# ---- a keysym that is NOT on this keymap must be refused --------------------
# These ARE valid keysyms (xkb_keysym_from_name answers non-zero), so the old
# code accepted them and sent a nonsense keycode. The resolver checks the
# keymap, so it refuses — and the refusal must surface as a raised error, not a
# silent #t, or a config cannot tell that its key did nothing.
for bad in "Cyrillic_a" "Cyrillic_A" "Arabic_alef"; do
  out=$(status "$bad")
  alive
  [[ "$out" == "-1" ]] || {
    failures+=("off-keymap keysym '$bad' -> status [$out], wanted -1 (refused) — it is a valid keysym but not on the us keymap, so accepting it means the keymap was never consulted")
    fail=1
  }
  msg=$(refused "$bad")
  alive
  [[ "$msg" != *"#t"* ]] || {
    failures+=("off-keymap keysym '$bad': the public wrapper answered #t — a refused key must RAISE, or a config cannot tell its key did nothing")
    fail=1
  }
done

# ---- the ordinary spellings must still resolve ------------------------------
# These behave the same in both implementations, so they do not separate them;
# they are here so that a future change cannot fix the two rows above by
# refusing everything.
for good in "f" "b" "v" "l" "x" "Right" "Left" "Next" "Prior" "<right>"; do
  out=$(status "$good")
  alive
  [[ "$out" == "0" ]] || { failures+=("key '$good' -> status [$out], wanted 0"); fail=1; }
done

# ---- and the PUBLIC wrappers, on the chords Chris actually reported ----------
# These are the five that were broken. They cannot assert delivery (above), but
# they must at least resolve and answer #t through the real API shape a config
# uses — #:keys as one hl-kbd chord, #:state a key-state-enum.
for chord in "C-f" "C-b" "C-v" "M-v" "C-l"; do
  out=$($SCHEME "(hl-window-send-key-state! #:keys (hl-kbd \"$chord\") #:state 'down)" 2>&1 | head -1)
  alive
  [[ "$out" == "#t" ]] || { failures+=("(hl-window-send-key-state! #:keys (hl-kbd \"$chord\")) -> [$out], wanted #t"); fail=1; }
done
# the named-key payloads themselves, through the wrapper
for k in "<right>" "<left>" "<next>" "<prior>"; do
  out=$($SCHEME "(hl-window-send-key-state! #:keys (hl-kbd \"$k\") #:state 'down)" 2>&1 | head -1)
  alive
  [[ "$out" == "#t" ]] || { failures+=("payload $k through the wrapper -> [$out], wanted #t"); fail=1; }
done
# send-shortcut takes the same chords through the same resolver
for chord in "C-f" "C-l" "S-<f3>"; do
  out=$($SCHEME "(hl-window-send-shortcut! #:keys (hl-kbd \"$chord\"))" 2>&1 | head -1)
  alive
  [[ "$out" == "#t" ]] || { failures+=("(hl-window-send-shortcut! #:keys (hl-kbd \"$chord\")) -> [$out], wanted #t"); fail=1; }
done

alive
if [[ $fail -eq 0 ]]; then
  echo "  keys resolve through the compositor's own resolver: keycode tokens accepted, off-keymap names refused"
  exit 0
fi
printf 'KEY-DELIVERY FAILURES (%d):\n' "${#failures[@]}"
printf '  %s\n' "${failures[@]:0:8}"
[[ ${#failures[@]} -gt 8 ]] && printf '  ... and %d more\n' "$(( ${#failures[@]} - 8 ))"
exit 1
