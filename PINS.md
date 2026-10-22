# PINS - Void_Linux_Discover

| package | version | checksum |
| --- | --- | --- |
| discover | 6.7.5 | e9830fffebc54c781dcb076db4ab9462ee27ddd7fd3769403a211743636e1f67 |

The checksum is the **source distfile** sha256 taken from the package
template (`srcpkgs/discover/template`, `checksum=`) — the pin on the build
input, not the digest of the built `.xbps`. Same convention as the cosmic
store repo's `PINS.md`.

The served index (`x86_64-repodata`) is signed with the store's repository
signing key. The key is never committed; its public half is pinned in the
installer (`_discover_key_expected`, handoff §8 Q6).
