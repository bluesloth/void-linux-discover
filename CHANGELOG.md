# Changelog

All notable changes to **Void Linux Stores** — the `xbps-src` packaging
sources for the desktop software stores on Void Linux — are documented in this
file.

## What this repository is

**One repository per store: this repo carries KDE Discover only** — its
template, its backend sources and its polkit policy — each template pinned
to **its own** upstream release. GNOME Software lives in its own repo
(`private/void-linux-gnome-software/`); the stores have **no shared version
mechanism** with each other or with the COSMIC epoch tooling, so they are
isolated by default.

**2026-10-21 — layout reversed to Option C (one repo per store).** The
original decision (2026-10-20, Decision 2 / Option B) grouped Discover and
GNOME Software in one shared `void-linux-stores` repo so that the layer they
genuinely share — one xbps command map, one privilege idiom, one packaging
pattern — would live once. The operator split them again **before the first
publish**, which is the only cheap window: once a repo is served, its
repodata, signing key, Pages site, installer repo URL and PINS rows all
point at one repository and a split means re-serving, re-signing and
re-pinning. Per-store repos make a one-store PR trivial (branch-scoped
review), keep provenance store-pure and isolate publish blast radius. The
cost — the shared layer now exists as identical copies of `tools/` in each
store repo — is managed by editing both copies together.

The cosmic-store backend is **not** here: it stays in
`private/void-linux-cosmic/`, because that repository is organised by
upstream provenance (the COSMIC epoch tags) and its tooling manages it.

## Status

**Published.** This repository *is* the served binary repository: it carries
the packaging source (`srcpkgs/`, `tools/`) **and** the built, signed
artifacts at its root, which GitHub Pages serves at
<https://bluesloth.github.io/void-linux-discover/>. The first served release
is `stores-r1` (2026-10-22) — see the release entry below. Every repack of
the served repodata must carry the re-signing caution below.

## Release model

Each store's template carries its own upstream `version=` and its own
`revision=`; a store's release is identified by `<upstream-version>_<revision>`.
Tracking a new upstream release is `tools/sync-upstream.sh`.

The *repository* release tag — what `tools/publish.sh` stamps a served index
with — is `stores-rN`, a monotonic per-repository counter (this repo's tags
and `void-linux-gnome-software/`'s tags are independent): the carried
templates track independent upstreams, so there is no shared epoch to key a
tag on the way the COSMIC repository keys `epoch-N.M.P`.

**Caution — repodata must be re-signed.** Any repack of this repository's
repodata MUST be signed with the repository signing key. An unsigned repack
makes the installer's trust-anchor check FATAL (see the 2026-10-09 incident
in the COSMIC repository: a repack that dropped the signature left the index
unsigned and every install aborted).

## stores-r7 — 2026-11-06

Served set: `discover-6.7.5_12.x86_64.xbps` (1657753 B,
`sha256 97e88981…b20b`), its `.sig2`, and the re-signed `x86_64-repodata`
advertising `discover-6.7.5_12`. Release tag `stores-r7` on the publish
commit. Superseded builds `_7`–`_11` remain at the repository root but are no
longer named by the index.

### `[XBPS-NOTIFIER]` — pending xbps updates reach the tray

OctoXBPS has a notifier that tells you updates are waiting; Discover on this
repository did not. The gap was **not** in the backend: Discover's tray icon,
its "Updates available" notification and its "View Updates" action do not come
from a `AbstractResourcesBackend` at all. They come from a separate process,
`DiscoverNotifier`, which loads plugins out of
`/usr/lib/qt6/plugins/discover-notifier/` whose IID matches
`DISCOVER_NOTIFIER_IID` (`org.kde.discover.6.7.5.BackendNotifierModule`).
Upstream ships exactly one such plugin, `FlatpakNotifier.so`, and Void builds
Discover with the notifier on (`-DWITH_NOTIFIER=ON`, upstream default), so a
stock install had a working tray path that could only ever see Flatpaks. A
Void box with pending xbps updates showed nothing.

