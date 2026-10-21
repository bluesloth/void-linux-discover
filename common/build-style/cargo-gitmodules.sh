#
# Wrapper around cargo.sh that adds git submodule support.
# Used by packages whose source tarball does not include git submodule content
# (e.g. libcosmic with its iced submodule).
#

# Source the original cargo build style (cargo.sh: do_build/do_check/do_install)
. "$XBPS_BUILDSTYLEDIR/cargo.sh"

# epoch-1.10.0 of libcosmic gitignores Cargo.lock (1.9 shipped one), so
# cargo.sh's do_build would die on `--locked` with nothing to lock against.
# Generate the lockfile first, then build locked against that resolution.
do_build() {
	: ${make_cmd:=cargo auditable}
	: ${make_verbose:=-v}
	if [ ! -f Cargo.lock ]; then
		msg_normal "$pkgver: upstream ships no Cargo.lock; resolving dependencies first\n"
		cargo generate-lockfile
	fi
	${make_cmd} build ${XBPS_VERBOSE+${make_verbose}} --release --locked --target ${RUST_TARGET} \
 		${configure_args} ${make_build_args}
}

do_extract() {
	# Resolve the staged tarball the same way the default do-extract hook
	# does (fetch stages distfiles under $XBPS_SRCDISTDIR/$pkgname-$version/),
	# with hostdir/distfiles and the tree-root XBPS_DISTDIR as fallbacks.
	# NOTE: never fail silently here -- a skipped extraction leaves wrksrc
	# missing and the error surfaces far away from the real cause.
	: ${wrksrc:=${PKGNAME}-${version}}
	local tf="" cand
	local base="$(basename "${distfiles%% *}")"
	for cand in "$XBPS_SRCDISTDIR/$PKGNAME-$version/$base" \
	            "$XBPS_HOSTDIR/distfiles/$base" \
	            "$XBPS_DISTDIR/$base"; do
		if [ -f "$cand" ]; then
			tf="$cand"
			break
		fi
	done
	if [ -z "$tf" ]; then
		msg_error "$pkgver: staged source $base not found for $PKGNAME-$version\n"
	fi
	# Use vextract (framework helper): the masterdir has bsdtar but no GNU tar,
	# and vextract mkdirs the destination and defaults to --strip-components=1.
	vextract -C "$wrksrc" "$tf"

	if [ -f "$wrksrc/.gitmodules" ]; then
		# GitHub archive tarballs leave submodule dirs EMPTY and carry no
		# gitlink SHAs, so the extracted tree can never satisfy path deps
		# such as `iced = { path = "iced" }`. Materialize the submodules by
		# fetching the superproject at the exact commit the distfile checksum
		# pins (archive/<40-hex-sha>.tar.gz) and letting git check out the
		# recorded gitlinks. The tarball remains the fetch/checksum gate.
		local sha="${base%.tar.gz}"
		if ! [[ "$sha" =~ ^[0-9a-f]{40}$ ]]; then
			msg_error "$pkgver: $base is not archive/<sha>.tar.gz; the cargo-gitmodules style needs a commit-pinned distfile to materialize submodules\n"
		fi
		if [ -z "${homepage:-}" ]; then
			msg_error "$pkgver: homepage is unset; cannot derive submodule superproject URL\n"
		fi
		cd "$wrksrc"
		git init -q
		git remote add origin "$homepage"
		if git fetch -q --depth 1 origin "$sha" 2>/dev/null; then
			git checkout -q -f FETCH_HEAD
		elif git fetch -q origin 2>/dev/null && git cat-file -e "$sha^{commit}" 2>/dev/null; then
			git checkout -q -f "$sha"
		else
			msg_error "$pkgver: cannot fetch $sha from $homepage\n"
		fi
		git submodule update --init --recursive --depth 1 2>/dev/null ||
			git submodule update --init --recursive ||
			msg_error "$pkgver: git submodule update failed\n"
		# Verify every declared submodule actually materialized; a silent
		# no-op here is exactly the failure mode this style must never have.
		local sp
		for sp in $(git config -f .gitmodules --get-regexp 'submodule\..*\.path' | cut -d' ' -f2-); do
			if [ -z "$(ls -A "$wrksrc/$sp" 2>/dev/null)" ]; then
				msg_error "$pkgver: submodule '$sp' is empty after git submodule update\n"
			fi
		done
		# Drop all git metadata; the package must not ship .git files.
		git submodule foreach -q --recursive 'rm -rf .git' >/dev/null 2>&1 || :
		rm -rf .git
	fi

	# Remove examples from workspace if present (broken cosmic-time dep)
	if [ -f "$wrksrc/Cargo.toml" ]; then
		sed -i '/examples\/\*/d' "$wrksrc/Cargo.toml"
	fi
}
