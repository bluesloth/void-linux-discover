#!/bin/sh
# publish.sh - stage a signed xbps repository into this repo, commit it, tag it
# with the repo release tag, and (optionally) push.
#
# This repository is both the packaging source and the served binary repo. The
# .xbps / .sig2 files and the <arch>-repodata index at the repo root are what
# GitHub Pages serves to the stores. This script automates the last mile:
#
#     build (optional) -> sign (optional) -> stage -> commit -> tag -> push
#
# Unlike void-linux-cosmic there is no tracked epoch to tag with: the carried
# templates track independent upstreams (KDE's, GNOME's, ...) with no shared
# version mechanism, so a release is a monotonic repository counter:
# `stores-r1`, `stores-r2`, ... The commit body names the exact package set,
# so tag + commit together describe what is served.
#
# Usage:
#     tools/publish.sh --from DIR [--push]
#     XBPS_DISTDIR=/path/to/void-packages tools/publish.sh --build [--push]
#
# `--from DIR` must be the complete, already-signed package set for the repo
# (all the *.x86_64.xbps you intend to serve, plus a fresh <arch>-repodata).
# `--build` runs tools/build-and-stage.sh first and stages what it produced.
#
# Options:
#     --from DIR        staging dir holding the built packages + index
#     --build            run tools/build-and-stage.sh first (needs XBPS_DISTDIR)
#     --arch ARCH        package architecture / index name (default: x86_64)
#     --resign           re-sign every package and the index (needs --privkey)
#     --privkey FILE     signing key for xbps-rindex (or $XBPS_SIGNING_KEY)
#     --signedby STR     signature identity (default below; must match the key)
#     --message TEXT     extra commit-message text
#     --tag NAME         override the auto (`stores-rN`) tag
#     --dry-run          print the plan and exit without changing anything
#     --push             push the commit and tag (default: commit + tag locally)
#     -h, --help         this help
#
# The commit and tag are made with a fixed, non-PII identity (override with
# $PUBLISH_GIT_NAME / $PUBLISH_GIT_EMAIL), so a machine with no git identity
# configured cannot leak its local user@host.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
branch=main
arch=x86_64
from=""; build=0; resign=0; push=0; dry=0; tag_override=0
privkey=${XBPS_SIGNING_KEY:-}
signedby='Void Linux Stores <noreply@github.com>'
message=""; tag=""
git_name=${PUBLISH_GIT_NAME:-Void Linux Stores}
git_email=${PUBLISH_GIT_EMAIL:-stores@localhost}
tag_base=${PUBLISH_TAG_PREFIX:-stores}

die() { printf 'publish: %s\n' "$1" >&2; exit "${2:-1}"; }

usage() {
    sed -n '2,/^set -eu/p' "$0" | sed '$d' | sed 's/^# \{0,1\}//'
}

while [ $# -gt 0 ]; do
    case "$1" in
        --from)     [ $# -ge 2 ] || die "--from needs a directory"; from=$2; shift 2 ;;
        --build)    build=1; shift ;;
        --arch)     [ $# -ge 2 ] || die "--arch needs a value"; arch=$2; shift 2 ;;
        --resign)   resign=1; shift ;;
        --privkey)  [ $# -ge 2 ] || die "--privkey needs a path"; privkey=$2; shift 2 ;;
        --signedby) [ $# -ge 2 ] || die "--signedby needs a value"; signedby=$2; shift 2 ;;
        --message)  [ $# -ge 2 ] || die "--message needs text"; message=$2; shift 2 ;;
        --tag)      [ $# -ge 2 ] || die "--tag needs a name"; tag=$2; tag_override=1; shift 2 ;;
        --dry-run)  dry=1; shift ;;
        --push)     push=1; shift ;;
        -h|--help)  usage; exit 0 ;;
        *)          die "unknown option '$1' (try --help)" ;;
    esac
done

cd "$root"
[ -d .git ] || die "$root is not a git checkout"

# --- pre-flight: the carried templates must be publishable -------------------
[ "$dry" = 1 ] || tools/check-maintainer.sh || die "check-maintainer.sh failed"

# --- optional build ---------------------------------------------------------
if [ "$build" = 1 ]; then
    [ -n "${XBPS_DISTDIR:-}" ] || die "--build needs XBPS_DISTDIR set"
    [ "$dry" = 1 ] || tools/build-and-stage.sh
    if [ -z "$from" ]; then
        from=$(dirname "$(find "${XBPS_DISTDIR:-/nonexistent}/hostdir/binpkgs" \
            -maxdepth 2 -name "*.$arch.xbps" -print 2>/dev/null | head -n1)")
    fi
fi
[ -n "$from" ] || die "nothing to stage: pass --from DIR or --build"
[ -d "$from" ] || die "--from '$from' is not a directory"

pkgs=$(find "$from" -maxdepth 1 -name "*.$arch.xbps" -print 2>/dev/null | sort)
[ -n "$pkgs" ] || die "no *.$arch.xbps in $from"
npkg=$(printf '%s\n' "$pkgs" | wc -l | tr -d ' ')