A second notifier module closes that gap, carried beside the backend in
`srcpkgs/discover/files/xbps/` and installed into the notifier plugin
namespace rather than the backend one:

- `XbpsNotifier.h` — a `BackendNotifierModule` subclass with the interface's
  pure virtuals: `recheckSystemUpdateNeeded()`, `hasUpdates()`,
  `hasSecurityUpdates()`, `needsReboot()`.
- `XbpsNotifier.cpp` — the probe and the schedule (below).
- `XbpsNotifier` target in `files/xbps/CMakeLists.txt` via
  `kcoreaddons_add_plugin(... INSTALL_NAMESPACE "discover-notifier")`, linking
  `Discover::Notifiers Qt::Core Qt::Concurrent` and setting
  `INSTALL_RPATH ${CMAKE_INSTALL_FULL_LIBDIR}/plasma-discover` so the plugin
  resolves `libDiscoverNotifiers.so` out of the Discover prefix (the same RPATH
  the flatpak notifier gets).

**The probe is unprivileged, on purpose.** `hasPendingUpdates()` runs
`xbps-install -nSu` and accepts any line whose second field is `update` — the
identical command and parse the backend's own `loadUpdates()` uses, so the
tray and the Updates page cannot disagree. `-n` is a dry run: it answers from
the cached repodata in `/var/db/xbps/`, which every user can read. **No polkit
action is involved and no prompt can appear at login, on resume, or on any
periodic re-check.** The notifier never escalates, so it also cannot be a
privilege surface: the only privileged xbps work on this box remains the GUI
backend's sync (the existing `org.kde.discover.xbps.refresh`
`allow_active=yes` action → `/usr/libexec/discover-xbps-refresh`, which takes
no arguments and hardcodes a bare `xbps-install -S`).

**How it reports.** The probe runs through `QtConcurrent::run` behind a
`QFutureWatcher`, so the GUI thread never blocks on xbps, and `foundUpdates()`
is emitted **only when the answer changes** — that transition is what tells
`DiscoverNotifier` to raise the tray badge and post one notification, so a
steady "up to date" state is silent. A `m_checking` guard makes a re-entrant
re-check a no-op instead of a second spawn. `hasSecurityUpdates()` returns
false and `needsReboot()` false: xbps carries no security-severity metadata
and has no separate reboot signal to report.

**What triggers a check.** A 24-hour `QTimer` (`m_dailyCheck`), plus a
`QFileSystemWatcher` over `/var/db/xbps/*/x86_64-repodata` — the file every
repository sync rewrites — so a `-S` from anywhere (the backend's own refresh
included) re-runs the probe immediately instead of waiting for the next day.
The watcher's paths are re-scanned before and after each check
(`refreshWatchedRepodata()`) because repository files can appear or disappear
as `/etc/xbps.d/*.conf` entries change.

Both child-process paths fail safe: the start leg has a 5 s timeout and the
finish leg 60 s, each killing the child on expiry, and any failure returns
"no updates" rather than leaving a stale badge on screen.

Template `revision=11` → `12`.

---

## stores-r6 — 2026-11-06

Served set: `discover-6.7.5_11.x86_64.xbps` (1650382 B,
`sha256 6bf09d48…2918`), its `.sig2`, and the re-signed `x86_64-repodata`
(2309 B, `sha256 5dafb919…96e8`) advertising `discover-6.7.5_11`.
Release tag `stores-r6` = `44294b9…` on commit `7507d91…`. Superseded builds
`_7`–`_10` remain at the repository root but are no longer named by the index.

### `[XBPS-SIZE]` — report real package sizes, and stop the "0 B" spinner

Discover showed a perpetually spinning indicator where an application's size
belongs — during and after an install — and Installed-page tiles read `0 B`.
Two independent faults stacked up:

