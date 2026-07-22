#!/usr/bin/env bash

set -euo pipefail

if (($# != 5)); then
  printf 'usage: %s TAG VERSION ARCHIVE CHECKSUM NOTES_FILE\n' "$0" >&2
  exit 2
fi

tag="$1"
version="$2"
archive="$3"
checksum="$4"
notes_file="$5"
title="netft-cpp ${version}"
archive_name="$(basename "${archive}")"
checksum_name="$(basename "${checksum}")"

for required_file in "${archive}" "${checksum}" "${notes_file}"; do
  if [[ ! -f "${required_file}" ]]; then
    printf 'required release file does not exist: %s\n' "${required_file}" >&2
    exit 2
  fi
done

release_state=absent
release_info=""
work_dir="$(mktemp -d)"
cleanup() {
  if [[ -n "${work_dir}" && "${work_dir}" != / && -d "${work_dir}" ]]; then
    rm -rf -- "${work_dir}"
  fi
}
trap cleanup EXIT

view_error="${work_dir}/release-view.stderr"
if release_info="$(
  gh release view "${tag}" \
    --json isDraft,assets \
    --jq '.isDraft, (.assets[] | .name)' 2>"${view_error}"
)"; then
  mapfile -t release_lines <<<"${release_info}"
  is_draft="${release_lines[0]:-}"
  case "${is_draft}" in
    true)
      release_state=draft
      ;;
    false)
      release_state=published
      ;;
    *)
      printf 'unexpected draft state for release %s: %s\n' "${tag}" "${is_draft}" >&2
      exit 1
      ;;
  esac
else
  view_status=$?
  if grep -Eiq 'release not found|HTTP 404|404 Not Found' "${view_error}"; then
    release_state=absent
  else
    printf 'could not inspect release %s; no release was created or modified\n' "${tag}" >&2
    if [[ -s "${view_error}" ]]; then
      cat "${view_error}" >&2
    fi
    exit "${view_status}"
  fi
fi

if [[ "${release_state}" == published ]]; then
  release_assets=("${release_lines[@]:1}")
  for expected_asset in "${archive_name}" "${checksum_name}"; do
    asset_found=false
    for existing_asset in "${release_assets[@]}"; do
      if [[ "${existing_asset}" == "${expected_asset}" ]]; then
        asset_found=true
        break
      fi
    done
    if [[ "${asset_found}" != true ]]; then
      printf 'manual intervention required: published release %s is missing asset %s; no changes were made\n' \
        "${tag}" "${expected_asset}" >&2
      exit 1
    fi
  done

  download_dir="${work_dir}/published assets"
  mkdir -p "${download_dir}"
  if ! gh release download "${tag}" \
    --pattern "${archive_name}" \
    --pattern "${checksum_name}" \
    --dir "${download_dir}"; then
    printf 'manual intervention required: assets for published release %s could not be downloaded; no changes were made\n' \
      "${tag}" >&2
    exit 1
  fi
  for expected_asset in "${archive_name}" "${checksum_name}"; do
    if [[ ! -f "${download_dir}/${expected_asset}" ]]; then
      printf 'manual intervention required: asset download for published release %s was incomplete; no changes were made\n' \
        "${tag}" >&2
      exit 1
    fi
  done
  if ! cmp -s "${archive}" "${download_dir}/${archive_name}"; then
    printf 'manual intervention required: archive asset for published release %s differs from the local archive; no changes were made\n' \
      "${tag}" >&2
    exit 1
  fi
  if ! cmp -s "${checksum}" "${download_dir}/${checksum_name}"; then
    printf 'manual intervention required: checksum asset for published release %s differs from the local checksum; no changes were made\n' \
      "${tag}" >&2
    exit 1
  fi
  printf 'release %s is already published with byte-identical assets\n' "${tag}"
  exit 0
fi

if [[ "${release_state}" == absent ]]; then
  gh release create "${tag}" \
    --draft \
    --verify-tag \
    --title "${title}" \
    --notes-file "${notes_file}"
else
  gh release edit "${tag}" \
    --verify-tag \
    --title "${title}" \
    --notes-file "${notes_file}"
fi

gh release upload "${tag}" "${archive}" "${checksum}" --clobber
gh release edit "${tag}" --draft=false
