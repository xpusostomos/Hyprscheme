#!/usr/bin/env bash
# Soak test — hammers the plugin for a configurable duration to see what
# shakes loose under sustained load. NOT part of the standard suite
# (deliberately not named t-*.sh, so run.sh never picks it up).
#
#   tests/soak.sh [SECONDS]        # default 300 (5 minutes)
#
# What it hammers (mixed batches, repeated until time is up):
#   state       hl-state set/ref/keys/remove churn
#   events      notification handlers added + removed (leak detector:
#               the count of live handles must return to baseline)
#   timers      hl-after one-shots (self-release) + repeat/cancel cycles
#   binds       hl-bind-add! + hl-unbind! cycles
#   gestures    hl-gesture-add! + hl-gesture-remove! cycles
#   rules       window rules created once, enabled/disabled toggled forever
#   notifs      hl-notify! bursts + notification objects create/cancel
#   windows     spawn/churn foot windows; getters + actions on live ones
#   exec        (hl-exec! "true") spawns
#
# Everything lands in /tmp/hyprscheme-soak.$$/:
#   compositor.log   the compositor's own output (grepped for ERR at the end)
#   soak-errors.log  every failed eval: TS ITER CATEGORY EXPR OUTPUT
#   soak-heartbeat.log  progress line per iteration batch
#   soak-summary.txt    final counts + verdict
#
# Exit 0 only when the run survived with zero eval errors, zero compositor
# ERR lines, and the leak detectors (live events/timers/notifications)
# back at baseline.

set -u
DURATION=${1:-300}
SECS=$((DURATION < 5 ? 5 : DURATION))
BATCH_EVALS=8      # hyprctl scheme evals per iteration batch
MAX_WINDOWS=6      # concurrent churn windows

BIN="${BIN:-$HOME/.local/bin/hyprland-scheme}"
[[ -x $BIN ]] || BIN=hyprland-scheme
command -v "$BIN" >/dev/null 2>&1 || { echo "FAIL: no hyprland-scheme binary (make install-compositor?)"; exit 1; }
[[ -f $HOME/.local/lib/hyprscheme/scheme-plugin.so ]] || { echo "FAIL: plugin not installed"; exit 1; }

WORK=/tmp/hyprscheme-soak.$$
mkdir -p "$WORK"
rm -f /tmp/hyprscheme-soak-last 2>/dev/null; ln -sfn "$WORK" /tmp/hyprscheme-soak-last
CLOG="$WORK/compositor.log"
ELOG="$WORK/soak-errors.log"
HLOG="$WORK/soak-heartbeat.log"
SLOG="$WORK/soak-summary.txt"

cd "$(dirname "$0")/.."
export XDG_CONFIG_HOME="$WORK/config" XDG_STATE_HOME="$WORK/state"
export HYPRSCHEME_CONFIG="$XDG_CONFIG_HOME/hypr/hyprland.scm"
mkdir -p "$XDG_CONFIG_HOME/hypr" "$XDG_STATE_HOME"
cp tests/config/hyprland.lua "$XDG_CONFIG_HOME/hypr/"
cp tests/config/hyprland.scm "$XDG_CONFIG_HOME/hypr/"

echo "launching nested compositor (soak, ${SECS}s, workdir $WORK)"
"$BIN" --config "$XDG_CONFIG_HOME/hypr/hyprland.lua" > "$CLOG" 2>&1 &
COMPOSITOR_PID=$!
cleanup() {
  kill "$COMPOSITOR_PID" 2>/dev/null
  wait "$COMPOSITOR_PID" 2>/dev/null
  [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]] && rm -rf "$XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE" 2>/dev/null
}
trap cleanup EXIT

