#!/usr/bin/env bash
# api-check — report what the PUBLIC API has lost or gained since a snapshot.
#
#   tools/api-check.sh              # diff against API.snapshot (the committed baseline)
#   tools/api-check.sh --update     # rewrite API.snapshot to the current API
#
# WHY THIS EXISTS. The suite only tests the names IT uses, so a rename or a
# removal looks perfectly clean from inside the repo — while breaking every
# config outside it. That happened three times in one session: hl-layout-msg
# became hl-layout-msg!, hl-is-key-down became hl-key-down?, and an entire
# function was split in two. Each time every lint passed and the suite was
# green, because a config in ~/.config is a consumer the repo cannot see.
#
# So the API gets a baseline, and any change to it has to be a DELIBERATE act.
# The list itself is not maintained here: it is whatever
# tools/api-names.scm derives by introspecting the modules, so the snapshot is
# a photograph of the API rather than another list to keep in step.
#
# REMOVALS AND RENAMES ARE THE POINT, so they are what the exit code is about.
# An addition is reported but does not fail: adding a function cannot break
# anybody, and demanding a snapshot bump for every one would make the tool
# annoying enough to ignore.
#
# This is NOT run by `make check` — it fails on any removal by design, and a
# normal working tree legitimately has removals mid-change. Run it before a
# release, or when you have finished a batch of API work and want to see what
# you broke.
set -u
cd "$(dirname "$0")/.."

SNAP=API.snapshot
CUR=$(mktemp)
trap 'rm -f "$CUR"' EXIT

guile --no-auto-compile -s tools/api-names.scm 2>/dev/null | LC_ALL=C sort -u > "$CUR"
n=$(LC_ALL=C grep -c . "$CUR")
if [[ $n -lt 200 ]]; then
  echo "api-check: derived only $n names — the DERIVATION is broken, not the API"
  exit 1
fi

if [[ ${1:-} == --update ]]; then
  cp "$CUR" "$SNAP"
  echo "api-check: $SNAP updated ($n names)"
  exit 0
fi

if [[ ! -f $SNAP ]]; then
  echo "api-check: no $SNAP — run: tools/api-check.sh --update"
  exit 1
fi

removed=$(LC_ALL=C comm -23 "$SNAP" "$CUR")
added=$(LC_ALL=C comm -13 "$SNAP" "$CUR")

if [[ -z $removed && -z $added ]]; then
  echo "api-check: api unchanged ($n names)"
  exit 0
fi

if [[ -n $added ]]; then
  echo "ADDED ($(printf '%s\n' "$added" | LC_ALL=C grep -c .)) — cannot break a config:"
  printf '  + %s\n' $added
fi

if [[ -n $removed ]]; then
  echo
  echo "REMOVED ($(printf '%s\n' "$removed" | LC_ALL=C grep -c .)) — EVERY config calling these now raises"
  echo "\"Unbound variable\", which aborts the rest of the file:"
  printf '  - %s\n' $removed
  echo
  echo "If these were RENAMED, say so in DONE.txt and in the wiki, and give the"
  echo "old name a note in the docs — a config is a consumer you cannot see."
  echo "Then: tools/api-check.sh --update"
  exit 1
fi

exit 0
