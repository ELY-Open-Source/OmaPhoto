# The dev image plus linuxdeploy, pinned by checksum.
ARG DEV=omaphoto-dev
FROM $DEV
RUN curl -fsSL -o /usr/local/bin/linuxdeploy \
        https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    && curl -fsSL -o /usr/local/bin/linuxdeploy-plugin-qt \
        https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
    && echo "c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d  /usr/local/bin/linuxdeploy" | sha256sum -c - \
    && echo "15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724  /usr/local/bin/linuxdeploy-plugin-qt" | sha256sum -c - \
    && chmod +x /usr/local/bin/linuxdeploy /usr/local/bin/linuxdeploy-plugin-qt
