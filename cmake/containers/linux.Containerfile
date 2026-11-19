# Debian 13 provides Qt 6.8 with matching private headers on arm64 and amd64.
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ca-certificates cmake ninja-build pkgconf python3 \
    curl file patchelf squashfs-tools desktop-file-utils \
    qt6-base-dev qt6-base-private-dev qt6-multimedia-dev \
    qt6-declarative-dev-tools qt6-shadertools-dev qt6-shader-baker qt6-l10n-tools qt6-qpa-plugins qt6-wayland \
    libudev-dev libusb-1.0-0-dev libhidapi-dev \
    xvfb xauth libgl1-mesa-dri libglx-mesa0 libegl1 fonts-dejavu-core \
    nasm xz-utils zlib1g-dev libva-dev libopenh264-dev libvpx-dev libopus-dev \
    && rm -rf /var/lib/apt/lists/*
# QtSvg can be a transitive Wayland dependency; deployment also needs its plugins.
RUN apt-get update && apt-get install -y --no-install-recommends qt6-svg-plugins \
    && rm -rf /var/lib/apt/lists/*
# Debian's FFmpeg is a GPL build (libx264/libx265). Build an LGPL one with the same major version
# (libavcodec.so.61) so Qt's FFmpeg plugin uses it instead; nothing GPL or nonfree is enabled.
# LD_LIBRARY_PATH makes it win for builds, tests and AppImage dependency resolution alike.
ARG FFMPEG_VERSION=7.1.5
ARG FFMPEG_SHA256=de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f
RUN curl -fL --retry 2 -o /tmp/ffmpeg.tar.xz https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VERSION}.tar.xz \
    && echo "${FFMPEG_SHA256}  /tmp/ffmpeg.tar.xz" | sha256sum -c - \
    && tar -xf /tmp/ffmpeg.tar.xz -C /tmp && cd /tmp/ffmpeg-${FFMPEG_VERSION} \
    && ./configure --prefix=/opt/ffmpeg-lgpl --enable-shared --disable-static --enable-pic \
        --disable-gpl --disable-version3 --disable-nonfree --disable-autodetect --disable-programs --disable-doc \
        --enable-zlib --enable-vaapi --enable-libopenh264 --enable-libvpx --enable-libopus \
    && make -j"$(nproc)" && make install \
    && strings -a /opt/ffmpeg-lgpl/lib/libavutil.so.59 | grep -q "libavutil license: LGPL" \
    && rm -rf /tmp/ffmpeg*
ENV LD_LIBRARY_PATH=/opt/ffmpeg-lgpl/lib
# No guest GPU is required for the Xvfb GUI/shader smoke tests.
ENV LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /workspace
