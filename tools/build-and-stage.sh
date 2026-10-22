#!/bin/sh
# build-and-stage.sh - overlay these templates onto a void-packages checkout,
# build the carried set with xbps-src, and stage the binary packages for
# signing/publishing (tools/publish.sh).
#
# Building GUI store stacks is heavy (Qt/KDE, GNOME). Run this on a Void
# Linux host or container with the usual build prerequisites, not on a
# random machine.
#
# Usage:
#   XBPS_DISTDIR=/path/to/void-packages tools/build-and-stage.sh [pkg ...]
#
#   XBPS_DISTDIR   a void-packages clone (https://github.com/void-linux/void-packages)
#   [pkg ...]      packages to build; defaults to every package carried in
#                  srcpkgs/ (the whole set this repo serves).
#
# Environment:
#   XBPS_ALLOW_RESTRICTED=yes     only needed by templates with a restricted
#                                 licence; none of ours carry one, so this is
#                                 normally left unset
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

# 2) extra shlibs entries, if any
if [ -s "$root/common/shlibs.append" ]; then
	cat "$root/common/shlibs.append" >> "$XBPS_DISTDIR/common/shlibs"
fi

# 3) build: default to the full carried set
if [ "$#" -gt 0 ]; then
	:
else
	set --
	for d in "$root"/srcpkgs/*/; do
		set -- "$@" "$(basename "$d")"
	done
fi
[ "$#" -gt 0 ] || {
	printf 'build-and-stage: nothing to build (srcpkgs/ is empty)\n' >&2
	exit 1
}
cd "$XBPS_DISTDIR"
printf 'building: %s\n' "$*"
./xbps-src pkg "$@"

cat <<EOF

Built. Packages are under:
  $XBPS_DISTDIR/hostdir/binpkgs/

To publish, stage and sign them with tools/publish.sh (it signs the packages
and the index, commits them into this repository's root and tags the release;
GitHub Pages then serves them to the stores):

  XBPS_SIGNING_KEY=/path/to/key tools/publish.sh --from hostdir/binpkgs [--push]

NOTE: a repack of an already-served repository MUST be re-signed
(xbps-rindex -f -a + -s), or clients abort on the stale signature.
EOF
