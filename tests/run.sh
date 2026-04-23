#!/usr/bin/env bash
# Integration tests: tests/pass/**/*.jolt must compile;
# tests/fail/**/*.jolt must error.
# Optional fail/<case>.jolt.expected: first line must appear in compiler output.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
COMPILER="$ROOT/bin/compiler"
OUT="$(mktemp)"
trap 'rm -f "$OUT"' EXIT

cd "$ROOT" || exit 2

if [[ ! -x "$COMPILER" ]]; then
  echo "error: $COMPILER not found (run make first)" >&2
  exit 2
fi

failures=0

run_pass() {
  local f="$1"
  if "$COMPILER" "$f" >"$OUT" 2>&1; then
    echo "ok  pass  ${f#"$ROOT"/}"
  else
    echo "FAIL pass  ${f#"$ROOT"/} (expected success)" >&2
    cat "$OUT" >&2
    failures=$((failures + 1))
  fi
}

run_fail() {
  local f="$1"
  if "$COMPILER" "$f" >"$OUT" 2>&1; then
    echo "FAIL fail  ${f#"$ROOT"/} (expected failure)" >&2
    failures=$((failures + 1))
    return
  fi
  local exp="${f}.expected"
  if [[ -f "$exp" ]]; then
    IFS= read -r line <"$exp" || true
    if [[ -z "${line:-}" ]]; then
      echo "ok  fail  ${f#"$ROOT"/}"
      return
    fi
    if grep -Fq -- "$line" "$OUT"; then
      echo "ok  fail  ${f#"$ROOT"/}"
    else
      echo "FAIL fail  ${f#"$ROOT"/} (output missing: $line)" >&2
      cat "$OUT" >&2
      failures=$((failures + 1))
    fi
  else
    echo "ok  fail  ${f#"$ROOT"/}"
  fi
}

# Skip `*/data/*`: those modules are only pulled in via imports when compiling
# their parent folder's client (see tests/pass/module_*/).
mapfile -d '' -t pass_files < <(
  find "$ROOT/tests/pass" -name '*.jolt' ! -path '*/data/*' -print0 | sort -z
)
if ((${#pass_files[@]} == 0)); then
  echo "error: no tests under tests/pass" >&2
  exit 2
fi
for f in "${pass_files[@]}"; do
  [[ -n "$f" ]] || continue
  run_pass "$f"
done

mapfile -d '' -t fail_files < <(find "$ROOT/tests/fail" -name '*.jolt' -print0 | sort -z)
for f in "${fail_files[@]}"; do
  [[ -n "$f" ]] || continue
  run_fail "$f"
done

if ((failures > 0)); then
  echo "$failures test(s) failed" >&2
  exit 1
fi
echo "all tests passed"
