#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
publisher="${repo_root}/.github/scripts/publish_release.sh"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

workspace="${test_root}/workspace with spaces"
mkdir -p "${workspace}/mock bin" "${workspace}/release files"

archive="${workspace}/release files/netft-cpp-1.2.3.tar.gz"
checksum="${archive}.sha256"
notes="${workspace}/release files/release notes.md"
log="${workspace}/gh.log"
standard_output="${workspace}/stdout.log"
standard_error="${workspace}/stderr.log"

printf 'archive contents\n' >"${archive}"
printf 'checksum contents\n' >"${checksum}"
printf 'release notes\n' >"${notes}"

cat >"${workspace}/mock bin/gh" <<'EOF'
#!/usr/bin/env bash

set -euo pipefail

{
  printf '%s' "$#"
  for argument in "$@"; do
    printf '\t%s' "${argument}"
  done
  printf '\n'
} >>"${GH_TEST_LOG}"

[[ "$1" == "release" ]]
command="$2"
shift 2

case "${command}" in
  view)
    case "${GH_TEST_STATE}" in
      absent)
        printf 'release not found\n' >&2
        exit 1
        ;;
      view-transient-error)
        printf 'temporary network failure while contacting api.github.com\n' >&2
        exit 1
        ;;
      draft | draft-upload-error)
        printf 'true\n'
        basename "${GH_TEST_ARCHIVE}"
        ;;
      published-complete | published-stale | published-download-error | published-download-incomplete)
        printf 'false\n'
        basename "${GH_TEST_ARCHIVE}"
        basename "${GH_TEST_CHECKSUM}"
        ;;
      published-missing)
        printf 'false\n'
        basename "${GH_TEST_ARCHIVE}"
        ;;
      *)
        exit 2
        ;;
    esac
    ;;
  download)
    download_dir=""
    while (($#)); do
      if [[ "$1" == "--dir" ]]; then
        download_dir="$2"
        shift 2
      else
        shift
      fi
    done
    [[ -n "${download_dir}" ]]
    mkdir -p "${download_dir}"
    case "${GH_TEST_STATE}" in
      published-complete)
        cp "${GH_TEST_ARCHIVE}" "${download_dir}/"
        cp "${GH_TEST_CHECKSUM}" "${download_dir}/"
        ;;
      published-stale)
        printf 'stale archive\n' >"${download_dir}/$(basename "${GH_TEST_ARCHIVE}")"
        printf 'stale checksum\n' >"${download_dir}/$(basename "${GH_TEST_CHECKSUM}")"
        ;;
      published-download-error)
        printf 'asset download failed\n' >&2
        exit 1
        ;;
      published-download-incomplete)
        cp "${GH_TEST_ARCHIVE}" "${download_dir}/"
        ;;
      *)
        exit 2
        ;;
    esac
    ;;
  upload)
    if [[ "${GH_TEST_STATE}" == draft-upload-error ]]; then
      printf 'asset upload failed\n' >&2
      exit 1
    fi
    ;;
  create | edit)
    ;;
  *)
    exit 2
    ;;
esac
EOF
chmod +x "${workspace}/mock bin/gh"

last_status=0
run_case() {
  local state="$1"
  : >"${log}"
  : >"${standard_output}"
  : >"${standard_error}"
  if GH_TEST_STATE="${state}" \
    GH_TEST_LOG="${log}" \
    GH_TEST_ARCHIVE="${archive}" \
    GH_TEST_CHECKSUM="${checksum}" \
    PATH="${workspace}/mock bin:${PATH}" \
    bash "${publisher}" v1.2.3 1.2.3 "${archive}" "${checksum}" "${notes}" \
      >"${standard_output}" 2>"${standard_error}"; then
    last_status=0
  else
    last_status=$?
  fi
}

assert_succeeded() {
  if ((last_status != 0)); then
    printf 'publisher unexpectedly failed with status %s\n' "${last_status}" >&2
    sed 's/^/  /' "${standard_error}" >&2
    exit 1
  fi
}

assert_failed() {
  if ((last_status == 0)); then
    printf 'publisher unexpectedly succeeded\n' >&2
    exit 1
  fi
}

assert_command() {
  local expected="$#"
  local argument
  for argument in "$@"; do
    expected+=$'\t'"${argument}"
  done
  if ! grep -Fxq -- "${expected}" "${log}"; then
    printf 'expected command with preserved arguments:\n  %s\n' "${expected}" >&2
    printf '%s\n' 'actual commands:' >&2
    sed 's/^/  /' "${log}" >&2
    exit 1
  fi
}

assert_operations() {
  local expected
  local actual
  expected="$(printf '%s\n' "$@")"
  actual="$(awk -F '\t' '$2 == "release" { print $3 }' "${log}")"
  if [[ "${actual}" != "${expected}" ]]; then
    printf 'unexpected operation sequence\nexpected:\n%s\nactual:\n%s\n' \
      "${expected}" "${actual}" >&2
    exit 1
  fi
}

assert_no_release_mutation() {
  if awk -F '\t' '$2 == "release" && ($3 == "create" || $3 == "edit" || $3 == "upload") {
    found = 1
  } END { exit !found }' "${log}"; then
    printf '%s\n' 'published release was unexpectedly mutated:' >&2
    sed 's/^/  /' "${log}" >&2
    exit 1
  fi
}

assert_error_contains() {
  local expected="$1"
  if ! grep -Fq -- "${expected}" "${standard_error}"; then
    printf 'expected stderr to contain: %s\n' "${expected}" >&2
    sed 's/^/  /' "${standard_error}" >&2
    exit 1
  fi
}

run_case absent
assert_succeeded
assert_operations view create upload edit
assert_command release create v1.2.3 --draft --verify-tag --title 'netft-cpp 1.2.3' \
  --notes-file "${notes}"
assert_command release upload v1.2.3 "${archive}" "${checksum}" --clobber
assert_command release edit v1.2.3 --draft=false

run_case draft
assert_succeeded
assert_operations view edit upload edit
assert_command release edit v1.2.3 --verify-tag --title 'netft-cpp 1.2.3' \
  --notes-file "${notes}"
assert_command release upload v1.2.3 "${archive}" "${checksum}" --clobber
assert_command release edit v1.2.3 --draft=false

run_case published-complete
assert_succeeded
assert_operations view download
assert_no_release_mutation

for state in published-missing published-stale published-download-error \
  published-download-incomplete; do
  run_case "${state}"
  assert_failed
  assert_no_release_mutation
  assert_error_contains 'manual intervention required'
done

run_case view-transient-error
assert_failed
assert_operations view
assert_no_release_mutation
assert_error_contains 'temporary network failure while contacting api.github.com'

run_case draft-upload-error
assert_failed
assert_operations view edit upload
assert_command release upload v1.2.3 "${archive}" "${checksum}" --clobber
if grep -Fq $'release\tedit\tv1.2.3\t--draft=false' "${log}"; then
  printf '%s\n' 'draft was published after an upload failure' >&2
  exit 1
fi

printf 'release publication simulations passed\n'
