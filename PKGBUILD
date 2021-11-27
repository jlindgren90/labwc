# Maintainer: Lex Black <autumn-wind@web.de>

pkgname=labwc
pkgver=0.7.2
pkgrel=1
pkgdesc='stacking wayland compositor with look and feel from openbox'
url="https://github.com/labwc/labwc"
arch=('x86_64')
license=('GPL2')
depends=('libpng' 'librsvg' 'pango' 'seatd' 'wayland' 'xorg-xwayland')
makedepends=('meson' 'scdoc' 'wayland-protocols')
optdepends=("bemenu: default launcher via Alt+F3")

prepare() {
  cd ..
  meson subprojects download wlroots
}

build() {
  cd ..
  arch-meson -Dman-pages=enabled build
  meson compile -C build
}

package() {
  cd ..
  meson install -C build --destdir "$pkgdir" --skip-subprojects
}