mapfile -t BEFORE < <(ls "$XDG_RUNTIME_DIR/hypr" 2>/dev/null)
# wait for the NEW instance's IPC to answer — the socket file appears well
# before the event loop services it, stale dirs from earlier instances must
# be ignored, and a REAL running session must never be picked up
DEADLINE=$((SECONDS + 120))
while (( SECONDS < DEADLINE )); do
  for d in "$XDG_RUNTIME_DIR/hypr"/*; do
    [[ -d $d ]] || continue
    sig=${d##*/}
    skip=0
    for b in "${BEFORE[@]:-}"; do [[ $sig == "$b" ]] && skip=1 && break; done
    (( skip )) && continue
    if env HYPRLAND_INSTANCE_SIGNATURE=$sig timeout 2 hyprctl version >/dev/null 2>&1; then
      HYPRLAND_INSTANCE_SIGNATURE=$sig
      break 2
    fi
  done
  sleep 0.5
done
export HYPRLAND_INSTANCE_SIGNATURE
[[ -n $HYPRLAND_INSTANCE_SIGNATURE ]] || { echo "FAIL: compositor never became responsive"; tail -30 "$CLOG"; exit 1; }

if ! timeout 20 env HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE hyprctl plugin load "$HOME/.local/lib/hyprscheme/scheme-plugin.so" | grep -q ok; then
  echo "FAIL: plugin load"; tail -20 "$CLOG"; exit 1
fi
sleep 1
export SCHEME="timeout 15 env HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE hyprctl scheme"
export HYP="timeout 10 hyprctl"

# --- counters ----------------------------------------------------------------
ITER=0
ERRORS=0
EVALS=0
declare -A CAT_ERRORS

record_error() { # record_error CATEGORY EXPR OUTPUT
  ERRORS=$((ERRORS+1))
  CAT_ERRORS[$1]=$((${CAT_ERRORS[$1]:-0}+1))
  printf '%s iter=%s cat=%s expr=%s\n  out=%s\n' "$(date +%H:%M:%S)" "$ITER" "$1" "$2" "$3" >> "$ELOG"
}

eval_scheme() { # eval_scheme CATEGORY EXPR -> echoes output; logs + counts errors
  local cat=$1 expr=$2 out
  EVALS=$((EVALS+1))
  out=$($SCHEME "$expr" 2>&1)
  if [[ $out == "error:"* ]]; then
    record_error "$cat" "$expr" "$out"
  fi
}

ping_alive() { # returns 0 when the interpreter answers
  [[ $COMPOSITOR_PID ]] && kill -0 "$COMPOSITOR_PID" 2>/dev/null || return 1
  out=$(timeout 20 env HYPRLAND_INSTANCE_SIGNATURE=$HYPRLAND_INSTANCE_SIGNATURE hyprctl scheme '(+ 1 1)' 2>&1)
  [[ $out == "2" ]]
}

# --- fixture -----------------------------------------------------------------
$SCHEME '(define soak-fixture (lambda ()
  (hl-exec! "foot -a soak-main")
  #t))' >/dev/null
eval_scheme fixture '(soak-fixture)'
for _ in $(seq 1 30); do
  w=$($SCHEME '(let ((w (hl-window-from "class:^soak-main$"))) (if w #t #f))' 2>/dev/null)
  [[ $w == "#t" ]] && break
  sleep 0.5
done
w=$($SCHEME '(let ((w (hl-window-from "class:^soak-main$"))) (if w #t #f))' 2>/dev/null)
if [[ $w != "#t" ]]; then echo "FAIL: fixture window never appeared"; exit 1; fi

# baseline counters for the leak detectors (live handles must come home)
BASE_EVENTS=$($SCHEME '(length (hl-notifications))' 2>/dev/null); BASE_EVENTS=${BASE_EVENTS:-0}

# the rule pool: created once, toggled forever
eval_scheme fixture "(begin (define soak-rule (hl-window-rule-add! \"soak-rule\" 'match (quote (class \"^soak-churn\")) 'opacity \"0.9\")) (define soak-rule2 (hl-window-rule-add! \"soak-rule2\" 'match (quote (class \"^soak-churn\")) 'float #t)) #t)"

