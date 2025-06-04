# Maintainer: Peter Jung <ptr1337@archlinux.org>
# Contributor: Lex Black <autumn-wind@web.de>

pkgname=labwc
pkgver=0.8.4
pkgrel=2
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
  libwlroots-0.18.so
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
b2sums=('ba631a9c5ff6cd1a4178620641dbb8823d20af90cc5b1c705ad0fa70786c6099e95cb04a5b18642aa203964b2bec418e0569b94b1a60543dfd7dbec866c9e0fa')


build() {
  arch-meson -Dman-pages=enabled "$pkgname" build
  meson compile -C build
}

package() {
  meson install -C build --destdir "$pkgdir"
}
