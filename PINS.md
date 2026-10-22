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

## Served artifacts (release `stores-r1`)

| artifact | value |
| --- | --- |
| served package | `discover-6.7.5_7.x86_64.xbps` (`sha256 26d2d646…`) |
| package signature | `discover-6.7.5_7.x86_64.xbps.sig2` (RSA over sha256 of the package) |
| served index | `x86_64-repodata` (`sha256 e6c4631b…`) |
| index identity | `signature-by = Void Linux Stores <noreply@github.com>` |
| index URL | `https://bluesloth.github.io/void-linux-discover/x86_64-repodata` |

