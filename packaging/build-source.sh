#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

version="${1:-$(head -n 1 "$root/VERSION" | tr -d '[:space:]')}"
out="${2:-$root/build/source}"
ref="${3:-HEAD}"
source_name="$(sed -n 's/^Source:[[:space:]]*//p' "$root/debian/control" | head -n 1)"

if [ -z "$version" ] || [ -z "$source_name" ]; then
  echo "cannot determine Debian version or source name" >&2
  exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# The project uses native Debian packaging (VERSION is a plain SemVer such as
# 0.3.0), so debian/ is part of the single source tarball.
stage="$tmp/${source_name}-${version}"
mkdir -p "$stage"
git archive --format=tar "$ref" | tar -x -C "$stage"

# Never leak debhelper build artifacts from the working tree. git archive only
# contains tracked files, so this is normally a no-op.
rm -rf "$stage/debian/.debhelper" "$stage/debian/tmp" \
       "$stage/debian/files" "$stage/debian/debhelper-build-stamp" \
       "$stage/debian/sparkapp-core" "$stage/debian/sparkapp-tui" \
       "$stage/debian/sparkapp-gui" \
       "$stage"/debian/*.substvars "$stage"/debian/*.debhelper.log
sed -i "1s/^[^(]* ([^)]*)/${source_name} (${version})/" "$stage/debian/changelog"

(
  cd "$stage"
  # -S: source package only; -d: build deps are already present when needed;
  # -nc: the staging tree is clean, so no debhelper clean hooks.
  dpkg-buildpackage -S -us -uc -d -nc
)

rm -rf "$out"
mkdir -p "$out"
mv "$tmp"/"${source_name}"_* "$out"/
ls -lh "$out"
