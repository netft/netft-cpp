#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workflow="${repo_root}/.github/workflows/codeql.yml"
expected="+codeql/cpp-queries:AlertSuppression.ql"

packs="$(
  yq -r \
    '.jobs.analyze.steps[] | select(.uses == "github/codeql-action/init@v4") | .with.packs' \
    "${workflow}"
)"

queries="$(
  yq -r \
    '.jobs.analyze.steps[] | select(.uses == "github/codeql-action/init@v4") | .with.queries' \
    "${workflow}"
)"

if [[ "${packs}" != "${expected}" ]]; then
  printf 'CodeQL init packs must append the alert-suppression query\n' >&2
  exit 1
fi

if [[ "${queries}" != "null" ]]; then
  printf 'CodeQL init queries must remain unset\n' >&2
  exit 1
fi
