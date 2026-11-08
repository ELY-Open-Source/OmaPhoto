# The AppImage's model, MIME data, lenses and HEVC decoder first.
export XDG_DATA_DIRS="$APPDIR/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export LIBHEIF_PLUGIN_PATH="$APPDIR/usr/lib/libheif"
