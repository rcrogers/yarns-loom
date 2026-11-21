# QEMU alone, for the host's own architecture: the toolchain image is amd64,
# and on an arm64 host it runs emulated, QEMU with it. Ubuntu 22.04 carries
# the same QEMU (6.2) as the toolchain image. run.py builds it on first use.
#
# flashload.so, the plugin that tells flash reads from RAM ones, compiles
# against QEMU 6.2's own plugin header, which the package does not ship.
FROM ubuntu:22.04
RUN apt-get update \
 && apt-get install -y --no-install-recommends qemu-system-arm gzip gcc libc6-dev \
    curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL -o /usr/local/include/qemu-plugin.h \
    https://raw.githubusercontent.com/qemu/qemu/v6.2.0/include/qemu/qemu-plugin.h
COPY flashload.c /opt/oscprofile/flashload.c
RUN gcc -O2 -shared -fPIC -Wall -Werror -o /opt/oscprofile/flashload.so \
    /opt/oscprofile/flashload.c
