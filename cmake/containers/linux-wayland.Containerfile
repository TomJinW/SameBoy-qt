# Runs the Linux AppImage on a Mac through Cocoa-Way (Apple Container + Waypipe).
# Build: container build --arch arm64 --tag sameboy-qt-wayland:arm64 --file cmake/containers/linux-wayland.Containerfile cmake/containers
FROM sameboy-qt-linux-builder:debian13-arm64

RUN apt-get update && apt-get install -y --no-install-recommends \
        waypipe libgl1-mesa-dri libegl-mesa0 libgbm1 libxkbcommon0 \
    && rm -rf /var/lib/apt/lists/*

ENV QT_QPA_PLATFORM=wayland \
    LIBGL_ALWAYS_SOFTWARE=1 \
    APPIMAGE_EXTRACT_AND_RUN=1
