#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TEST_ROOT="${REPO_ROOT}/test/contesttestcases"
RUNTIME_LIB="${TEST_ROOT}/lib/std.c"

MODE=""
SHOW_FAILURES=0
COMPILER_BIN="${COMPILER_BIN:-}"
MINIC_EXTRA_ARGS="${MINIC_EXTRA_ARGS:-}"
MINIC_TARGET="RISCV64"
LLVM_CC="${LLVM_CC:-}"

declare -a TARGET_SPECS=()
declare -a CASES=()
declare -a FAILED_CASES=()
declare -a FAILED_REASONS=()
declare -a EXTRA_ARGS=()
declare -a RUNNER_ARGS=()

show_usage() {
  cat <<'EOF'
Usage:
  scripts/run_ci_tests.sh <-ast|-ir|-asm> [--show-failures] [--compiler <path>] <target> [target...]

Targets:
  2023_function
  2023_func_00_main.c
  2023_func_00_main
  test/contesttestcases/2023_function/2023_func_00_main.c

Options:
  -ast               Test AST generation
  -ir                Test LLVM IR generation, local link, run and diff
  -asm               Test RISCV64 assembly generation and execution
  --compiler <path>  Override compiler binary path
  --show-failures    Print failed case names after summary
  -h, --help         Show this help message

Examples:
  scripts/run_ci_tests.sh -ir 2023_function
  scripts/run_ci_tests.sh -ast 2023_func_00_main.c
  scripts/run_ci_tests.sh -asm 2023_func_01_var_defn2.c 2023_func_02_var_defn3.c --show-failures
EOF
}

set_mode() {
  local new_mode="$1"
  if [[ -n "${MODE}" && "${MODE}" != "${new_mode}" ]]; then
    echo "Only one of -ast, -ir, -asm can be specified." >&2
    exit 1
  fi
  MODE="${new_mode}"
}

pick_first_command() {
  local cmd
  for cmd in "$@"; do
    if command -v "${cmd}" >/dev/null 2>&1; then
      printf '%s\n' "${cmd}"
      return 0
    fi
  done
  return 1
}

resolve_default_compiler() {
  local candidate

  if [[ -n "${COMPILER_BIN}" ]]; then
    if [[ ! -x "${COMPILER_BIN}" ]]; then
      echo "Compiler binary not found or not executable: ${COMPILER_BIN}" >&2
      exit 1
    fi
    return
  fi

  if [[ "${MODE}" == "asm" ]]; then
    for candidate in \
      "${REPO_ROOT}/build/compiler" \
      "${REPO_ROOT}/build-docker/compiler"; do
      if [[ -x "${candidate}" ]]; then
        COMPILER_BIN="${candidate}"
        return
      fi
    done
  else
    for candidate in \
      "${REPO_ROOT}/build-nobackend/compiler" \
      "${REPO_ROOT}/build-docker-nobackend/compiler" \
      "${REPO_ROOT}/build/compiler" \
      "${REPO_ROOT}/build-docker/compiler"; do
      if [[ -x "${candidate}" ]]; then
        COMPILER_BIN="${candidate}"
        return
      fi
    done
  fi

  echo "No suitable compiler binary found. Use --compiler <path>." >&2
  exit 1
}

