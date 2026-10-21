#!/bin/sh
# check-maintainer.sh - identity gate for void-linux-stores repo.
# Templates may declare their own maintainer identity (different upstreams).
set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
status=0

# Allow either common identities used by KDE/GNOME or just ensure non-empty
n=0
bad=""
for t in "$root"/srcpkgs/*/template; do
	[ -f "$t" ] || continue
	n=$((n + 1))
	if ! grep -q '^maintainer=' "$t"; then
		bad="$bad $t"
	fi
done
if [ -n "$bad" ]; then
	printf 'FAIL: templates without maintainer:%s\n' "$bad" >&2
	status=1
fi

# Optional external denylist
deny="${MAINTAINER_DENYLIST:-$root/.maintainer-denylist}"
if [ -f "$deny" ]; then
	tmp=$(mktemp) || exit 1
	grep -v '^[[:space:]]*#' "$deny" | grep -v '^[[:space:]]*$' > "$tmp" || true
	if [ -s "$tmp" ]; then
		hits=$(grep -rInE --exclude=check-maintainer.sh -f "$tmp" \
			"$root/srcpkgs" "$root/tools" "$root/README.md" 2>/dev/null || true)
		if [ -n "$hits" ]; then
			printf 'FAIL: disallowed identifier present:\n%s\n' "$hits" >&2
			status=1
		fi
	fi
	rm -f "$tmp"
fi

if [ "$status" -eq 0 ]; then
	printf 'OK: maintainer present in all %s templates\n' "$n"
fi
exit "$status"
