#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
TEST_ROOT="${REPO_ROOT}/test/contesttestcases"
RUNTIME_LIB="${TEST_ROOT}/lib/std.c"
DEFAULT_TEST_PLAN_FILE="${SCRIPT_DIR}/ci_test_plan.txt"

MODE=""
RUN_ALL=0
SHOW_FAILURES=0
COMPILER_BIN="${COMPILER_BIN:-}"
TEST_PLAN_FILE="${TEST_PLAN_FILE:-${DEFAULT_TEST_PLAN_FILE}}"
MINIC_EXTRA_ARGS="${MINIC_EXTRA_ARGS:-}"
MINIC_TARGET="RISCV64"
LLVM_CC="${LLVM_CC:-}"
ASM_CC="${ASM_CC:-}"
ASM_RUNNER="${ASM_RUNNER:-}"

declare -a TARGET_SPECS=()
declare -a CASES=()
declare -a FAILED_CASES=()
declare -a FAILED_REASONS=()
declare -a EXTRA_ARGS=()
declare -a RUNNER_ARGS=()

declare -a PLAN_AST_TARGETS=()
declare -a PLAN_IR_TARGETS=()
declare -a PLAN_ASM_TARGETS=()

SUITE_OK_COUNT=0
SUITE_NG_COUNT=0
TOTAL_OK_COUNT=0
TOTAL_NG_COUNT=0

declare -a ALL_FAILED_CASES=()
declare -a ALL_FAILED_REASONS=()

WORK_ROOT=""
QUIET_ALL_SUCCESS=0
USE_COLOR=0

show_usage() {
  cat <<'EOF'
Usage:
  scripts/run_ci_tests.sh <-ast|-ir|-asm> [--show-failures] [--compiler <path>] <target> [target...]
  scripts/run_ci_tests.sh [<-ast|-ir|-asm>] --all [--show-failures] [--compiler <path>] [--test-plan <path>]

Targets:
  2023_function
  2023_func_00_main.c
  2023_func_00_main
  test/contesttestcases/2023_function/2023_func_00_main.c

Options:
  -ast                Test AST generation
  -ir                 Test LLVM IR generation, local link, run and diff
  -asm                Test RISCV64 assembly generation and execution
  --all               Run tests listed in the test plan file
  --test-plan <path>  Override the default test plan file
  --compiler <path>   Override compiler binary path
  --show-failures     Print failed case names after summary
  -h, --help          Show this help message

Examples:
  scripts/run_ci_tests.sh -ir 2023_function
  scripts/run_ci_tests.sh -ast 2023_func_00_main.c
  scripts/run_ci_tests.sh -asm 2023_func_01_var_defn2.c 2023_func_02_var_defn3.c --show-failures
  scripts/run_ci_tests.sh --all
  scripts/run_ci_tests.sh -asm --all --test-plan scripts/ci_test_plan.txt
EOF
}

run_logged_command() {
  if [[ ${QUIET_ALL_SUCCESS} -eq 1 ]]; then
    "$@" >/dev/null 2>&1
  else
    "$@"
  fi
}

color_text() {
  local color_code="$1"
  shift

  if [[ ${USE_COLOR} -eq 1 ]]; then
    printf '\033[%sm%s\033[0m' "${color_code}" "$*"
  else
    printf '%s' "$*"
  fi
}

green_text() {
  color_text "32" "$*"
}

red_text() {
  color_text "31" "$*"
}

suite_title() {
  case "$1" in
    ast) printf 'ast tests:' ;;
    ir) printf 'ir tests:' ;;
    asm) printf 'asm tests:' ;;
    *) printf '%s tests:' "$1" ;;
  esac
}

mode_label() {
  case "$1" in
    ast) printf 'AST\n' ;;
    ir) printf 'IR\n' ;;
    asm) printf 'ASM\n' ;;
    *) printf '%s\n' "$1" ;;
  esac
}

