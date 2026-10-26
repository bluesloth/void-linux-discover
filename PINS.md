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

Latest release **`stores-r2`** (2026-10-26):

| artifact | value |
| --- | --- |
| served package | `discover-6.7.5_8.x86_64.xbps` (`sha256 1e12fed3…`) |
| package signature | `discover-6.7.5_8.x86_64.xbps.sig2` (RSA over sha256 of the package) |
| served index | `x86_64-repodata` (`sha256 3682bff9…`) |
| index identity | `signature-by = Void Linux Stores <noreply@github.com>` |
| index URL | `https://bluesloth.github.io/void-linux-discover/x86_64-repodata` |
| release tag | `stores-r2` = `59a100d…` |

`stores-r1` (2026-10-22, tag `d2a027a…`) served `discover-6.7.5_7`
(`sha256 26d2d646…`) with index `sha256 e6c4631b…`. Its `.xbps` and `.sig2`
remain at the repository root — build numbers are not pruned — but the index
lists only the newest build, so `_7` is history rather than a current
download.

`discover-6.7.5_8` is a **metadata-only** rebuild of `_7`: the shipped
`xbps-backend.so` is byte-identical (md5 `bb385531…`), so the runtime
validation recorded for `_7` carries over unchanged.

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