1. **The backend never set a size except for updates.** `XbpsBackend` called
   `setSize()` only inside `loadUpdates()`, from the download-bytes column of
   `xbps-install -nSu`; `loadInstalled()` and `search()` left `m_size` at its
   0 default. There is no `fetchDetails()` override either, so nothing could
   fill it in later.
2. **Discover treats "size 0 with a description" as "still resolving".**
   `ApplicationPageFullComponent.qml` shows
   `BusyIndicator { visible: !appSize.visible && sizeDescription.length > 0 }`
   and only shows `appSize` when `application.size != 0`, while the inherited
   `AbstractResource::sizeDescription()` formats an unknown size as `"0 B"` —
   never an empty string. So the spinner ran forever on top of a bogus
   `0 B`.

Fixed in the carried backend (`srcpkgs/discover/files/xbps/`):

- `readInstalledSizes()` reads `/var/db/xbps/pkgdb-*.plist` once and returns
  `pkgname → installed_size` in raw bytes. One file read beats ~800
  `xbps-query` forks, and it is the only way to get exact numbers: the CLI
  prints human-readable sizes only and has no batch property form.
- `loadInstalled()` applies that size to every installed package when it is
  marked installed, and resets a package to size 0 when it is no longer in
  `xbps-query -l`. `loadUpdates()` still overrides with the download size for
  upgradeable packages, because it runs afterwards.
- `XbpsResource::sizeDescription()` now returns an empty `QString` when the
  size is unknown instead of `"0 B"`, which hides both the label and the
  spinner. `setSize()` also emits `sizeChanged()` on change (and returns
  early when nothing changed), so a size arriving late re-renders the QML
  bindings.

Template `revision=10` → `11`.

---

## stores-r5 — 2026-11-03

### `[PUBLISH-INDEX-ORDER]` — correct the index `stores-r4` shipped

`stores-r4` (`d37f092`) shipped `discover-6.7.5_10` and signed it correctly —
`openssl dgst -sha256 -verify` against the store's published key says
`Verified OK` — but its `x86_64-repodata` advertised **`discover-6.7.5_9`**.
The package was right and the index was wrong, so a client pointed at this
repository would have been offered `_9`, i.e. the build without the refresh
fix.

**Cause.** `tools/publish.sh` built `xbps-rindex -f -a`'s argument list with
`find … | sort`, which is lexicographic: `discover-6.7.5_10` sorts **before**
`discover-6.7.5_7/_8/_9`. `xbps-rindex -a` is last-wins for a given pkgname
and `-f` overwrites unconditionally, so `_10` was added first and `_9` last,
and the index ended up naming `_9`. Reproduced in a scratch directory: `_10`
alone indexes as `_10`; adding `_9` afterwards with `-f` flips it back. The
packages themselves were never in doubt — `xbps-uhelper cmpver discover-6.7.5_10 discover-6.7.5_9` returns `rc=1`, so `_10` is the newer build.
Single-digit revisions had always happened to sort in build order, which is
why every release before this one was correct and the trap only appeared once
a revision reached `_10`.

**Fix.** `tools/publish.sh` now resolves the index list per package name: for
each pkgname it compares every staged build with `xbps-uhelper cmpver` and
adds **only the newest** to `xbps-rindex -f -a`, printing a note when that
differs from what the lexical order would have picked (here:
`index: discover -> discover-6.7.5_10.x86_64.xbps (lexical order would have picked discover-6.7.5_9.x86_64.xbps)`).
The index is keyed by pkgname and has always advertised exactly one build per
name, so this changes no convention — superseded files are still copied to the
repository root and stay out of the index, as before. The same block is copied
byte-for-byte from the COSMIC repository's `tools/publish.sh`, which has
carried this fix since the `cosmic-store` `_10` build.

**This release contains no package change.** `discover-6.7.5_10` is the same
1649510 B, `sha256 2b44d31a…b09a` file `stores-r4` served; only
`x86_64-repodata` is regenerated and re-signed (it now advertises `_10`).

---

