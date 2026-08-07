from debian:trixie

run apt update
run apt install -y meson \
  wayland-protocols \
  libxkbcommon-dev \
  libwayland-dev \
  libcairo2-dev \
  libgdk-pixbuf-2.0-dev \
  libcrypt-dev \
  libpam0g-dev

entrypoint sh
