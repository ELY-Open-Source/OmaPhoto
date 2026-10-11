#!/usr/bin/env bash
# Installs OmaPhoto's newest DEB on Ubuntu LTS and derivatives.
set -euo pipefail
repo=https://git.zacharyzhang.com/ZacharyZhang-NY/OmaPhoto
# The newest release's tag, from Gitea's API.
tag=$(curl -fsSL --proto '=https' https://git.zacharyzhang.com/api/v1/repos/ZacharyZhang-NY/OmaPhoto/releases/latest | grep -o '"tag_name":"v[^"]*"' | cut -d'"' -f4) || tag=
if [ -z "$tag" ]; then
    echo "No release found at $repo/releases." >&2
    exit 1
fi
base=$repo/releases/download/$tag
. /etc/os-release
# Each DEB names its LTS's libraries; derivatives name their base.
case "${UBUNTU_CODENAME:-}" in
noble | resolute) ;;
*)
    echo "This script is for Ubuntu 24.04 or 26.04 and systems built on them; this system is ${PRETTY_NAME:-unknown}." >&2
    exit 1
    ;;
esac
package=omaphoto-${UBUNTU_CODENAME}_amd64.deb
if [ "$(uname -m)" != x86_64 ]; then
    echo "OmaPhoto's packages are built for x86_64 alone." >&2
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
curl -fL --proto '=https' -o "$work/$package" "$base/$package"
curl -fsSL --proto '=https' -o "$work/SHA256SUMS" "$base/SHA256SUMS"
# The package must match the checksum published beside it.
(cd "$work" && grep " $package\$" SHA256SUMS | sha256sum -c -)
# apt reads the file as its own user.
chmod a+rx "$work"
sudo apt-get install -y "$work/$package"
