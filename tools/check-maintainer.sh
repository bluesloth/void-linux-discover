#!/bin/sh
# check-maintainer.sh - identity gate for void-linux-stores.
#
# Every template must declare the canonical maintainer identity below, so the
# packaging sources and the served packages advertise the same maintainer
# (this repo's carried templates start from upstream's own template, which
# advertises the upstream maintainer - a misattribution once we serve the
# built package ourselves).
#
# The canonical identity can be overridden with MAINTAINER_CANON (used by a
# per-template exception, if one is ever needed).
#
# An optional external denylist can also reject additional identifiers. It is
# kept out of the repository (see .gitignore): set MAINTAINER_DENYLIST to its
# path, or place it at ./.maintainer-denylist (one extended regex per line;
# '#' comments). A CI-style run without a denylist simply skips that pass.
set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
canon=${MAINTAINER_CANON:-'Void Linux Stores <noreply@github.com>'}
status=0

# 1) every template must carry exactly the canonical maintainer line
n=0
bad=""
for t in "$root"/srcpkgs/*/template; do
	[ -f "$t" ] || continue
	n=$((n + 1))
	if ! grep -qxF "maintainer=\"$canon\"" "$t"; then
		bad="$bad $t"
	fi
done
if [ -n "$bad" ]; then
	printf 'FAIL: templates without the canonical maintainer (%s):%s\n' "$canon" "$bad" >&2
	status=1
fi

# 2) optional external denylist of extra disallowed identifiers
deny="${MAINTAINER_DENYLIST:-$root/.maintainer-denylist}"
if [ -f "$deny" ]; then
	tmp=$(mktemp) || exit 1
	grep -v '^[[:space:]]*#' "$deny" | grep -v '^[[:space:]]*$' > "$tmp" || true
	if [ -s "$tmp" ]; then
		hits=$(grep -rInE --exclude=check-maintainer.sh -f "$tmp" \
			"$root/srcpkgs" "$root/common" "$root/tools" \
			"$root/README.md" "$root/CHANGELOG.md" 2>/dev/null || true)
		if [ -n "$hits" ]; then
			printf 'FAIL: disallowed identifier present:\n%s\n' "$hits" >&2
			status=1
		fi
	fi
	rm -f "$tmp"
fi

if [ "$status" -eq 0 ]; then
	printf 'OK: canonical maintainer in all %s templates\n' "$n"
fi
exit "$status"
