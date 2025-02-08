# Maintainer: Peter Jung <ptr1337@archlinux.org>
# Contributor: Lex Black <autumn-wind@web.de>

pkgname=labwc
pkgver=0.8.2
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
b2sums=('849269d15966b4826b2e44a4154ad5027a21ca23084d4281c96be4f8ed4a15a5e825145065df61c579d53c7130d905df80eece0388847d592572c38030499d47')


build() {
  arch-meson -Dman-pages=enabled "$pkgname" build
  meson compile -C build
}

package() {
  meson install -C build --destdir "$pkgdir"
}
