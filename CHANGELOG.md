# Changelog

All notable changes to **Void Linux Stores** — the `xbps-src` packaging
sources for the desktop software stores on Void Linux — are documented in this
file.

## What this repository is

One repository, one template per store, each pinned to **its own** upstream
release (KDE Discover, GNOME Software, …). The stores have **no shared
version mechanism** with each other or with the COSMIC epoch tooling, so
they are isolated by default; what they genuinely share — one xbps command
map, one privilege idiom, one packaging pattern — lives here once.

The cosmic-store backend is **not** here: it stays in
`private/void-linux-cosmic/`, because that repository is organised by
upstream provenance (the COSMIC epoch tags) and its tooling manages it.

## Status

**Source only so far.** The templates build, but nothing is published from
this repository yet: there is no served binary repository and no public
export. A release, when it comes, must carry the re-signing caution below.

## Release model

Each store's template carries its own upstream `version=` and its own
`revision=`; a store's release is identified by `<upstream-version>_<revision>`.
Tracking a new upstream release is `tools/sync-upstream.sh`.

**Caution — repodata must be re-signed.** Any repack of this repository's
repodata MUST be signed with the repository signing key. An unsigned repack
makes the installer's trust-anchor check FATAL (see the 2026-10-09 incident
in the COSMIC repository: a repack that dropped the signature left the index
unsigned and every install aborted).

## Unreleased

### `[DISCOVER-XBPS]` — KDE Discover with a native xbps backend

`discover-6.7.5_2` — the first template in this repository, and the first
store that can install xbps packages natively on Void.

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