## stores-r4 — 2026-11-03

### `[XBPS-REFRESH]` — sync the repodata before an update check

`revision=9 → 10`. Discover's update list comes from `xbps-install -nSu`,
which is a **dry run**: it answers from the cached repodata and never syncs
it. Nothing else on a stock Void box syncs it either (there is no xbps cron
job), so until now the backend reported "up to date" forever while the
mirrors moved on — the same defect the COSMIC store carried, fixed there in
`epoch-1.11.0-r7` and here by the same shape.

**What is new.**

* `checkForUpdates()` no longer goes straight to the probe. It zeroes the
  progress (Discover's "(Fetching…)", as before), then `beginUpdateCheck()`
  spawns `pkexec /usr/libexec/discover-xbps-refresh` and calls `loadUpdates()`
  only when that child has finished. One completion path, whatever happened:
  an `errorOccurred(FailedToStart)`, a normal exit, a signal death and a
  120 s guard kill all funnel through a single lambda, so the probe runs
  exactly once and `m_refreshProcess` cannot leak.
* `refreshUpdates()` is deliberately **untouched**: it also runs after every
  finished transaction, and the one transaction that can change what is
  available (`xbps-install -Syu`) has just synced the repositories itself, so
  a second sync there would be pure noise.
* A check already in flight makes a second `beginUpdateCheck()` a no-op
  rather than a second spawn.
* `pkexec` exit codes are named separately: **126** "authentication was
  cancelled", **127** "pkexec could not run the helper", **75** (the helper's
  `EX_TEMPFAIL` throttle) "the index was synced recently", signal "the helper
  was killed". Anything else carries the helper's stderr tail into the log.
* The refresh is **best effort throughout**: a refused authorisation, a
  missing helper or an unreachable mirror leaves the cached index in place
  and the update list still loads. An update list that never appears would be
  a worse outcome than a stale one.

**The helper** is `/usr/libexec/discover-xbps-refresh`, package-owned, mode
755, `#!/bin/sh`, `set -u`. It takes **no arguments**, refuses to run unless
uid 0 (so an accidental unprivileged run cannot report a sync that never
happened — `xbps-install` as a normal user fails every reposync with EPERM
*and still exits 0*), hardcodes a bare `xbps-install -S`, and throttles
itself with a `/run` stamp (`EX_TEMPFAIL` = 75) so opening the Updates page
repeatedly cannot hammer the mirrors. It mirrors the COSMIC store's
`cosmic-store-refresh` byte-for-byte in behaviour.

**The privilege model.** A fourth action, `org.kde.discover.xbps.refresh`, is
`allow_active=yes` / `allow_any=no` / `allow_inactive=no` and is annotated on
`org.freedesktop.policykit.exec.path=/usr/libexec/discover-xbps-refresh`. The
three transaction actions keep `auth_admin` on every default and keep their
`exec.argv1` annotations (`-y` / `-y` / `-Syu`) — **unchanged from revision
9**. Annotating `argv1=-S` on `xbps-install` itself was rejected: pkexec
matches only the *first* argument, so that grant would have let any active
session run a full `pkexec xbps-install -S -y -- <packages>` with no password.
The helper takes no arguments at all, so the grant cannot be widened into a
transaction.

A refresh prompt would have been a password box at every startup and every
visit to the Updates page, which is why this one action is the exception; a
refused refresh is harmless by construction.

**Verified at build time** (this box runs COSMIC, so neither Discover nor its
GUI exists here — the packaged bytes are what could be checked):

* `discover-6.7.5_10.x86_64.xbps`, 1649510 B,
  `sha256 2b44d31a…b09a`; `./xbps-src pkg` `EXIT=0`.
* inside the package: `usr/libexec/discover-xbps-refresh` (4690 B, md5
  `6f8b9d26…b2b8`), `usr/share/polkit-1/actions/org.kde.discover.xbps.policy`
  (3876 B, **exactly 4 actions**, three `auth_admin` + one `allow_active`),
  `usr/lib/qt6/plugins/discover/xbps-backend.so` (134288 B, md5
  `2a8b1525…6130`).
