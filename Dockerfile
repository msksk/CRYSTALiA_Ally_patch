# Build a 32-bit Windows d3d9.dll with Zig (Linux container).
#
# Usage (from this directory):
#   docker build --output "type=local,dest=./out" .
#
# The exported file is out/d3d9.dll.

FROM debian:bookworm-slim AS build

# Zig release used for cross-compiling. When changing the version, update
# ZIG_SHA256 as well (see https://ziglang.org/download/index.json).
ARG ZIG_VERSION=0.17.1
ARG ZIG_SHA256=3581863e3cbcfd0fce0efc682ce7e9e93da96668a977791d356a26d2a7e9b17a

RUN set -eux; \
    apt-get update; \
    apt-get install -y --no-install-recommends ca-certificates curl xz-utils; \
    rm -rf /var/lib/apt/lists/*; \
    url="https://ziglang.org/download/${ZIG_VERSION}/zig-x86_64-linux-${ZIG_VERSION}.tar.xz"; \
    curl -fsSL --retry 3 -o /tmp/zig.tar.xz "$url"; \
    echo "${ZIG_SHA256}  /tmp/zig.tar.xz" | sha256sum -c -; \
    mkdir -p /opt/zig; \
    tar -xJf /tmp/zig.tar.xz -C /opt/zig --strip-components=1; \
    rm -f /tmp/zig.tar.xz; \
    ln -s /opt/zig/zig /usr/local/bin/zig; \
    zig version

WORKDIR /src

COPY d3d9proxy_final.c d3d9.def ./

RUN set -eux; \
    mkdir -p /out; \
    zig cc --target=x86-windows-gnu -shared -O2 -o /out/d3d9.dll d3d9proxy_final.c d3d9.def

# Export only the DLL. Used with:
#   docker build --output "type=local,dest=./out" .
FROM scratch AS export

COPY --from=build /out/d3d9.dll /d3d9.dll
