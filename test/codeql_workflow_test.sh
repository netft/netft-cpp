#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workflow="${repo_root}/.github/workflows/codeql.yml"
expected="+codeql/cpp-queries:AlertSuppression.ql"

queries="$(
  yq -r \
    '.jobs.analyze.steps[] | select(.uses == "github/codeql-action/init@v4") | .with.queries' \
    "${workflow}"
)"

if [[ "${queries}" != "${expected}" ]]; then
  printf 'CodeQL init queries must append the alert-suppression query\n' >&2
  exit 1
fi
