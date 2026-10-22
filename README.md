# void-linux-discover

KDE Discover with a native **xbps** backend, packaged for Void Linux — the
packaging source **and** (once published) the served binary repository for
this store. One repository per store: GNOME Software lives in
`private/void-linux-gnome-software/`, cosmic-store in
`private/void-linux-cosmic/` (epoch-managed).

## Contents

| path | what it is |
| --- | --- |
| `srcpkgs/discover/template` | the carried template (backend copy + CMake wiring in `post_extract()`, polkit policy in `post_install()`) |
| `srcpkgs/discover/files/xbps/` | the xbps backend itself, carried as **source files** (a discover release bump does not invalidate diff context the way a unified patch would) |
| `srcpkgs/discover/files/org.kde.discover.xbps.policy` | three `auth_admin` actions: `.install` / `.remove` / `.update` |
| `tools/` | build / gate / publish tooling (identical copies in both store repos — edit them together) |
| `CHANGELOG.md` | the decision + release record |
| `PINS.md` | source-distfile checksums of the published packages |
| `LICENSING.md` | licensing statement for the repository and the carried package |
| `.nojekyll`, `.gitattributes`, `.gitignore` | Pages + binary-hygiene rules for the served repository (mirrors the cosmic store repo) |

## House rules that apply here

- **Never emit a bare `-Sy`/`-Syy`** as a pkexec argument: OctoXBPS ships a
  NOPASS polkit action keyed on `argv1=-Sy`. Install/remove use `-y`, the
  system update uses `-Syu`.
- **Privilege** is pkexec + the dedicated `.policy`, every action
  `auth_admin`. No NOPASS, no daemon.
- **Repacks must be re-signed**: an unsigned repack of served repodata makes
  clients abort (2026-10-09 `void-cosmic-repo` incident).
- The maintainer identity in every template is
  `Void Linux Stores <noreply@github.com>` — gated by
  `tools/check-maintainer.sh`.

## Build

```sh
XBPS_DISTDIR=/path/to/void-packages tools/build-and-stage.sh [pkg ...]
```

Overlays `srcpkgs/` into the checkout (idempotent), builds with `xbps-src`
(defaults to every template carried here), output under
`$XBPS_DISTDIR/hostdir/binpkgs/`.

## Publish (not yet done)

```sh
XBPS_SIGNING_KEY=/path/to/key tools/publish.sh --from hostdir/binpkgs [--push]
```

Gates on `check-maintainer.sh`, signs packages + index, commits them into
the repository root and tags `stores-rN`. `--dry-run` prints the plan.
**Status: source only — no served repository, signing key or public export
yet** (handoff §7 item 6/7).

## Served repository layout

Mirrors the cosmic store repo (`bluesloth/void-linux-cosmic`): **one
repository carries both the packaging source and the served xbps
repository** — there is no separate artifact repo. After `tools/publish.sh`
the built artifacts live at the repository root (`*.x86_64.xbps`,
`*.x86_64.xbps.sig2`, `x86_64-repodata`) and GitHub Pages serves them:

| what | URL |
| --- | --- |
| repository (audit) | `https://github.com/bluesloth/void-linux-discover` |
| index | `https://bluesloth.github.io/void-linux-discover/x86_64-repodata` |

`.nojekyll` is present so Pages serves the raw binaries instead of passing
them through Jekyll. The signing key stays off-repo (`--privkey` /
`$XBPS_SIGNING_KEY`); only its public fingerprint is pinned in the installer.

## Validation

The backend is validated by the headless Qt harness that loads the shipped
`xbps-backend.so` via `QPluginLoader` and compares its view against the
xbps CLI (shim + real-pkexec modes), plus a GUI walkthrough matrix. The
record lives in `docs/HANDOFF-KDE-DISCOVER-XBPS-BACKEND.md`.
