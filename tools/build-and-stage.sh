#!/bin/sh
# build-and-stage.sh - overlay these templates onto a void-packages checkout,
# build the COSMIC set with xbps-src, and stage a signed binary repository.
#
# Building COSMIC is heavy (Rust). Run this on a Void Linux host or container
# with the usual build prerequisites, not on a random machine.
#
# Usage:
#   XBPS_DISTDIR=/path/to/void-packages tools/build-and-stage.sh [pkg ...]
#
#   XBPS_DISTDIR   a void-packages clone (https://github.com/void-linux/void-packages)
#   [pkg ...]      packages to build; defaults to cosmic-desktop-full, which
#                  pulls the rest of the set in as dependencies.
#
# Environment:
#   XBPS_ALLOW_RESTRICTED=yes     required by some COSMIC components
#   XBPS_PKG_ARCH=x86_64          target architecture (default: host arch)
#
# The overlay is idempotent: our templates are copied over the checkout's, so
# re-running after a template change is enough. Output packages land under
# $XBPS_DISTDIR/hostdir/binpkgs/<repository>/.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
XBPS_DISTDIR=${XBPS_DISTDIR:?set XBPS_DISTDIR to a void-packages checkout}

[ -x "$XBPS_DISTDIR/xbps-src" ] || {
	printf 'build-and-stage: %s does not look like a void-packages tree\n' "$XBPS_DISTDIR" >&2
	exit 1
}
command -v rsync >/dev/null 2>&1 || {
	printf 'build-and-stage: rsync is required\n' >&2
	exit 1
}

# 1) overlay our templates
count=0
for d in "$root"/srcpkgs/*/; do
	n=$(basename "$d")
	mkdir -p "$XBPS_DISTDIR/srcpkgs/$n"
	rsync -a --delete "$d" "$XBPS_DISTDIR/srcpkgs/$n/"
	count=$((count + 1))
done
printf 'overlaid %d templates into %s\n' "$count" "$XBPS_DISTDIR/srcpkgs"

# 2) the git-submodule cargo build style (libcosmic)
cp -a "$root/common/build-style/cargo-gitmodules.sh" "$XBPS_DISTDIR/common/build-style/"

# 3) extra shlibs entries, if any
if [ -s "$root/common/shlibs.append" ]; then
	cat "$root/common/shlibs.append" >> "$XBPS_DISTDIR/common/shlibs"
fi

# 4) build
set -- ${*:-cosmic-desktop-full}
cd "$XBPS_DISTDIR"
export XBPS_ALLOW_RESTRICTED=${XBPS_ALLOW_RESTRICTED:-yes}
printf 'building: %s\n' "$*"
./xbps-src pkg "$@"

cat <<EOF

Built. Packages are under:
  $XBPS_DISTDIR/hostdir/binpkgs/

To publish, sign the packages and copy the .xbps / .sig2 files and the
<arch>-repodata index into this repository's root (GitHub Pages serves them
to the installer):

  xbps-rindex -d -a hostdir/binpkgs/<repo>/*.xbps
  xbps-rindex --sign --signedby "Void Cosmic <noreply@github.com>" hostdir/binpkgs/<repo>
  # copy .xbps / .sig2 / <arch>-repodata into this repo root, then commit + push
EOF
