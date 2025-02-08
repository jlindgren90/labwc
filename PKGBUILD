# Maintainer: Peter Jung <ptr1337@archlinux.org>
# Contributor: Lex Black <autumn-wind@web.de>

pkgname=labwc
pkgver=0.8.4
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
  libxcb
  libxkbcommon
  libxml2
  pango
  pixman
  seatd
  ttf-font
  wayland
  xorg-xwayland
  # wlroots
  lcms2
  libdisplay-info
  libglvnd
  libliftoff
  libudev.so
  libvulkan.so
  opengl-driver
  xcb-util-errors
  xcb-util-renderutil
  xcb-util-wm
)
makedepends=(
  git
  meson
  scdoc
  wayland-protocols
  # wlroots
  glslang
  systemd
  vulkan-headers
)
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
