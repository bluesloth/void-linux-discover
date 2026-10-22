#!/bin/sh
# sync-upstream.sh - inert in this repository: the carried templates are not
# epoch-tagged and each tracks its own upstream independently (there is no
# shared baseline to re-cut against, unlike the COSMIC epoch tooling).
echo "sync-upstream: no-op in this repository (non-epoch templates)"
exit 0
