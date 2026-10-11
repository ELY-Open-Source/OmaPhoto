#!/usr/bin/env bash
# Installs OmaPhoto on NixOS from its flake.
set -euo pipefail
if ! command -v nix >/dev/null; then
    echo "This script needs Nix; NixOS carries it." >&2
    exit 1
fi
if [ "$(uname -m)" != x86_64 ]; then
    echo "OmaPhoto's flake builds for x86_64 alone." >&2
    exit 1
fi
repo=https://git.zacharyzhang.com/ZacharyZhang-NY/OmaPhoto
# The newest release's tag, from Gitea's API.
tag=$(curl -fsSL --proto '=https' https://git.zacharyzhang.com/api/v1/repos/ZacharyZhang-NY/OmaPhoto/releases/latest | grep -o '"tag_name":"v[^"]*"' | cut -d'"' -f4) || tag=
if [ -z "$tag" ]; then
    echo "No release found at $repo/releases." >&2
    exit 1
fi
nix --extra-experimental-features "nix-command flakes" profile install "git+$repo?ref=refs/tags/$tag"
