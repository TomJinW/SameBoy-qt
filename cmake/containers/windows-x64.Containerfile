# Cross-compiles the Windows x64 build with mingw-w64. Qt's prebuilt MinGW kit supplies the target libraries;
# a Linux Qt kit of the same version supplies moc, rcc, uic and qsb (QT_HOST_PATH). aqtinstall downloads both.
FROM debian:trixie-slim
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates cmake ninja-build pkgconf python3 python3-venv python3-pip curl file xz-utils p7zip-full \
    g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix mingw-w64-tools \
    libgl1 libegl1 libxkbcommon0 libfontconfig1 libglib2.0-0 libdbus-1-3 libxcb-cursor0 \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/aqt && /opt/aqt/bin/pip install aqtinstall
WORKDIR /workspace
