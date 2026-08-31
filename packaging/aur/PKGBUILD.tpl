# Maintainer: Manuel Spagnolo <shikaan@disroot.org>

# Template. The 'publish' job in .github/workflows/release.yml renders
# this into the PKGBUILD attached to each release; packaging/build.sh
# renders it for local test builds. This file is not a usable PKGBUILD.
pkgver=##VERSION##
sha256sums=('##CHECKSUM##')
_commit=##SHA##

pkgname=dewlock
pkgrel=1
pkgdesc="A minimal, beautiful screen locker for Wayland"
arch=('x86_64' 'aarch64')
url="https://github.com/shikaan/dewlock"
license=('MIT')
depends=('wayland' 'libxkbcommon' 'cairo' 'pam')
# wayland-scanner ships in 'wayland', which is already a runtime dependency.
makedepends=('scdoc')
backup=('etc/pam.d/dewlock')
source=("$pkgname-$pkgver.tar.gz::$url/archive/refs/tags/v$pkgver.tar.gz")

build() {
  cd "$pkgname-$pkgver"
  make VERSION="v$pkgver" SHA="$_commit" ERR_ON_WARN=0 all
}

package() {
  cd "$pkgname-$pkgver"
  make install VERSION="v$pkgver" SHA="$_commit" DESTDIR="$pkgdir" PREFIX=/usr
  install -Dm644 LICENSE "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
