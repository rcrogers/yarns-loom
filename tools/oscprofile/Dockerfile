# QEMU alone, for the host's own architecture: the toolchain image is amd64,
# and on an arm64 host it runs emulated, QEMU with it. Ubuntu 22.04 carries
# the same QEMU (6.2) as the toolchain image. run.py builds it on first use.
FROM ubuntu:22.04
RUN apt-get update \
 && apt-get install -y --no-install-recommends qemu-system-arm gzip \
 && rm -rf /var/lib/apt/lists/*
