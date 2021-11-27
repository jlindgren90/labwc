# Maintainer: Lex Black <autumn-wind@web.de>

pkgname=labwc
pkgver=0.6.3
pkgrel=1
pkgdesc='stacking wayland compositor with look and feel from openbox'
url="https://github.com/labwc/labwc"
arch=('x86_64')
license=('GPL2')
depends=('pango' 'wayland' 'wlroots>=0.16' 'wlroots<0.17' 'xorg-xwayland')
makedepends=('meson' 'scdoc' 'wayland-protocols')
optdepends=("bemenu: default launcher via Alt+F3")

build() {
  cd ..
  arch-meson -Dman-pages=enabled build
  meson compile -C build
}

package() {
  cd ..
  meson install -C build --destdir "$pkgdir"
}