* `strings -e l` on the plugin — Qt's `QStringLiteral`s are **UTF-16LE**, so
  plain `strings` reports zero hits — shows
  `/usr/libexec/discover-xbps-refresh` and all seven
  `xbps-backend: repodata …` messages, i.e. the refresh path really is
  compiled into the shipped binary.
* `sh -n` clean on the helper; the policy parses as XML and carries no `--`
  inside any comment (an XML comment may not contain a double hyphen, and one
  silently breaks policy loading).
* `xbps-backend: repodata …` — the harness expectation that a cold start
  spawns zero `pkexec` calls changes with this release: `checkForUpdates()`
  runs once at construction, so a cold start now legitimately spawns exactly
  one `pkexec /usr/libexec/discover-xbps-refresh`.

**Not verified here** (needs a KDE box, owed): the polkit dialog for the
three transaction actions, and the refresh path end to end at runtime.

---

## stores-r3 — 2026-11-01

### `[POLKIT-SNAP-PATH]` — annotate the wrapper the installer actually ships

`revision=8 → 9`. The `install` and `update` actions move their
`org.freedesktop.policykit.exec.path` annotation from `/usr/bin/xbps-install`
to `/usr/local/sbin/void-xbps-snap`. **Nothing else changes** — the action ids,
every `exec.argv1` (`-y` / `-y` / `-Syu`), the `auth_admin` defaults, the
backend sources and the template's build recipe are byte-identical to
revision 8. The `remove` action deliberately keeps `/usr/bin/xbps-remove`: the
installer's snapshot shadow only covers `xbps-install`, so `remove` already
matched.

