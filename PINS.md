# PINS - Void_Linux_Discover

| package | version | checksum |
| --- | --- | --- |
| discover | 6.7.5 | e9830fffebc54c781dcb076db4ab9462ee27ddd7fd3769403a211743636e1f67 |

The checksum is the **source distfile** sha256 taken from the package
template (`srcpkgs/discover/template`, `checksum=`) — the pin on the build
input, not the digest of the built `.xbps`. Same convention as the cosmic
store repo's `PINS.md`.

The served index (`x86_64-repodata`) is signed with this store's own
repository signing key (RSA-4096, DER sha256
`4e40218a79784f0e4cc559b835c8e9f7585000d5b678177827a853a0937f17cf`). The key
is never committed — only the public fingerprint is pinned: in
`coordination/PINS.md` §8, and on the installer side in
`_discover_key_expected` (handoff §8 Q6).

## Served artifacts

Latest release **`stores-r5`** (2026-11-03):

| artifact | value |
| --- | --- |
| served package | `discover-6.7.5_10.x86_64.xbps` (1649510 B, `sha256 2b44d31a…b09a`) |
| package signature | `discover-6.7.5_10.x86_64.xbps.sig2` (RSA over sha256 of the package) |
| served index | `x86_64-repodata` (2305 B, `sha256 75050cf1…0b17`) |
| index identity | `signature-by = Void Linux Stores <noreply@github.com>` |
| index URL | `https://bluesloth.github.io/void-linux-discover/x86_64-repodata` |
| release tag | `stores-r5` = `c3fa0ff…` (commit `5fe8866…`) |

`stores-r4` (2026-11-03, commit `d37f092…`) published the same
`discover-6.7.5_10` package and signed it correctly, but its index named
`discover-6.7.5_9`: `tools/publish.sh` fed `xbps-rindex -f -a` a lexically
sorted file list, and `_10` sorts before `_7`/`_8`/`_9` while
`xbps-rindex -a` is last-wins. `stores-r5` is the correction — **no package
bytes changed**, only the regenerated, re-signed index, which now advertises
`_10`. The tooling fix (resolve the newest build per pkgname with
`xbps-uhelper cmpver`) is in `tools/publish.sh` and mirrored byte-for-byte in
the GNOME Software repository; see `CHANGELOG.md` for the full analysis.

`stores-r3` (2026-11-01, tag `890c125…`) served `discover-6.7.5_9`
(`sha256 4b4683cf…`, index `5bcb40e1…`); `stores-r2` (2026-10-26, tag
`59a100d…`) served `discover-6.7.5_8` (`sha256 1e12fed3…`, index
`3682bff9…`); `stores-r1` (2026-10-22, tag `d2a027a…`) served
`discover-6.7.5_7` (`sha256 26d2d646…`, index `e6c4631b…`). Their `.xbps` and
`.sig2` remain at the repository root — build numbers are not pruned — but the
index lists only the newest build, so `_7`–`_9` are history rather than
current downloads.

`discover-6.7.5_10` is the **`[XBPS-REFRESH]` release** (template
`revision=10`): `checkForUpdates()` now runs
`pkexec /usr/libexec/discover-xbps-refresh` — a fourth polkit action,
`allow_active=yes`, annotated on that package-owned argless helper — before
the `xbps-install -nSu` probe, because the probe answers from the cached
repodata and nothing on a stock Void box syncs it. The three transaction
actions keep `auth_admin` and their `argv1` annotations unchanged from
`revision=9`. The backend binary changes (plugin md5
`2a8b1525…6130`), so the runtime matrix recorded for `_7`–`_9` does **not**
carry over. It was re-run on a fresh KDE box on **2026-11-06** (installer
`Version-3.2.06`, this store's `discover-6.7.5_10` installed with
`xbps-query -p repository discover` naming the store URL): the refresh
`pkexec` is **promptless** (`org.kde.discover.xbps.refresh` =
`allow_active=yes`) and throttled by `/run/discover-xbps-refresh.stamp`
(exit 75 when throttled), a **cold start spawns exactly one** refresh
`pkexec` — the revision-10 addition, not a prompt — install and remove raise
the `auth_admin` dialogs and their action ids resolve through the `exec.path`
annotations, cancelling either install or remove leaves **exactly one**
"Update Issue" sheet with the UI still usable and the retry working, and a
held xbps lock parks a transaction indefinitely (`package database locked,
waiting...`) instead of surfacing an error. Full verdict table:
`docs/HANDOFF-KDE-DISCOVER-XBPS-BACKEND.md` §5.6.

`discover-6.7.5_8` is a **metadata-only** rebuild of `_7`: the shipped
`xbps-backend.so` is byte-identical (md5 `bb385531…`), so the runtime
validation recorded for `_7` carries over unchanged.

`discover-6.7.5_9` is the **`[POLKIT-SNAP-PATH]` re-cut** (template
`revision=9`): same backend binary again, with one policy change — the
`install` and `update` actions now annotate
`/usr/local/sbin/void-xbps-snap`, which is what both `/usr/bin/xbps-install`
and `/usr/sbin/xbps-install` are symlinked to on an
installer-provisioned box. pkexec canonicalises the program path *before*
action lookup, so the old `/usr/bin/xbps-install` annotation could never
match there and the prompt fell back to pkexec's generic raw-argv wording.
`remove` keeps `/usr/bin/xbps-remove` (the snapshot shadow covers
`xbps-install` only). Every action stays `auth_admin`, the `argv1`
annotations (`-y` / `-y` / `-Syu`) are unchanged, and the wrapper passes argv
through untouched — so an annotation can only change the **wording**, never
what is authorised. Policy file: sha256 `434f5a8f…cb8a`, shipped and carried
copy byte-identical. Since the `xbps-backend.so` is unchanged, every runtime
matrix verdict recorded for `_7`/`_8` carries over.

The release count of `.xbps` files at the repository root is four
(`_7`, `_8`, `_9`, `_10`); the index advertises `_10` only.

## Verifying a package signature

`.sig2` is a **raw RSA signature over the package's binary sha256** — it is not
an `openssl dgst -verify` over the hex digest, which is the form older project
notes recorded and which fails. The working check:

```sh
xbps-digest discover-*.x86_64.xbps > digest.hex
openssl dgst -sha256 -binary discover-*.x86_64.xbps > digest.bin
openssl pkeyutl -verify -pubin -inkey void-discover-repo-signing.pub.pem \
    -sigfile discover-*.x86_64.xbps.sig2 -in digest.bin -pkeyopt digest:sha256
# -> Signature Verified Successfully
```

(`xbps-install` performs the equivalent check itself and prints
`verifying RSA signature…` before unpacking.)

