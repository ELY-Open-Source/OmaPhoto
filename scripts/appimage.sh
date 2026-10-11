#!/usr/bin/env bash
# Builds omaphoto-x86_64.AppImage inside the AppImage image.
set -euo pipefail
id=io.github.ZacharyZhang_NY.OmaPhoto
cmake -S . -B build-appimage -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DOMAPHOTO_BUNDLE_ORT=ON -DOMAPHOTO_MODEL=/usr/local/share/omaphoto/u2net.onnx
cmake --build build-appimage --target omaphoto
rm -rf AppDir
DESTDIR="$PWD/AppDir" cmake --install build-appimage
# libheif loads its HEVC decoder as a plugin.
mkdir -p AppDir/usr/lib/libheif AppDir/apprun-hooks
cp /usr/lib/x86_64-linux-gnu/libheif/plugins/libheif-libde265.so AppDir/usr/lib/libheif/
cp packaging/appimage/omaphoto.sh AppDir/apprun-hooks/
# Qt's Wayland client needs its shell and decoration plugins.
qtplugins=/usr/lib/x86_64-linux-gnu/qt6/plugins
mkdir -p AppDir/usr/plugins
for dir in wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
    cp -r "$qtplugins/$dir" AppDir/usr/plugins/
done
export QMAKE=/usr/lib/qt6/bin/qmake6 APPIMAGE_EXTRACT_AND_RUN=1 OUTPUT=omaphoto-x86_64.AppImage
export EXTRA_PLATFORM_PLUGINS="libqwayland-egl.so;libqwayland-generic.so" EXTRA_QT_MODULES=svg
linuxdeploy --appdir AppDir --plugin qt --output appimage \
    --desktop-file "AppDir/usr/share/applications/$id.desktop" \
    --icon-file "AppDir/usr/share/icons/hicolor/512x512/apps/$id.png" \
    --deploy-deps-only AppDir/usr/lib/libheif \
    --deploy-deps-only AppDir/usr/plugins/wayland-shell-integration \
    --deploy-deps-only AppDir/usr/plugins/wayland-decoration-client \
    --deploy-deps-only AppDir/usr/plugins/wayland-graphics-integration-client
