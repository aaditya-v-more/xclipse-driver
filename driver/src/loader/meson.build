# SPDX-License-Identifier: MIT
# Only screen-resource helpers are needed by this Vulkan-only tree.
libloader = static_library(
  'loader', 'loader_dri_helper_screen.c',
  gnu_symbol_visibility: 'hidden',
  include_directories: [inc_include, inc_src],
  dependencies: [idep_mesautil, dep_thread, dep_xcb, dep_xcb_xrandr],
  build_by_default: false,
)
