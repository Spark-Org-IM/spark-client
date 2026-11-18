#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

version="${1:-$(dpkg-parsechangelog -SVersion)}"
upstream="${version%-*}"
out="${2:-$root/build/source}"
ref="${3:-HEAD}"

if [ -z "$version" ] || [ -z "$upstream" ]; then
  echo "cannot determine Debian version" >&2
  exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

stage="$tmp/spark-tui-$upstream"
mkdir -p "$stage"
git archive --format=tar "$ref" | tar -x -C "$stage"
rm -rf "$stage/debian"

# The upstream tarball must not contain the debian/ directory; the packaging
# diff is created separately by dpkg-source.
tar -C "$tmp" -czf "$tmp/spark-tui_${upstream}.orig.tar.gz" \
  "spark-tui-${upstream}"

cp -a "$root/debian" "$stage/debian"
sed -i "1s/^spark-tui ([^)]*)/spark-tui (${version})/" "$stage/debian/changelog"

(
  cd "$stage"
  # -S: source package only; -sa: include the orig tarball in .changes;
  # -nc: staging tree is already clean, so no debhelper clean hooks.
  dpkg-buildpackage -S -us -uc -sa -d -nc
)

rm -rf "$out"
mkdir -p "$out"
mv "$tmp"/spark-tui_* "$out"/
ls -lh "$out"
