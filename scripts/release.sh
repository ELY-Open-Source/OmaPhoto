#!/usr/bin/env bash
# Builds a tag's packages and publishes a GitHub release.
set -euo pipefail
tag=${1:?"usage: scripts/release.sh vX.Y.Z"}
gh_repo=ELY-Open-Source/OmaPhoto
# gh must be logged in with write access to the repository.
gh auth status >/dev/null
repo=$(cd "$(dirname "$0")/.." && pwd)
cd "$repo"
version=${tag#v}
# The tag must name the version each build file carries.
if ! git show "$tag:CMakeLists.txt" | grep -qF "project(omaphoto VERSION $version "; then
    echo "CMakeLists.txt at $tag is not $version" >&2
    exit 1
fi
if ! git show "$tag:packaging/arch/PKGBUILD" | grep -qxF "pkgver=$version"; then
    echo "The PKGBUILD at $tag is not $version" >&2
    exit 1
fi
if ! git show "$tag:flake.nix" | grep -qF "version = \"$version\";"; then
    echo "flake.nix at $tag is not $version" >&2
    exit 1
fi
out=$repo/release/$tag
rm -rf "$out"
mkdir -p "$out"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
user="$(id -u):$(id -g)"
configure="cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DOMAPHOTO_BUNDLE_ORT=ON -DOMAPHOTO_MODEL=/usr/local/share/omaphoto/u2net.onnx && cmake --build build --target omaphoto"
# Each package builds from a fresh copy of the tag.
fresh() {
    rm -rf "$work/src"
    mkdir "$work/src"
    git archive "$tag" | tar -x -C "$work/src"
}
build() {
    docker run --rm -u "$user" -e HOME=/tmp -v "$work/src:/src" -w /src "$1" sh -c "$2"
}
# One DEB per Ubuntu LTS: each names its own libraries.
docker build -t omaphoto-dev --build-arg BASE=ubuntu:24.04 -f Dockerfile.dev .
docker build -t omaphoto-resolute --build-arg BASE=ubuntu:26.04 -f Dockerfile.dev .
for pair in noble:omaphoto-dev resolute:omaphoto-resolute; do
    fresh
    build "${pair#*:}" "$configure && cd build && cpack -G DEB"
    mv "$work"/src/build/omaphoto_*.deb "$out/omaphoto-${pair%%:*}_amd64.deb"
done
# One RPM per Fedora release: each names its own LibRaw.
docker build -t omaphoto-fedora --build-arg RELEASE=43 scripts/distro/fedora
docker build -t omaphoto-fedora44 --build-arg RELEASE=44 scripts/distro/fedora
for pair in 43:omaphoto-fedora 44:omaphoto-fedora44; do
    fresh
    build "${pair#*:}" "$configure && cd build && cpack -G RPM"
    mv "$work"/src/build/omaphoto-*.x86_64.rpm "$out/omaphoto-fc${pair%%:*}.x86_64.rpm"
done
# makepkg refuses root; a user of our id builds.
docker build --pull -t omaphoto-arch scripts/distro/arch
fresh
docker run --rm -v "$work/src:/src" omaphoto-arch sh -c "useradd -u $(id -u) builder && chown -R builder /src && su builder -c 'cd /src/packaging/arch && makepkg -f'"
mv "$work"/src/packaging/arch/omaphoto-[0-9]*-x86_64.pkg.tar.zst "$out/omaphoto-x86_64.pkg.tar.zst"
# The AppImage builds on Ubuntu 24.04, the oldest glibc here.
docker build -t omaphoto-appimage --build-arg DEV=omaphoto-dev scripts/distro/appimage
fresh
build omaphoto-appimage scripts/appimage.sh
mv "$work/src/omaphoto-x86_64.AppImage" "$out/"
names="omaphoto-noble_amd64.deb omaphoto-resolute_amd64.deb omaphoto-fc43.x86_64.rpm omaphoto-fc44.x86_64.rpm omaphoto-x86_64.pkg.tar.zst omaphoto-x86_64.AppImage"
(cd "$out" && sha256sum $names > SHA256SUMS)
# The tag must already be on GitHub.
notes="Install with the script for your system, or run the AppImage: see the README's Install section."
(cd "$out" && gh release create "$tag" --repo "$gh_repo" --verify-tag --title "OmaPhoto $version" --notes "$notes" $names SHA256SUMS)
echo "Published https://github.com/$gh_repo/releases/tag/$tag"