trim_line() {
  local line="$1"
  line="${line#"${line%%[![:space:]]*}"}"
  line="${line%"${line##*[![:space:]]}"}"
  printf '%s\n' "${line}"
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

resolve_compiler_for_mode() {
  local suite_mode="$1"
  local candidate

  if [[ -n "${COMPILER_BIN}" ]]; then
    if [[ ! -x "${COMPILER_BIN}" ]]; then
      echo "Compiler binary not found or not executable: ${COMPILER_BIN}" >&2
      exit 1
    fi
    printf '%s\n' "${COMPILER_BIN}"
    return 0
  fi

  if [[ "${suite_mode}" == "asm" ]]; then
    for candidate in \
      "${REPO_ROOT}/build/compiler" \
      "${REPO_ROOT}/build-docker/compiler" \
      "${REPO_ROOT}/build-docker-backend/compiler"; do
      if [[ -x "${candidate}" ]]; then
        printf '%s\n' "${candidate}"
        return 0
      fi
    done
  else
    for candidate in \
      "${REPO_ROOT}/build-nobackend/compiler" \
      "${REPO_ROOT}/build-docker-nobackend/compiler" \
      "${REPO_ROOT}/build-docker-nobackend2/compiler" \
      "${REPO_ROOT}/build/compiler" \
      "${REPO_ROOT}/build-docker/compiler" \
      "${REPO_ROOT}/build-docker-backend/compiler"; do
      if [[ -x "${candidate}" ]]; then
        printf '%s\n' "${candidate}"
        return 0
      fi
    done
  fi

  echo "No suitable compiler binary found for $(mode_label "${suite_mode}"). Use --compiler <path>." >&2
  exit 1
}

record_failure() {
  local case_name="$1"
  local reason="$2"

  FAILED_CASES+=("${case_name}")
  FAILED_REASONS+=("${reason}")
}

print_case_failure() {
  local case_name="$1"
  local reason="$2"

  if [[ ${QUIET_ALL_SUCCESS} -eq 0 ]]; then
    printf '%s: %s\n' "${reason}" "${case_name}" >&2
  fi
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

  if [[ -d "${REPO_ROOT}/${normalized}" ]]; then
    add_cases_from_dir "${REPO_ROOT}/${normalized}"
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

  if [[ -f "${REPO_ROOT}/${normalized}" ]]; then
    add_case_if_new "${REPO_ROOT}/${normalized}"
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

setup_asm_tools() {
  local default_asm_cc=""
  local default_asm_runner=""

  default_asm_cc="$(pick_first_command riscv64-linux-gnu-gcc || true)"
  default_asm_runner="$(pick_first_command qemu-riscv64-static qemu-riscv64 || true)"

  ASM_CC="${ASM_CC:-${default_asm_cc}}"
  ASM_RUNNER="${ASM_RUNNER:-${default_asm_runner}}"

  RUNNER_ARGS=()
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

append_plan_target() {
  local section="$1"
  local token="$2"

  case "${section}" in
    ast) PLAN_AST_TARGETS+=("${token}") ;;
    ir) PLAN_IR_TARGETS+=("${token}") ;;
    asm) PLAN_ASM_TARGETS+=("${token}") ;;
    *)
      echo "Unknown test plan section: ${section}" >&2
      exit 1
      ;;
  esac
}