# --- resolve the next release tag (counting only tags on the remote) --------
if [ -z "$tag" ]; then
    remote_tags=$(git ls-remote --tags --refs origin "refs/tags/$tag_base-r*" 2>/dev/null \
        | sed 's#.*refs/tags/##' || true)
    n=0
    for t in $remote_tags; do
        case "$t" in
            "$tag_base"-r*)
                r=${t#"$tag_base"-r}
                case "$r" in
                    ''|*[!0-9]*) ;;
                    *) if [ "$r" -gt "$n" ]; then n=$r; fi ;;
                esac ;;
        esac
    done
    tag="$tag_base-r$((n + 1))"
fi

printf 'publish: %s -> tag %s (%s package(s) from %s)\n' \
    "$branch" "$tag" "$npkg" "$from"

# --- sign (optional) --------------------------------------------------------
if [ -n "$privkey" ]; then
    [ "$dry" = 1 ] || printf 'signing %s package(s)\n' "$npkg"
    for f in $pkgs; do
        if [ "$resign" = 1 ] || [ ! -f "$f.sig2" ]; then
            # NB: --sign-pkg -f is broken on some xbps builds (unlinks the
            # .sig2, then fails to recreate it). Remove any stale signature
            # ourselves, then sign without -f.
            [ "$dry" = 1 ] || { rm -f "$f.sig2"; xbps-rindex -S --privkey "$privkey" --signedby "$signedby" "$f" >/dev/null; }
        fi
    done
    if [ "$dry" = 1 ]; then
        printf '   would sign, regenerate and sign the %s index\n' "$arch"
    else
        xbps-rindex -f -a $pkgs >/dev/null
        xbps-rindex -s --privkey "$privkey" --signedby "$signedby" "$from" >/dev/null
    fi
else
    printf 'no --privkey: expecting pre-signed packages and a signed index\n'
fi

idx="$from/$arch-repodata"
[ "$dry" = 1 ] || [ -f "$idx" ] || die "no $arch-repodata in $from (build/sign first, or pass --privkey)"

# --- stage ------------------------------------------------------------------
if [ "$dry" = 1 ]; then
    printf '   would copy *.%s.xbps, *.sig2 and %s-repodata into %s/\n' "$arch" "$arch" "$root"
    printf '   would commit with identity %s <%s> and tag %s\n' "$git_name" "$git_email" "$tag"
    if [ "$push" = 1 ]; then printf '   would push %s and the tag\n' "$branch"; fi
    exit 0
fi

cp -f "$from"/*."$arch".xbps "$root"/ || die "copy of .xbps into the repo root failed"
cp -f "$from"/*."$arch".xbps.sig2 "$root"/ 2>/dev/null || die "copy of .sig2 into the repo root failed"
cp -f "$idx" "$root/$arch-repodata" || die "copy of the index failed"

git add -A -- "*.$arch.xbps" "*.$arch.xbps.sig2" "$arch-repodata"

if git diff --cached --quiet -- "*.$arch.xbps" "*.$arch.xbps.sig2" "$arch-repodata"; then
    printf 'publish: served repository unchanged — nothing to commit\n'
    # a previous run may already have tagged this exact commit (e.g. the push
    # failed): reuse that tag instead of inventing a revision.
    if [ "$tag_override" = 0 ]; then
        lt=$(git tag --points-at HEAD 2>/dev/null \
            | grep -E "^${tag_base}-r[0-9]+$" | sort -V | tail -n1 || true)
        if [ -n "$lt" ]; then tag=$lt; fi
    fi
    if git rev-parse -q --verify "refs/tags/$tag" >/dev/null 2>&1; then
        printf 'publish: tag %s already exists\n' "$tag"
    else
        git -c user.name="$git_name" -c user.email="$git_email" \
            tag -a "$tag" -m "Release $tag" || die "tag failed"
        printf 'publish: tagged existing HEAD %s\n' "$tag"
    fi
else
    subject="repo: publish $arch repository as $tag"
    names=$(git diff --cached --name-only -- "*.$arch.xbps" | sed 's#.*/##' | sort)
    body=$(printf 'Packages:\n%s' "$(printf '%s\n' "$names" | sed 's/^/- /')")
    if [ -n "$message" ]; then body=$(printf '%s\n\n%s' "$body" "$message"); fi
    msg=$(printf '%s\n\n%s' "$subject" "$body")
    git -c user.name="$git_name" -c user.email="$git_email" \
        commit -q -m "$msg" -- "*.$arch.xbps" "*.$arch.xbps.sig2" "$arch-repodata" \
        || die "commit failed"
    git -c user.name="$git_name" -c user.email="$git_email" \
        tag -a "$tag" -m "$subject" || die "tag failed"
    printf 'publish: committed %s, tagged %s\n' \
        "$(git rev-parse --short HEAD)" "$tag"
fi

# --- push -------------------------------------------------------------------
if [ "$push" = 1 ]; then
    git push origin "$branch" || die "push of $branch failed (not a fast-forward?)" 7
    git push origin "$tag" || die "push of tag $tag failed" 7
    printf 'OK: published %s to origin (%s, tag %s)\n' "$arch" "$branch" "$tag"
else
    printf 'OK: staged locally (NOT pushed; re-run with --push)\n'
    printf '   git push origin %s && git push origin %s\n' "$branch" "$tag"
fi
