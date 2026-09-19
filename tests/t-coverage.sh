# meta-coverage: every public hl- API defined in the bootstrap must be
# referenced by at least one test in this suite. Internal hl-- helpers are
# exempt. Adding a public API without a test fails here by design.
apis=$(awk '/^static constexpr const char\* SCHEME_BOOTSTRAP/,/^\)scm";/' \
        src/config/scheme/SchemeManager.cpp \
      | grep -o "^(define (hl-[a-z0-9-?]*" | sed 's/(define (//' | sort -u \
      | grep -v '^hl--')
missing=0
for a in $apis; do
  grep -q -- "$a" tests/t-*.sh || { echo "API not covered by any test: $a"; missing=1; }
done
# kbd has no hl- prefix; pin it explicitly
grep -q '(kbd ' tests/t-api.sh || { echo "kbd not covered"; missing=1; }
[[ $missing -eq 0 ]]