load_test_plan() {
  local current_section=""
  local raw_line=""
  local line=""

  if [[ ! -f "${TEST_PLAN_FILE}" ]]; then
    echo "Test plan file not found: ${TEST_PLAN_FILE}" >&2
    exit 1
  fi

  PLAN_AST_TARGETS=()
  PLAN_IR_TARGETS=()
  PLAN_ASM_TARGETS=()

  while IFS= read -r raw_line || [[ -n "${raw_line}" ]]; do
    line="$(trim_line "${raw_line}")"

    if [[ -z "${line}" || "${line}" == \#* ]]; then
      continue
    fi

    if [[ "${line}" =~ ^(ast|ir|asm)[[:space:]]*:$ ]]; then
      current_section="${BASH_REMATCH[1]}"
      continue
    fi

    if [[ -z "${current_section}" ]]; then
      echo "Test plan entry appears before a section header: ${line}" >&2
      exit 1
    fi

    append_plan_target "${current_section}" "${line}"
  done < "${TEST_PLAN_FILE}"

  if [[ ${#PLAN_AST_TARGETS[@]} -eq 0 && ${#PLAN_IR_TARGETS[@]} -eq 0 && ${#PLAN_ASM_TARGETS[@]} -eq 0 ]]; then
    echo "No test targets found in plan file: ${TEST_PLAN_FILE}" >&2
    exit 1
  fi
}

run_mode_suite() {
  local suite_mode="$1"
  shift

  local compiler_path=""
  local workdir=""
  local case_file=""
  local case_name=""
  local case_dir=""
  local input_file=""
  local output_file=""
  local artifact_file=""
  local bin_file=""
  local result_file=""
  local exit_code=0

  local -a compiler_cmd=()
  local -a run_cmd=()

  CASES=()
  FAILED_CASES=()
  FAILED_REASONS=()
  SUITE_OK_COUNT=0
  SUITE_NG_COUNT=0

  if [[ $# -eq 0 ]]; then
    echo "No targets specified for $(mode_label "${suite_mode}")." >&2
    exit 1
  fi

  compiler_path="$(resolve_compiler_for_mode "${suite_mode}")"

  case "${suite_mode}" in
    asm) setup_asm_tools ;;
    ir) setup_ir_tools ;;
  esac

  for case_file in "$@"; do
    resolve_case_token "${case_file}"
  done

  if [[ ${#CASES[@]} -eq 0 ]]; then
    echo "No test cases resolved for $(mode_label "${suite_mode}")." >&2
    exit 1
  fi

  workdir="$(mktemp -d "${WORK_ROOT}/${suite_mode}.XXXXXX")"

  for case_file in "${CASES[@]}"; do
    case_name="$(basename "${case_file}" .c)"
    case_dir="$(cd "$(dirname "${case_file}")" && pwd)"
    input_file="${case_dir}/${case_name}.in"
    output_file="${case_dir}/${case_name}.out"

    case "${suite_mode}" in
      asm) artifact_file="${workdir}/${case_name}.s" ;;
      ir) artifact_file="${workdir}/${case_name}.ll" ;;
      ast) artifact_file="${workdir}/${case_name}.png" ;;
    esac

    if [[ ${QUIET_ALL_SUCCESS} -eq 0 ]]; then
      echo "Running ${case_name} (${suite_mode})"
    fi

    compiler_cmd=("${compiler_path}" -S)
    case "${suite_mode}" in
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

    if ! run_logged_command "${compiler_cmd[@]}"; then
      print_case_failure "${case_name}" "compile failed"
      record_failure "${case_name}" "compile failed"
      SUITE_NG_COUNT=$((SUITE_NG_COUNT + 1))
      continue
    fi

    if [[ ! -f "${artifact_file}" ]]; then
      print_case_failure "${case_name}" "artifact not generated"
      record_failure "${case_name}" "artifact not generated"
      SUITE_NG_COUNT=$((SUITE_NG_COUNT + 1))
      continue
    fi

    if [[ "${suite_mode}" == "ast" ]]; then
      SUITE_OK_COUNT=$((SUITE_OK_COUNT + 1))
      continue
    fi

    if [[ ! -f "${output_file}" ]]; then
      print_case_failure "${case_name}" "missing expected output"
      record_failure "${case_name}" "missing expected output"
      SUITE_NG_COUNT=$((SUITE_NG_COUNT + 1))
      continue
    fi

    bin_file="${workdir}/${case_name}"
    result_file="${workdir}/${case_name}.result"

    if [[ "${suite_mode}" == "ir" ]]; then
      if ! run_logged_command "${LLVM_CC}" -o "${bin_file}" "${artifact_file}" "${RUNTIME_LIB}"; then
        print_case_failure "${case_name}" "link failed"
        record_failure "${case_name}" "LLVM IR link failed"
        SUITE_NG_COUNT=$((SUITE_NG_COUNT + 1))
        continue
      fi

      run_cmd=("${bin_file}")
    else
      if [[ -z "${ASM_CC}" ]]; then
        print_case_failure "${case_name}" "missing assembler/linker"
        record_failure "${case_name}" "missing assembler/linker"
        SUITE_NG_COUNT=$((SUITE_NG_COUNT + 1))
        continue
      fi

      if ! command -v "${ASM_CC}" >/dev/null 2>&1; then
        print_case_failure "${case_name}" "invalid ASM_CC"
        record_failure "${case_name}" "invalid ASM_CC"
        SUITE_NG_COUNT=$((SUITE_NG_COUNT + 1))
        continue
      fi

      if ! run_logged_command "${ASM_CC}" -g -o "${bin_file}" "${artifact_file}" "${RUNTIME_LIB}"; then
        print_case_failure "${case_name}" "link failed"
        record_failure "${case_name}" "link failed"
        SUITE_NG_COUNT=$((SUITE_NG_COUNT + 1))
        continue
      fi

      run_cmd=("${bin_file}")
      if [[ -n "${ASM_RUNNER}" ]]; then
        if ! command -v "${ASM_RUNNER}" >/dev/null 2>&1; then
          print_case_failure "${case_name}" "invalid ASM_RUNNER"
          record_failure "${case_name}" "invalid ASM_RUNNER"
          SUITE_NG_COUNT=$((SUITE_NG_COUNT + 1))
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
      if [[ ${QUIET_ALL_SUCCESS} -eq 1 ]]; then
        "${run_cmd[@]}" < "${input_file}" > "${result_file}" 2>/dev/null
      else
        "${run_cmd[@]}" < "${input_file}" > "${result_file}"
      fi
  else
      if [[ ${QUIET_ALL_SUCCESS} -eq 1 ]]; then
        "${run_cmd[@]}" > "${result_file}" 2>/dev/null
      else
        "${run_cmd[@]}" > "${result_file}"
      fi
  fi
  exit_code=$?
  set -e

    printf "%s\n" "${exit_code}" >> "${result_file}"

    if diff -a --strip-trailing-cr "${result_file}" "${output_file}" >/dev/null; then
      SUITE_OK_COUNT=$((SUITE_OK_COUNT + 1))
    else
      print_case_failure "${case_name}" "mismatch"
      record_failure "${case_name}" "output mismatch"
      SUITE_NG_COUNT=$((SUITE_NG_COUNT + 1))
    fi
  done
}

run_plan_section() {
  local suite_mode="$1"
  local i=0
  local -a suite_targets=()

  case "${suite_mode}" in
    ast) suite_targets=("${PLAN_AST_TARGETS[@]}") ;;
    ir) suite_targets=("${PLAN_IR_TARGETS[@]}") ;;
    asm) suite_targets=("${PLAN_ASM_TARGETS[@]}") ;;
  esac

  printf '%s\n' "$(suite_title "${suite_mode}")"

  if [[ ${#suite_targets[@]} -eq 0 ]]; then
    echo "no tests configured"
    printf '%s number=%d, %s number=%d\n' \
      "$(green_text "OK")" 0 \
      "$(red_text "NG")" 0
    echo
    return
  fi

  run_mode_suite "${suite_mode}" "${suite_targets[@]}"

  if [[ ${SUITE_NG_COUNT} -eq 0 ]]; then
    printf '%s\n' "$(green_text "all passed")"
  else
    for i in "${!FAILED_CASES[@]}"; do
      printf '%s %s\n' "$(red_text "failed:")" "${FAILED_CASES[$i]}"
    done
  fi
  printf '%s number=%d, %s number=%d\n' \
    "$(green_text "OK")" "${SUITE_OK_COUNT}" \
    "$(red_text "NG")" "${SUITE_NG_COUNT}"
  echo

  TOTAL_OK_COUNT=$((TOTAL_OK_COUNT + SUITE_OK_COUNT))
  TOTAL_NG_COUNT=$((TOTAL_NG_COUNT + SUITE_NG_COUNT))

  if [[ ${#FAILED_CASES[@]} -gt 0 ]]; then
    for i in "${!FAILED_CASES[@]}"; do
      ALL_FAILED_CASES+=("${suite_mode}:${FAILED_CASES[$i]}")
      ALL_FAILED_REASONS+=("${FAILED_REASONS[$i]}")
    done
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
    --all)
      RUN_ALL=1
      ;;
    --test-plan)
      shift
      if [[ $# -eq 0 ]]; then
        echo "--test-plan requires a path argument." >&2
        exit 1
      fi
      TEST_PLAN_FILE="$1"
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

if [[ -n "${MINIC_EXTRA_ARGS}" ]]; then
  # Intentionally split shell words so callers can pass multiple flags.
  # shellcheck disable=SC2206
  EXTRA_ARGS=(${MINIC_EXTRA_ARGS})
fi

if [[ ${RUN_ALL} -eq 1 && ${#TARGET_SPECS[@]} -gt 0 ]]; then
  echo "--all cannot be combined with explicit test targets." >&2
  exit 1
fi

if [[ ${RUN_ALL} -eq 0 ]]; then
  if [[ -z "${MODE}" ]]; then
    echo "You must specify one of -ast, -ir, -asm." >&2
    exit 1
  fi

  if [[ ${#TARGET_SPECS[@]} -eq 0 ]]; then
    show_usage >&2
    exit 1
  fi
fi

WORK_ROOT="$(mktemp -d)"
trap 'rm -rf "${WORK_ROOT}"' EXIT

if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
  USE_COLOR=1
fi

if [[ ${RUN_ALL} -eq 1 ]]; then
  QUIET_ALL_SUCCESS=1
  load_test_plan

  if [[ -n "${MODE}" ]]; then
    run_plan_section "${MODE}"
  else
    run_plan_section ast
    run_plan_section ir
    run_plan_section asm
  fi

  if [[ ${TOTAL_NG_COUNT} -ne 0 ]]; then
    exit 1
  fi

  exit 0
fi

run_mode_suite "${MODE}" "${TARGET_SPECS[@]}"

echo "OK number=${SUITE_OK_COUNT}, NG number=${SUITE_NG_COUNT}"

if [[ ${SHOW_FAILURES} -eq 1 && ${#FAILED_CASES[@]} -gt 0 ]]; then
  echo "Failed cases:"
  for i in "${!FAILED_CASES[@]}"; do
    printf "  %s : %s\n" "${FAILED_CASES[$i]}" "${FAILED_REASONS[$i]}"
  done
fi

if [[ ${SUITE_NG_COUNT} -ne 0 ]]; then
  exit 1
fi
