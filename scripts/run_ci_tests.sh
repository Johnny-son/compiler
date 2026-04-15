#!/usr/bin/env bash

set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <compiler-bin> <test-dir> [pattern]" >&2
  exit 1
fi

COMPILER_BIN="$1"
TEST_DIR="$2"
PATTERN="${3:-}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
RUNTIME_LIB="${REPO_ROOT}/test/contesttestcases/lib/std.c"

if [[ ! -x "${COMPILER_BIN}" ]]; then
  echo "Compiler binary not found or not executable: ${COMPILER_BIN}" >&2
  exit 1
fi

if [[ ! -d "${TEST_DIR}" ]]; then
  echo "Test directory not found: ${TEST_DIR}" >&2
  exit 1
fi

if [[ ! -f "${RUNTIME_LIB}" ]]; then
  echo "Runtime library not found: ${RUNTIME_LIB}" >&2
  exit 1
fi

mapfile -t CASES < <(find "${TEST_DIR}" -maxdepth 1 -type f -name "*.c" | sort)

if [[ -n "${PATTERN}" ]]; then
  FILTERED=()
  for case_file in "${CASES[@]}"; do
    case_name="$(basename "${case_file}" .c)"
    if [[ "${case_name}" == ${PATTERN}* ]]; then
      FILTERED+=("${case_file}")
    fi
  done
  CASES=("${FILTERED[@]}")
fi

if [[ ${#CASES[@]} -eq 0 ]]; then
  echo "No test cases matched in ${TEST_DIR}" >&2
  exit 1
fi

workdir="$(mktemp -d)"
trap 'rm -rf "${workdir}"' EXIT

ok_count=0
ng_count=0

for case_file in "${CASES[@]}"; do
  case_name="$(basename "${case_file}" .c)"
  input_file="${TEST_DIR}/${case_name}.in"
  output_file="${TEST_DIR}/${case_name}.out"
  asm_file="${workdir}/${case_name}.s"
  bin_file="${workdir}/${case_name}"
  result_file="${workdir}/${case_name}.result"

  echo "Running ${case_name}"

  if ! "${COMPILER_BIN}" -S -o "${asm_file}" "${case_file}"; then
    echo "compile failed: ${case_name}" >&2
    ng_count=$((ng_count + 1))
    continue
  fi

  if [[ ! -f "${asm_file}" ]]; then
    echo "assembly not generated: ${case_name}" >&2
    ng_count=$((ng_count + 1))
    continue
  fi

  if ! gcc -g -o "${bin_file}" "${asm_file}" "${RUNTIME_LIB}"; then
    echo "link failed: ${case_name}" >&2
    ng_count=$((ng_count + 1))
    continue
  fi

  set +e
  if [[ -f "${input_file}" ]]; then
    program_output="$("${bin_file}" < "${input_file}")"
  else
    program_output="$("${bin_file}")"
  fi
  exit_code=$?
  set -e

  if [[ -n "${program_output}" ]]; then
    printf "%s\n%s\n" "${program_output}" "${exit_code}" > "${result_file}"
  else
    printf "%s\n" "${exit_code}" > "${result_file}"
  fi

  if diff -a --strip-trailing-cr "${result_file}" "${output_file}" > /dev/null; then
    ok_count=$((ok_count + 1))
  else
    echo "mismatch: ${case_name}" >&2
    ng_count=$((ng_count + 1))
  fi
done

echo "OK number=${ok_count}, NG number=${ng_count}"

if [[ ${ng_count} -ne 0 ]]; then
  exit 1
fi
