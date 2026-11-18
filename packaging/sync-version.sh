#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

version="$(head -n 1 VERSION | tr -d '[:space:]')"
project="${version%-*}"
check=false
if [ "${1:-}" = "--check" ]; then
  check=true
fi

current_version="$(sed -n '1s/^spark-tui (\([^)]*\)).*/\1/p' debian/changelog 2>/dev/null || true)"
current_vcpkg="$(sed -n 's/.*"version-string": "\([^"]*\)".*/\1/p' vcpkg.json 2>/dev/null | head -n 1)"

if $check; then
  if [ "$current_version" != "$version" ]; then
    echo "debian/changelog is ${current_version:-missing}, expected ${version}" >&2
    exit 1
  fi
  if [ "$current_vcpkg" != "$project" ]; then
    echo "vcpkg.json is ${current_vcpkg:-missing}, expected ${project}" >&2
    exit 1
  fi
  exit 0
fi

if [ "$current_version" != "$version" ]; then
  tmp="$(mktemp)"
  cat >"$tmp" <<CHANGELOG
spark-tui (${version}) unstable; urgency=medium

  * Release ${version}.

 -- Jianyin Li <jianyin@sparkorg.dpdns.org>  $(date -R)
CHANGELOG
  if [ -s debian/changelog ]; then
    echo >>"$tmp"
    cat debian/changelog >>"$tmp"
  fi
  mv "$tmp" debian/changelog
fi

sed -i -E 's/"version-string": "[^"]*"/"version-string": "'"${project}"'"/' vcpkg.json
