# SPDX-License-Identifier: MIT
# Cross-file property xclipse_x11_sdk points at pipeline/prepare_x11.py's isolated output.
# Header/runtime ABI is checked by the final link; do not search a host pkg-config installation.
x11_inc = include_directories(xclipse_x11_sdk / 'include')
x11_libdir = xclipse_x11_sdk / 'lib'
xcb_lib = cc.find_library('xcb', dirs: x11_libdir)
xlib_lib = cc.find_library('X11', dirs: x11_libdir)
meson.override_dependency('xcb', declare_dependency(dependencies: xcb_lib, include_directories: x11_inc, version: '1.17.0'))
meson.override_dependency('x11-xcb', declare_dependency(dependencies: [xcb_lib, xlib_lib,
  cc.find_library('X11-xcb', dirs: x11_libdir), cc.find_library('android-sysvshm', dirs: x11_libdir)],
  include_directories: x11_inc, version: '1.8.0'))
foreach component : ['dri3', 'present', 'shm', 'sync', 'xfixes', 'randr']
  meson.override_dependency('xcb-' + component, declare_dependency(dependencies: [xcb_lib,
    cc.find_library('xcb-' + component, dirs: x11_libdir)], include_directories: x11_inc, version: '1.17.0'))
endforeach
meson.override_dependency('xshmfence', declare_dependency(
  dependencies: cc.find_library('xshmfence', dirs: x11_libdir), include_directories: x11_inc, version: '1.3'))
