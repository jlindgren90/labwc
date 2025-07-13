# Maintainer: Peter Jung <ptr1337@archlinux.org>
# Contributor: Lex Black <autumn-wind@web.de>

pkgname=labwc
pkgver=0.9.0
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
b2sums=('dcf68ae5969db298478b35e6180321983b24793696f1519131ac288e93fa5d2211f9d7e1f45b23ad288e88b84deeff9241b090477b8146eee4ccd1429fc3adb9')


build() {
  arch-meson -Dman-pages=enabled "$pkgname" build
  meson compile -C build
}

package() {
  meson install -C build --destdir "$pkgdir"
}
