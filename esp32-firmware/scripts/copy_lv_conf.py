"""
Pre-build script: copy src/lv_conf.h next to the lvgl library folder.

LVGL's lv_conf_internal.h tries three locations in order:
  1. LV_CONF_PATH  (not set here)
  2. LV_CONF_INCLUDE_SIMPLE  -> #include "lv_conf.h" via -I search paths
  3. Fallback: #include "../../lv_conf.h" relative to lvgl/src/

Option 2 (LV_CONF_INCLUDE_SIMPLE + -I src) silently fails on the Xtensa
GCC 8.x toolchain due to a known __has_include bug
(https://gcc.gnu.org/bugzilla/show_bug.cgi?id=80753).
When it fails, lv_conf_internal.h defaults all LV_FONT_MONTSERRAT_* to 0,
the font symbols are never emitted, and linking ui.cpp fails.

This script enables option 3 (the reliable fallback) by placing a copy of
lv_conf.h at .pio/libdeps/<env>/lv_conf.h, which resolves to exactly
"../../lv_conf.h" as seen from any file inside lvgl/src/.
"""

import shutil
import os

Import("env")  # noqa: F821  (SCons environment injected by PlatformIO)

src_path = os.path.join(env["PROJECT_SRC_DIR"], "lv_conf.h")
dst_dir  = os.path.join(env["PROJECT_LIBDEPS_DIR"], env["PIOENV"])
dst_path = os.path.join(dst_dir, "lv_conf.h")

os.makedirs(dst_dir, exist_ok=True)
shutil.copy2(src_path, dst_path)
print("[PRE] lv_conf.h copied to", dst_path)