**Why.** On every installer-provisioned box, `/usr/bin/xbps-install` and
`/usr/sbin/xbps-install` are symlinks to the installer-owned
`/usr/local/sbin/void-xbps-snap` (the snapper pre/post wrapper). pkexec
canonicalises the program path (`realpath()`) *before* it looks up an action,
so the old annotation could never match there: lookup fell through to pkexec's
generic fallback and the user saw raw-argv wording ("Authentication is required
to execute /usr/local/sbin/void-xbps-snap -y") instead of this store's own
"Authentication is required to install software" message. Annotating the
canonical path restores the store's own message.

**Failure mode is graceful, by design.** On a stock Void box (no shadow) the
canonicalised path is the real `/usr/bin/xbps-install`, never matches the
wrapper annotation, and lookup falls back to the same generic prompt as before.
A wrong-path annotation can only degrade the *wording*; it can never authorise
anything different, because every action stays `auth_admin` and the wrapper
passes its argv straight through.

Artefacts: template `revision=9`; policy
`org.kde.discover.xbps.policy`, sha256
`434f5a8fbd80908abae64b0282b39af07cd8adbe6a467f4c80194214c3a3cb8a`;
package `discover-6.7.5_9.x86_64.xbps`, 1643360 B, sha256
`4b4683cf4cfe2ff5d08249b322bd6141628f0226a92316a78cc274116e278598`.
The built plugin (`usr/lib/qt6/plugins/discover/xbps-backend.so`) is unchanged
in behaviour — this is policy data only. The signing key is unchanged
(DER sha256 `4e40218a…f17cf`).

---

## stores-r2 — 2026-10-26

A **metadata-only** re-cut of the served package so an installed box can be
identified at a glance, plus the revision-numbering rule written down where a
packager will actually read it.

### Served repository (root)

- The served package is now **`discover-6.7.5_8`** (`sha256 1e12fed3…`), built
  from `f95dbb2` with `tools/build-and-stage.sh` + `xbps-src` (a real rebuild,
  not a hand-edited repack) and signed by `tools/publish.sh`.
- The shipped plugin is **byte-identical** to `stores-r1`'s:
  `xbps-backend.so` md5 `bb385531f1ddf30acb59b2d1d2f79429`. Nothing in the
  backend changed — only `props.plist`.
- The signed `x86_64-repodata` is `sha256 3682bff9…` (2307 B) and lists
  **only** `discover-6.7.5_8`; it is byte-identical to the copy in this
  repository and to what Pages serves.
- `discover-6.7.5_7`'s `.xbps` and `.sig2` stay at the repository root —
  build numbers are not pruned (the cosmic store's monotonic-growth
  behaviour), so a client holding `_7` still resolves it while the index
  offers `_8`.
- Release tag `stores-r2` = `59a100d…`; signed with the same store key
  (DER sha256 `4e40218a…`), `signature-by = Void Linux Stores
  <noreply@github.com>`.

### `[DISCOVER-XBPS]` — revision 8

- **`short_desc` marker.** It now ends with ` (xbps backend)`, so
  `xbps-query -p short_desc discover` distinguishes this build from Void's
  stock flatpak-only one without comparing `pkgver`. That mattered in
  practice: after the `[DISCOVER-STORE-REPO-ORDER]` installer bug
  (installer `Version-3.2.01`), a box could hold the **stock** `_1` while the
  store was configured and reachable — the description is what makes that
  obvious.
- **Revision scheme documented, rebase rule included.** `_N` is xbps's
  per-repository revision: it has no cross-repo namespace, so a downstream
  rebuild must outrank upstream or clients compare equal and never upgrade.
  The alternative scheme (mirroring the upstream revision, e.g.
  `discover-6.7.5_1_0`) was **measured**, not guessed — `xbps-uhelper
  cmpver` reports `6.7.5_1_0 == 6.7.5` and `6.7.5_1_0 < 6.7.5_7`, i.e. it
  would be a downgrade while our revision is the high one. The flip side is
  recorded as the rule: a high `_N` also outranks a later *revision-only*
  upstream bump (`6.7.5_7 > 6.7.5_2`), so when Void moves their revision this
  template must be re-cut above it (their `_2` → our `_9`) and the "Based on
  …" line updated, or their fix is silently masked on every
  store-provisioned box. A new upstream *version* needs no such care. The
  rule lives next to `revision=` in `srcpkgs/discover/template`.

---

## stores-r1 — 2026-10-22

The first served release of this repository: KDE Discover with a native xbps
backend. The repository was published **directly** (no clean-room export, no
separate artifact repo) with `tools/publish.sh`, which signs the packages and
the index with `xbps-rindex`, copies the artifacts into the repository root,
commits, tags `stores-rN` and pushes.

### Served repository (root)

- The built and signed binary repository: 1 `.xbps` package + its `.sig2`
  signature and the signed `x86_64-repodata` index, served over GitHub Pages
  at <https://bluesloth.github.io/void-linux-discover/>.
- The served package is **`discover-6.7.5_7`** — revision 7, the exact build
  the operator validated on the box (the shipped `xbps-backend.so` md5
  `bb385531f1ddf30acb59b2d1d2f79429` matches the installed plugin).
- Signed with this store's **own** repository signing key (RSA-4096, DER sha256
  `4e40218a79784f0e4cc559b835c8e9f7585000d5b678177827a853a0937f17cf`). The
  COSMIC signing key stays with the COSMIC repository — one key per served
  repo, so a compromise or a rotation never couples two stores. The key
  itself is never committed; only its public half is pinned, in
  `coordination/PINS.md` §8 and (installer side) in the handoff §8 Q6.
- The index embeds that public key and `signature-by =
  Void Linux Stores <noreply@github.com>`; a client's trust anchor is the
  key's fingerprint in `/var/db/xbps/keys/<hexfp>.plist`, which is why the
  fingerprint — not the key — is the value worth pinning.
- Release tag `stores-r1` = `d2a027a…`; the served index is
  `sha256 e6c4631b…` and is byte-identical to the copy in this repository.

