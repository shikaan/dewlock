from debian:trixie as base

run apt update
run apt install -y \
  build-essential \
  pkg-config \
  meson \
  ninja-build \
  git \
  libxkbcommon-dev \
  libwayland-dev \
  libcairo2-dev \
  libcrypt-dev \
  libpam0g-dev

entrypoint bash

from base as tooling

run apt update
run apt install -y \
  clangd
workdir /src
entrypoint bash