add_case_if_new() {
  local case_file="$1"
  local existing

  if [[ ${#CASES[@]} -gt 0 ]]; then
    for existing in "${CASES[@]}"; do
      if [[ "${existing}" == "${case_file}" ]]; then
        return
      fi
    done
  fi

  CASES+=("${case_file}")
}

add_cases_from_dir() {
  local dir_path="$1"
  local found=0
  local case_file

  while IFS= read -r case_file; do
    add_case_if_new "${case_file}"
    found=1
  done < <(find "${dir_path}" -maxdepth 1 -type f -name "*.c" | LC_ALL=C sort)

  if [[ ${found} -eq 0 ]]; then
    echo "No test cases found in directory: ${dir_path}" >&2
    exit 1
  fi
}

infer_case_dir() {
  local case_name="$1"

  if [[ "${case_name}" =~ ^([0-9]{4})_func_ ]]; then
    printf '%s/%s_function\n' "${TEST_ROOT}" "${BASH_REMATCH[1]}"
    return 0
  fi

  if [[ "${case_name}" =~ ^([0-9]{4})_perf_ ]]; then
    printf '%s/%s_performance\n' "${TEST_ROOT}" "${BASH_REMATCH[1]}"
    return 0
  fi

  return 1
}

resolve_case_token() {
  local token="$1"
  local normalized="${token%/}"
  local case_name=""
  local dir_path=""
  local candidate=""
  local matches=""
  local match_count=0

  if [[ -d "${normalized}" ]]; then
    add_cases_from_dir "${normalized}"
    return
  fi

  if [[ -d "${TEST_ROOT}/${normalized}" ]]; then
    add_cases_from_dir "${TEST_ROOT}/${normalized}"
    return
  fi

  if [[ -f "${normalized}" ]]; then
    add_case_if_new "$(cd "$(dirname "${normalized}")" && pwd)/$(basename "${normalized}")"
    return
  fi

  if [[ -f "${TEST_ROOT}/${normalized}" ]]; then
    add_case_if_new "${TEST_ROOT}/${normalized}"
    return
  fi

  case_name="$(basename "${normalized}")"
  case_name="${case_name%.c}"

  if dir_path="$(infer_case_dir "${case_name}")"; then
    candidate="${dir_path}/${case_name}.c"
    if [[ -f "${candidate}" ]]; then
      add_case_if_new "${candidate}"
      return
    fi
  fi

  matches="$(find "${TEST_ROOT}" -maxdepth 2 -type f -name "${case_name}.c" | LC_ALL=C sort)"
  if [[ -n "${matches}" ]]; then
    while IFS= read -r candidate; do
      [[ -z "${candidate}" ]] && continue
      match_count=$((match_count + 1))
      dir_path="${candidate}"
    done <<EOF
${matches}
EOF

    if [[ ${match_count} -eq 1 ]]; then
      add_case_if_new "${dir_path}"
      return
    fi

    echo "Ambiguous test case token: ${token}" >&2
    echo "${matches}" >&2
    exit 1
  fi

  echo "Cannot resolve test target: ${token}" >&2
  exit 1
}

record_failure() {
  local case_name="$1"
  local reason="$2"

  FAILED_CASES+=("${case_name}")
  FAILED_REASONS+=("${reason}")
}

setup_asm_tools() {
  local default_asm_cc=""
  local default_asm_runner=""

  default_asm_cc="$(pick_first_command riscv64-linux-gnu-gcc || true)"
  default_asm_runner="$(pick_first_command qemu-riscv64-static qemu-riscv64 || true)"

  ASM_CC="${ASM_CC:-${default_asm_cc}}"
  ASM_RUNNER="${ASM_RUNNER:-${default_asm_runner}}"

  if [[ -d /usr/riscv64-linux-gnu ]]; then
    RUNNER_ARGS=(-L /usr/riscv64-linux-gnu)
  fi

  if [[ ! -f "${RUNTIME_LIB}" ]]; then
    echo "Runtime library not found: ${RUNTIME_LIB}" >&2
    exit 1
  fi
}

setup_ir_tools() {
  local default_llvm_cc=""

  default_llvm_cc="$(pick_first_command clang cc || true)"
  LLVM_CC="${LLVM_CC:-${default_llvm_cc}}"

  if [[ -z "${LLVM_CC}" ]]; then
    echo "No local C compiler found for LLVM IR testing. Install clang or set LLVM_CC." >&2
    exit 1
  fi

  if ! command -v "${LLVM_CC}" >/dev/null 2>&1; then
    echo "LLVM_CC not found: ${LLVM_CC}" >&2
    exit 1
  fi

  if [[ ! -f "${RUNTIME_LIB}" ]]; then
    echo "Runtime library not found: ${RUNTIME_LIB}" >&2
    exit 1
  fi
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -ast)
      set_mode ast
      ;;
    -ir)
      set_mode ir
      ;;
    -asm)
      set_mode asm
      ;;
    --show-failures)
      SHOW_FAILURES=1
      ;;
    --compiler)
      shift
      if [[ $# -eq 0 ]]; then
        echo "--compiler requires a path argument." >&2
        exit 1
      fi
      COMPILER_BIN="$1"
      ;;
    -h|--help)
      show_usage
      exit 0
      ;;
    --)
      shift
      while [[ $# -gt 0 ]]; do
        TARGET_SPECS+=("$1")
        shift
      done
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      exit 1
      ;;
    *)
      TARGET_SPECS+=("$1")
      ;;
  esac
  shift
done

if [[ -z "${MODE}" ]]; then
  echo "You must specify one of -ast, -ir, -asm." >&2
  exit 1
fi

if [[ ${#TARGET_SPECS[@]} -eq 0 ]]; then
  show_usage >&2
  exit 1
fi

if [[ -n "${MINIC_EXTRA_ARGS}" ]]; then
  # Intentionally split shell words so callers can pass multiple flags.
  # shellcheck disable=SC2206
  EXTRA_ARGS=(${MINIC_EXTRA_ARGS})
fi

resolve_default_compiler

if [[ "${MODE}" == "asm" ]]; then
  setup_asm_tools
elif [[ "${MODE}" == "ir" ]]; then
  setup_ir_tools
fi

for token in "${TARGET_SPECS[@]}"; do
  resolve_case_token "${token}"
done

if [[ ${#CASES[@]} -eq 0 ]]; then
  echo "No test cases resolved." >&2
  exit 1
fi

workdir="$(mktemp -d)"
trap 'rm -rf "${workdir}"' EXIT

ok_count=0
ng_count=0

for case_file in "${CASES[@]}"; do
  case_name="$(basename "${case_file}" .c)"
  case_dir="$(cd "$(dirname "${case_file}")" && pwd)"
  input_file="${case_dir}/${case_name}.in"
  output_file="${case_dir}/${case_name}.out"

  case "${MODE}" in
    asm)
      artifact_file="${workdir}/${case_name}.s"
      ;;
    ir)
      artifact_file="${workdir}/${case_name}.ll"
      ;;
    ast)
      artifact_file="${workdir}/${case_name}.png"
      ;;
  esac

  echo "Running ${case_name} (${MODE})"

  compiler_cmd=("${COMPILER_BIN}" -S)
  case "${MODE}" in
    asm)
      compiler_cmd+=(-t "${MINIC_TARGET}")
      ;;
    ir)
      compiler_cmd+=(-L)
      ;;
    ast)
      compiler_cmd+=(-T)
      ;;
  esac
  if [[ ${#EXTRA_ARGS[@]} -gt 0 ]]; then
    compiler_cmd+=("${EXTRA_ARGS[@]}")
  fi
  compiler_cmd+=(-o "${artifact_file}" "${case_file}")

  if ! "${compiler_cmd[@]}"; then
    echo "compile failed: ${case_name}" >&2
    record_failure "${case_name}" "compile failed"
    ng_count=$((ng_count + 1))
    continue
  fi

  if [[ ! -f "${artifact_file}" ]]; then
    echo "artifact not generated: ${artifact_file}" >&2
    record_failure "${case_name}" "artifact not generated"
    ng_count=$((ng_count + 1))
    continue
  fi

  if [[ "${MODE}" == "ast" ]]; then
    ok_count=$((ok_count + 1))
    continue
  fi

  if [[ ! -f "${output_file}" ]]; then
    echo "expected output file not found: ${output_file}" >&2
    record_failure "${case_name}" "missing expected output"
    ng_count=$((ng_count + 1))
    continue
  fi

  bin_file="${workdir}/${case_name}"
  result_file="${workdir}/${case_name}.result"

  if [[ "${MODE}" == "ir" ]]; then
    if ! "${LLVM_CC}" -o "${bin_file}" "${artifact_file}" "${RUNTIME_LIB}"; then
      echo "link failed: ${case_name}" >&2
      record_failure "${case_name}" "LLVM IR link failed"
      ng_count=$((ng_count + 1))
      continue
    fi

    run_cmd=("${bin_file}")
  else

    if [[ -z "${ASM_CC}" ]]; then
      echo "ASM_CC not found. Install riscv64-linux-gnu-gcc or set ASM_CC." >&2
      record_failure "${case_name}" "missing assembler/linker"
      ng_count=$((ng_count + 1))
      continue
    fi

    if ! command -v "${ASM_CC}" >/dev/null 2>&1; then
      echo "ASM_CC not found: ${ASM_CC}" >&2
      record_failure "${case_name}" "invalid ASM_CC"
      ng_count=$((ng_count + 1))
      continue
    fi

    if ! "${ASM_CC}" -g -o "${bin_file}" "${artifact_file}" "${RUNTIME_LIB}"; then
      echo "link failed: ${case_name}" >&2
      record_failure "${case_name}" "link failed"
      ng_count=$((ng_count + 1))
      continue
    fi

    run_cmd=("${bin_file}")
    if [[ -n "${ASM_RUNNER}" ]]; then
      if ! command -v "${ASM_RUNNER}" >/dev/null 2>&1; then
        echo "ASM_RUNNER not found: ${ASM_RUNNER}" >&2
        record_failure "${case_name}" "invalid ASM_RUNNER"
        ng_count=$((ng_count + 1))
        continue
      fi
      run_cmd=("${ASM_RUNNER}")
      if [[ ${#RUNNER_ARGS[@]} -gt 0 ]]; then
        run_cmd+=("${RUNNER_ARGS[@]}")
      fi
      run_cmd+=("${bin_file}")
    fi
  fi

  set +e
  if [[ -f "${input_file}" ]]; then
    "${run_cmd[@]}" < "${input_file}" > "${result_file}"
  else
    "${run_cmd[@]}" > "${result_file}"
  fi
  exit_code=$?
  set -e

  printf "%s\n" "${exit_code}" >> "${result_file}"

  if diff -a --strip-trailing-cr "${result_file}" "${output_file}" >/dev/null; then
    ok_count=$((ok_count + 1))
  else
    echo "mismatch: ${case_name}" >&2
    record_failure "${case_name}" "output mismatch"
    ng_count=$((ng_count + 1))
  fi
done

echo "OK number=${ok_count}, NG number=${ng_count}"

if [[ ${SHOW_FAILURES} -eq 1 && ${#FAILED_CASES[@]} -gt 0 ]]; then
  echo "Failed cases:"
  for i in "${!FAILED_CASES[@]}"; do
    printf "  %s : %s\n" "${FAILED_CASES[$i]}" "${FAILED_REASONS[$i]}"
  done
fi

if [[ ${ng_count} -ne 0 ]]; then
  exit 1
fi