### `[DISCOVER-XBPS]` — KDE Discover with a native xbps backend

`discover-6.7.5_7` — the first template in this repository, and the first
store that can install xbps packages natively on Void.

**Revision 7 (2026-10-22)** — fixes the spurious **"Update Issue"** sheet
that appeared on *every* install and removal. The sheet is not driven by the
transaction status at all: Discover opens it from `onPassiveMessage`
(`DiscoverWindow.qml`), and its friendly text is the generic *"There was an
issue during the update or installation process. Please try again later."*
so **any** passive message raises it. The backend forwarded the helper's
output verbatim as passive messages, and xbps writes its whole progress
stream - download bars, unpacking, the summary line - to **stderr**, so the
sheet popped up on every transaction while the package was in fact installed
correctly (observed live on the box with `cmus` and `mplayer`: the packages
landed, the sheet still claimed a failure).

The output is now collected instead of forwarded, and follows the contract
the other native backends use (see `AlpineApkTransaction`): a **successful**
transaction says nothing at all, a **failed** one reports exactly one
message, marked `Error:`, carrying the tail of the helper's output - the
head is only the download log, the tail is where the reason is. pkexec's own
exit codes are named separately (126: authentication was cancelled, 127: the
helper could not be started) so the user is told what actually happened
instead of being sent looking in the wrong place.

**Revision 6 (2026-10-21)** — makes a failed attempt stop poisoning the
next one. `XbpsUpdater` never cleared its error message, so a recoverable
failure (a dismissed polkit prompt, a transient repository lock) left
Discover's "the update failed" text attached for the rest of the window's
lifetime: `start()` now resets the error message and a successful
transaction clears it as well. It also tells pkexec's exit codes apart
(126/127) from the package manager's own verdict.

**Revision 5 (2026-10-21)** — fixes the Updates tab sticking on
`(Fetching...)` forever. Discover's UI treats
`ResourcesModel.fetchingUpdatesProgress < 100` as "still fetching"
(`DiscoverDrawer.qml`, `DiscoverWindow.qml`); the backend's
`checkForUpdates()` dropped the progress to 0 before each probe but
`loadUpdates()` never drove it back to 100 nor emitted
`fetchingUpdatesProgressChanged()`, so the label never cleared (found
live on the box in a real GUI session after the operator's flatpak
update run). Both transitions now signal: `loadUpdates()` ends with
`m_fetchProgress = 100` + the notify emission (the probe is a
synchronous unprivileged dry run, so 0→100 with no intermediate steps
is the honest progress), and `checkForUpdates()` signals the reset to 0
for a re-check. No command, privilege or transaction change.

**Revision 4 (2026-10-21)** — the template now advertises this repository's
canonical maintainer identity, `Void Linux Stores <noreply@github.com>`,
instead of inheriting Void's own upstream identity
(`Void Linux <voidlinux@voidlinux.org>`). The `maintainer=` field is written
into the built package (`xbps-query -p maintainer`), so serving different
bytes under the already-used pkgver `6.7.5_3` would give one version two
byte-streams — hence the revision bump. Enforced from now on by
`tools/check-maintainer.sh`, which `tools/publish.sh` runs as a pre-flight.

**Revision 3 (2026-10-21)** — GUI walkthrough finding: Discover's Sources
page logged `Unable to assign [undefined] to int` once per repository row,
because the carried `XbpsSourcesBackend` model never set the `checkState`
role. Rows now report `Qt::Checked` — the repositories listed by
`xbps-query -L` are always active and cannot be toggled from Discover
(`addSource`/`removeSource` are unsupported) — so the checkbox renders
checked-and-disabled and the console is clean. Revision 2 was the first
GUI-validated build (real install of `htop` through the store, flatpak
coexistence confirmed).

