#!/bin/sh
# Upstream v7.2 release; build only, no phone access or tracing.
# Static/cross options documented in upstream INSTALL and configure --help.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SOURCE="$ROOT/refs/strace-7.2"
BUILD="$ROOT/refs/strace-7.2-build-aarch64"
test -r "$SOURCE/configure"
printf '%s  %s\n' \
  4bde6246926890dcee824f6e6ac42a06752f47d77e5097d86e3c0d6d4b709fe5 \
  "$ROOT/refs/strace-7.2.tar.xz" | sha256sum -c -
mkdir -p "$BUILD"
(
  cd "$BUILD"
  CC=aarch64-linux-gnu-gcc CPPFLAGS= CFLAGS='-Os -Wall -Wextra' \
    LDFLAGS='-static -pthread' "$SOURCE/configure" \
    --host=aarch64-linux-gnu --build="$("$SOURCE/build-aux/config.guess")" \
    --enable-mpers=no --enable-bundled=yes \
    --without-libdw --without-libunwind --without-libiberty \
    --without-libselinux
)
make -C "$BUILD" -j"${JOBS:-8}"
cp "$BUILD/src/strace" "$ROOT/refs/strace-static"
file "$ROOT/refs/strace-static"
sha256sum "$ROOT/refs/strace-static"
