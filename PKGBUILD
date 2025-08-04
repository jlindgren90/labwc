# Maintainer: Peter Jung <ptr1337@archlinux.org>
# Contributor: Lex Black <autumn-wind@web.de>

pkgname=labwc
pkgver=0.9.1
pkgrel=1
pkgdesc='stacking wayland compositor with look and feel from openbox'
url="https://github.com/labwc/labwc"
arch=('x86_64')
license=('GPL-2.0-only')
depends=(
  cairo
  glib2
  glibc
  libinput
  libpng
  librsvg
  libsfdo
  libwlroots-0.19.so
  libxcb
  libxkbcommon
  libxml2
  pango
  pixman
  seatd
  ttf-font
  wayland
  xorg-xwayland
)
makedepends=(
  git
  meson
  scdoc
  wayland-protocols
)
optdepends=("bemenu: default launcher via Alt+F3")
source=("git+https://github.com/labwc/labwc#tag=${pkgver}")
b2sums=('3c1a2eb42f86ab2859ed746ba7e836089446f718a4b31467c509a0d7eebb964219768a45d7138c4802c44dd6f9a5024084097292bb3c1e7cd2e3eee8ed331417')


build() {
  arch-meson -Dman-pages=enabled "$pkgname" build
  meson compile -C build
}

package() {
  meson install -C build --destdir "$pkgdir"
}