**Why.** Void's `discover-6.7.5` ships only the `flatpak`, `fwupd` and `kns`
backends — there is **no xbps backend upstream at all**, and no PackageKit
backend is buildable on Void (Void's `PackageKit` builds only the dummy/test
backends and there is no `packagekit-qt6`). Void also ships no AppStream
catalogs, so a catalog-driven backend would have nothing to browse.
Discover's `search()` is query-driven, so a **single phase** delivers search,
installed, updates, install and remove — no AppStream dependency for the core
flows.

**Carried changes — the backend is carried as source files, not as a patch.**

`srcpkgs/discover/files/xbps/` holds the backend sources
(`XbpsBackend`, `XbpsResource`, `XbpsTransaction`, `XbpsUpdater`,
`XbpsSourcesBackend` + `CMakeLists.txt`, 11 files). The template's
`post_extract()` copies them into `libdiscover/backends/XbpsBackend/` and
appends the CMake block idempotently (guarded by
`grep -q "add_subdirectory(XbpsBackend)"`).

*Why not a unified patch:* a patch against upstream's `CMakeLists.txt` is
diff-context-bound, so every discover release bump invalidates it and forces
a rebase of a patch nobody can read any more. Carrying plain sources keeps
the bump mechanical.

**The xbps command map** (the backend's entire I/O surface):

| purpose | command | privilege |
|---|---|---|
| search | `xbps-query -Rs <term>` / `-s <term>` | none |
| installed | `xbps-query -l` | none |
| repositories | `xbps-query -L` | none |
| pending updates | `xbps-install -nSu` | none |
| install | `pkexec xbps-install -y -- <pkg>` | `auth_admin` |
| remove | `pkexec xbps-remove -y -- <pkg>` | `auth_admin` |
| system update | `pkexec xbps-install -Syu` | `auth_admin` |

**Never a bare `-Sy`/`-Syy`.** Other tools ship a polkit action that grants
*passwordless* execution for `/usr/bin/xbps-install` with `argv1=-Sy` (the
OctoXBPS NOPASS action). A bare `-Sy` would therefore sync the repositories
as root without asking anybody. The backend emits `-y` for install/remove and
`-Syu` for the system update, and the shipped polkit action annotates
`exec.argv1` accordingly.

**Privilege model:** `pkexec` plus a dedicated polkit policy
(`srcpkgs/discover/files/org.kde.discover.xbps.policy`, installed by the
template's `post_install()`) with three actions — `…xbps.install`,
`…xbps.remove`, `…xbps.update` — each `auth_admin`. The policy matches on
`exec.path` **and** `exec.argv1`, so the action that authorises a call is the
one whose argument shape was expected.

**Verified (2026-10-21)** against the built package with a headless Qt
harness that loads the shipped plugin (`xbps-backend.so`) through
`QPluginLoader`:

* the plugin loads with the expected IID and reports itself valid;
* the installed list matches `xbps-query -l` **exactly** (1171 packages, same
  names, versions and summaries);
* search returns **the same set** the CLI prints (checked for `qt6`,
  `firefox`, `python3`);
* the pending-update list comes from the unprivileged `-nSu`, and **zero**
  polkit prompts happen during cold start and during search;
* a real install and a real remove of `sl` succeed through `pkexec` → polkit
  → the action → `xbps-install -y -- sl` / `xbps-remove -y -- sl`;
* with the polkit rule that grants only our two action ids deleted, the
  identical command **blocks** waiting for authentication — proving our own
  action (not pkexec's generic default) is what authorises the call;
* with OctoXBPS's NOPASS policy and rules present, our `-y` argument still
  gets a real prompt — no NOPASS leak.

**Known limitations** (documented in the handoff): search-driven only (no
browse page), no per-resource details fetch (no `xbps-query -R`), no
AppStream metadata, phase-based progress only, and the polkit prompt's
literal rendering plus its Cancel gesture are still unverified on screen.

**Upstream tracking:** KDE Discover 6.7.5. The backend's plugin IID is
version-bound (`org.kde.discover.<version>.AbstractResourcesBackendFactory`)
and is derived from the version at build time.
