# Licensing

`void-linux-discover` is a binary **xbps package repository** (`.xbps` packages +
the signed `x86_64-repodata` index) whose packaging templates live in
`srcpkgs/`.

The repository itself carries no single license; each package's license is
declared in its `srcpkgs/<pkg>/template` `license=` field — for `discover`
that is `GPL-2.0-or-later`.

The template is built on top of upstream KDE Discover sources
(`https://download.kde.org/stable/plasma/`). The xbps backend under
`srcpkgs/discover/files/xbps/` and the polkit action under
`srcpkgs/discover/files/` are original work for this project, distributed
under the same terms.
