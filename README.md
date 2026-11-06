# void-linux-discover

KDE Discover with a native **xbps** backend, packaged for Void Linux — the
packaging source **and** the served binary repository for this store.
One repository per store: GNOME Software lives in
`private/void-linux-gnome-software/`, cosmic-store in
`private/void-linux-cosmic/` (epoch-managed).

## Contents

| path | what it is |
| --- | --- |
| `srcpkgs/discover/template` | the carried template (backend copy + CMake wiring in `post_extract()`, polkit policy in `post_install()`) |
| `srcpkgs/discover/files/xbps/` | the xbps backend itself, carried as **source files** (a discover release bump does not invalidate diff context the way a unified patch would) |
| `srcpkgs/discover/files/org.kde.discover.xbps.policy` | the polkit actions: `.install` / `.remove` / `.update` `auth_admin`, `.refresh` `allow_active=yes` |
| `srcpkgs/discover/files/discover-xbps-refresh` | the argless helper the `.refresh` action annotates (`xbps-install -S`) |
| `tools/` | build / gate / publish tooling (identical copies in both store repos — edit them together) |
| `CHANGELOG.md` | the decision + release record |
| `PINS.md` | source-distfile checksums of the published packages |
| `LICENSING.md` | licensing statement for the repository and the carried package |
| `.nojekyll`, `.gitattributes`, `.gitignore` | Pages + binary-hygiene rules for the served repository (mirrors the cosmic store repo) |

## House rules that apply here

- **Never emit a bare `-Sy`/`-Syy`** as a pkexec argument: OctoXBPS ships a
  NOPASS polkit action keyed on `argv1=-Sy`. Install/remove use `-y`, the
  system update uses `-Syu`.
- **Privilege** is pkexec + the dedicated `.policy`: every action that performs
  a **transaction** is `auth_admin`, and there is no NOPASS and no daemon. The
  single exception is `.refresh` (`allow_active=yes`), which runs a
  package-owned argless helper that only syncs the repository index — it can
  install, remove and upgrade nothing, and forcing a password for it would make
  Discover's routine update check prompt for no benefit. Do not extend that
  exception to a `pkexec xbps-install` action.
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

## Publish

```sh
XBPS_SIGNING_KEY=/path/to/key tools/publish.sh --from hostdir/binpkgs [--push]
```

Gates on `check-maintainer.sh`, signs packages + index, commits them into
the repository root and tags `stores-rN`. `--dry-run` prints the plan.

**Status: published** — <https://bluesloth.github.io/void-linux-discover/>
serves **`discover-6.7.5_12`** as release `stores-r7` (2026-11-06); its
predecessors `stores-r6` (2026-11-06) served `_11`, `stores-r5` (2026-11-03)
served `discover-6.7.5_10`, `stores-r3` (2026-11-01) served `discover-6.7.5_9`,
`stores-r2` (2026-10-26) served `discover-6.7.5_8` and `stores-r1` (2026-10-22)
served `discover-6.7.5_7`. The signing key is this store's own (DER sha256
`4e40218a…`); it lives off-repo and only its public fingerprint is pinned
(`coordination/PINS.md` §8, installer handoff §8 Q6).

`_10` is the **`[XBPS-REFRESH]`** release (`revision=10`). Discover's update
list comes from `xbps-install -nSu`, which answers from the **cached**
repodata — nothing on a stock Void box ever syncs it, so an update check could
report a system as up to date indefinitely. `checkForUpdates()` now first runs
`pkexec /usr/libexec/discover-xbps-refresh`, a package-owned argless helper
behind a fourth polkit action (`allow_active=yes`), and the three transaction
actions keep `auth_admin` with an annotated `argv1`. The helper path is
annotated rather than `xbps-install -S` because pkexec matches only the first
argument. The probe itself stays unprivileged and unchanged; only the sync is
privileged, and it is asynchronous with a 120 s kill guard so the GUI never
blocks. The helper takes no arguments and can only ever run `xbps-install -S`
(never `-Syu`, never `-y`, stdin on `/dev/null` so a key-import question
declines): annotating `argv1="-S"` on `xbps-install` itself would have been a
hole, because pkexec matches only the first argument and
`pkexec xbps-install -S -y -- <pkg>` would then have run a full transaction
with no password. It resolves `/usr/bin/xbps-install.real` so a sync does not
take a snapper snapshot pair through the `void-xbps-snap` shadow, and throttles
itself to one sync per 600 s with a `/run` stamp, exiting **75**
(`EX_TEMPFAIL`) when it skips. The backend treats any non-zero status as
non-fatal — a refused authorisation or an unreachable mirror must not empty the
update list — but it distinguishes 75 from a real failure so the log never
claims a sync that did not happen.

`stores-r4` (2026-11-03, commit `d37f092…`) published the **same** `_10`
package and signed it correctly, but its index advertised `_9`:
`tools/publish.sh` passed a lexically sorted list to `xbps-rindex -f -a`, and
`discover-6.7.5_10` sorts before `_7`/`_8`/`_9` while `xbps-rindex -a` is
last-wins per pkgname. `stores-r5` corrects the index and changes no package
bytes; the tooling now resolves the newest build per pkgname with
`xbps-uhelper cmpver` (mirrored byte-for-byte in the GNOME Software
repository). Full analysis in `CHANGELOG.md`.

Old build numbers are **not** pruned (`_7`–`_9`'s `.xbps` and `.sig2` stay at
the root; the index lists only the newest), matching the cosmic store's
monotonic-growth behaviour. `_8` is a **metadata-only** rebuild of `_7`: the
shipped `xbps-backend.so` is byte-identical (md5 `bb385531…`), while
`short_desc` gained the ` (xbps backend)` marker so an installed box can be
told apart from Void's stock flatpak-only build without comparing `pkgver`.
`_9` is a **policy-only** rebuild of `_8`: same plugin binary, and the
`install`/`update` polkit actions now annotate
`/usr/local/sbin/void-xbps-snap` — the wrapper that `/usr/bin/xbps-install`
is symlinked to on an installer-provisioned box — instead of
`/usr/bin/xbps-install`, which pkexec never matched after canonicalising the
program path. Revision-numbering and rebase rules are documented in the
template itself.

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
record lives in `docs/HANDOFF-KDE-DISCOVER-XBPS-BACKEND.md`. The current
`_10` build (the `[XBPS-REFRESH]` release) was walked end to end on a fresh
KDE box on 2026-11-06 — install, remove, cancel, the flatpak regression, the
Updates KCM, lock contention and the promptless refresh path — in that
document's §5.6.