# the custom layout: a shifting master/stack whose ratio wobbles via
# hl-layout-msg; recalc errors fall back to the default grid (that path
# is part of what we are exercising)
eval_scheme fixture "(let ((mfact (vector 0.5)) (opens (vector 0)) (closes (vector 0)))
  (hl-layout-add! \"soak-layout\"
    'recalculate (lambda (count W H windows)
                   (cond ((= count 0) '())
                         ((= count 1) (list (list 0 0 W H)))
                         (else
                          (let* ((mw (exact (floor (* W (vector-ref mfact 0)))))
                                 (sl (- count 1))
                                 (sh (quotient H sl)))
                            (let build ((i 1) (acc (list (list 0 0 mw H))))
                              (if (> i sl) (reverse acc)
                                  (build (+ i 1)
                                         (cons (list (+ mw 0) (* (- i 1) sh) (- W mw) sh) acc))))))))
    'resize (lambda (count W H windows dx dy corner)
              (vector-set! mfact 0 (max 0.2 (min 0.8 (+ (vector-ref mfact 0) (* dx 0.0005)))))
              #f)
    'window-open (lambda (w) (vector-set! opens 0 (+ 1 (vector-ref opens 0))) #f)
    'window-close (lambda (w) (vector-set! closes 0 (+ 1 (vector-ref closes 0))) #f)
    'layout-msg (lambda (msg)
                  (cond ((equal? msg \"wider\") (vector-set! mfact 0 (min 0.8 (+ (vector-ref mfact 0) 0.05))) #t)
                        ((equal? msg \"narrower\") (vector-set! mfact 0 (max 0.2 (- (vector-ref mfact 0) 0.05))) #t)
                        (else #f))))
  #t)"
# workspace 9 runs the layout; pool windows live there
eval_scheme fixture "(begin (hl-workspace-rule-add! \"9\" 'layout \"scheme:soak-layout\") #t)"

# spawn the LIVE POOL (kept open for the whole run, rearranged every iteration)
eval_scheme fixture "(begin (hl-exec! \"foot -a soak-pool-1\") (hl-exec! \"foot -a soak-pool-2\") (hl-exec! \"foot -a soak-pool-3\") (hl-exec! \"foot -a soak-pool-4\") (hl-exec! \"foot -a soak-pool-5\") #t)"
for _ in $(seq 1 40); do
  n=$($SCHEME '(length (hl-windows-from "class:soak-pool-.*"))' 2>/dev/null)
  [[ $n == "5" ]] && break
  [[ $_ == 40 ]] && echo "pool probe last: $n"
  sleep 0.5
done
n=$($SCHEME '(length (hl-windows-from "class:soak-pool-.*"))' 2>/dev/null)
[[ $n == "5" ]] || { echo "FAIL: pool windows never appeared (got $n/5)"; exit 1; }
eval_scheme fixture "(begin (for-each (lambda (w) (hl-window-workspace-set! w \"9\")) (hl-windows-from \"class:soak-pool-.*\")) #t)"
sleep 1

# --- the hammer loop ---------------------------------------------------------
END=$((SECONDS + SECS))
NEXT_REPORT=$SECONDS
echo "soak started: ${SECS}s, batching $BATCH_EVALS evals/iteration"

while (( SECONDS < END )); do
  ITER=$((ITER+1))

  for _ in $(seq 1 $BATCH_EVALS); do
    # state churn (set! two keys, ref a live one + a missing one with default)
    out=$(eval_scheme state "(begin (hl-state-set! (quote soak-k) $ITER) (hl-state-set! (quote soak-t) (quote ($ITER))) (hl-state-set! (quote soak-gone) 1) (list (hl-state-ref (quote soak-k)) (hl-state-ref (quote soak-missing) 7) (hl-state-remove! (quote soak-gone)) (null? (hl-state-remove! (quote soak-never)))))")
    [[ $out == "($ITER 7 #t #f)" ]] || { ERRORS=$((ERRORS+1)); CAT_ERRORS[state]=$((${CAT_ERRORS[state]:-0}+1)); printf '%s iter=%s cat=state expr=state-roundtrip\n  out=%s\n' "$(date +%H:%M:%S)" "$ITER" "$out" >> "$ELOG"; }

    # events: register + remove + remove-again semantics; the second remove
    # must report #f (the record is inert)
    out=$(eval_scheme events "(let ((e (hl-window-title-notification-add! (lambda _ #f)))) (and (hl-notification-remove! e) (not (hl-notification-remove! e))))")
    [[ $out == "#t" ]] || { ERRORS=$((ERRORS+1)); CAT_ERRORS[events]=$((${CAT_ERRORS[events]:-0}+1)); printf '%s iter=%s cat=events expr=event-remove-semantics\n  out=%s\n' "$(date +%H:%M:%S)" "$ITER" "$out" >> "$ELOG"; }
    # bubble objects: dismiss! semantics — dismiss returns #t, and the handle
    # reads stale (#f) from every getter afterwards
    out=$(eval_scheme events2 "(let ((n (hl-notification-add! (quote text) \"soak\" (quote timeout) 10))) (and (hl-notification-dismiss! n) (not (hl-notification-text n))))")
    [[ $out == "#t" ]] || { ERRORS=$((ERRORS+1)); CAT_ERRORS[notifs]=$((${CAT_ERRORS[notifs]:-0}+1)); printf '%s iter=%s cat=notifs expr=bubble-remove-semantics\n  out=%s\n' "$(date +%H:%M:%S)" "$ITER" "$out" >> "$ELOG"; }

    # timers: one-shot (self-releases) + repeat/cancel in the same eval
    eval_scheme timers "(begin (hl-after 5 (lambda () #f)) (hl-timer-cancel! (hl-repeat 5 (lambda () #f))) #t)"

    # binds: add + unbind cycle
    eval_scheme binds "(hl-unbind! (hl-bind-add! (hl-key \"SUPER+F$((ITER % 12 + 13))\") (lambda () #f)))"

    # gestures: add + remove cycle
    eval_scheme gestures "(hl-gesture-remove! (hl-gesture-add! (quote fingers) 8 (quote direction) \"up\" (quote action) (hl-make-move-gesture)))"

    # rules: toggle the pool
    eval_scheme rules "(begin (hl-rule-enabled-set! soak-rule (not (hl-rule-enabled? soak-rule))) #t)"

    # notifications: object create + cancel
    eval_scheme notifs "(begin (hl-notification-remove! (hl-notification-add! (quote text) \"soak\" (quote timeout) 10)) #t)"

    # getters + actions on the live window
    eval_scheme getters "(let ((w (hl-window-from \"class:^soak-main$\"))) (and w (hl-window-title w) (hl-window-floating? w) (integer? (hl-window-pid w)) (pair? (hl-window-size w)) #t))"
    eval_scheme actions "(let ((w (hl-window-from \"class:^soak-main$\"))) (and w (begin (hl-window-focus! w) (hl-window-float-set! w) (hl-window-float-set! w #f) (hl-window-position-set! w 10 10 (quote relative)) #t)))"

    # exec
    eval_scheme exec "(integer? (hl-exec! \"true\"))"
  done

  # rearrange the live pool: visible churn — toggles, moves, workspace
  # hops, layout messages (each accepted layout-msg re-runs recalculate)
  eval_scheme rearrange "(let* ((pool (hl-windows-from \"class:soak-pool-.*\"))
         (i (modulo $ITER (length pool)))
         (w (list-ref pool i)))
    (hl-window-focus! w)
    (case (modulo $ITER 6)
      ((0) (hl-window-float-set! w))
      ((1) (hl-window-float-set! w #f))
      ((2) (hl-window-fullscreen-set! w) (hl-window-fullscreen-set! w #f))
      ((3) (hl-window-maximized-set! w) (hl-window-maximized-set! w #f))
      ((4) (hl-window-workspace-set! w \"8\") (hl-window-workspace-set! w \"9\"))
      ((5) (hl-window-move-direction! w \"r\") (hl-window-center! w)))
    (when (hl-window-floating? w)
      (hl-window-position-set! w $((ITER % 7)) $((ITER % 5)) (quote relative))
      (hl-window-size-set! w 20 10 (quote relative)))
    (hl-layout-msg \"wider\")
    (hl-layout-msg \"narrower\")
    #t)"
  # pool integrity: all five alive, none lost
  out=$(eval_scheme rearrange-check "(length (hl-windows-from \"class:soak-pool-.*\"))")
  [[ $out == "5" ]] || { ERRORS=$((ERRORS+1)); CAT_ERRORS[pool]=$((${CAT_ERRORS[pool]:-0}+1)); printf '%s iter=%s cat=pool expr=pool-integrity\n  out=%s\n' "$(date +%H:%M:%S)" "$ITER" "$out" >> "$ELOG"; }

  # window churn: spawn a sacrificial, then close it (burst load)
  win_iter=$((ITER % MAX_WINDOWS + 1))
  $SCHEME "(hl-exec! \"foot -a soak-churn$win_iter\")" >/dev/null 2>&1 &
  sleep 0.7
  $SCHEME "(let loop ((ws (hl-windows-from \"class:^soak-churn$win_iter$\"))) (unless (null? ws) (hl-window-close! (car ws)) (loop (cdr ws))))" >/dev/null 2>&1

  # liveness check every iteration batch
  if ! ping_alive; then
    ERRORS=$((ERRORS+1))
    printf '%s iter=%s compositor/interpreter UNRESPONSIVE\n' "$(date +%H:%M:%S)" "$ITER" >> "$ELOG"
    echo "FAIL: compositor stopped answering at iter $ITER"; break
  fi

  if (( SECONDS >= NEXT_REPORT )); then
    LEFT=$((END - SECONDS))
    printf '%s iter=%s evals=%s errors=%s remaining=%ss\n' "$(date +%H:%M:%S)" "$ITER" "$EVALS" "$ERRORS" "$LEFT" >> "$HLOG"
    NEXT_REPORT=$((SECONDS + 30))
  fi
done

# --- teardown + report -------------------------------------------------------
# churn windows may have survived the burst race; sweep them
$SCHEME '(let loop ((ws (hl-windows-from "class:soak-churn.*"))) (unless (null? ws) (hl-window-close! (car ws)) (loop (cdr ws))))' >/dev/null 2>&1
sleep 1

END_EVENTS=$($SCHEME '(length (hl-notifications))' 2>/dev/null); END_EVENTS=${END_EVENTS:-0}
CERRS=$(grep -c "] ERR" "$CLOG" 2>/dev/null); CERRS=${CERRS:-0}
CRASH=$(grep -ci "segfault\|SIGSEGV\|terminate called\|static assertion" "$CLOG" 2>/dev/null); CRASH=${CRASH:-0}

{
  echo "soak: ${SECS}s, $ITER iterations, $EVALS evals"
  echo "eval errors: $ERRORS"
  for k in "${!CAT_ERRORS[@]}"; do echo "  $k: ${CAT_ERRORS[$k]}"; done
  echo "compositor ERR lines: $CERRS"
  echo "compositor crash markers: $CRASH"
  echo "live notifications: start=$BASE_EVENTS end=$END_EVENTS"
  if (( ERRORS == 0 && CERRS == 0 && CRASH == 0 )); then
    echo "VERDICT: PASS"
  else
    echo "VERDICT: FAIL — see $ELOG / $CLOG"
  fi
} | tee "$SLOG"

[[ $ERRORS == 0 && $CERRS == 0 && $CRASH == 0 && $BASE_EVENTS == $END_EVENTS ]]
