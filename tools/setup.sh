#!/bin/sh
# Sets up this project's portable toolchain in tools/ on macOS or Linux, with no installers and
# no sudo: Android NDK r29, Python 3.13.16 with meson/ninja/mako/packaging/pyyaml/pymsi, and
# glslang 16.6.0. It also installs ADB (platform-tools) to ~/platform-tools when adb isn't on PATH.
# The Windows equivalent is tools\setup.ps1; both produce the same tools/ layout.
#
# Every download is checked against a pinned checksum: the NDK against Google's published SHA-1,
# Python and glslang against GitHub's SHA-256 asset digests. Tools already present are skipped,
# so it's safe to run again.
#
# usage: sh tools/setup.sh
set -eu

TOOLS="$(cd "$(dirname "$0")" && pwd)"
DOWNLOADS="$TOOLS/_downloads"

case "$(uname -s)-$(uname -m)" in
   Darwin-arm64)
      NDK_ZIP=android-ndk-r29-darwin.zip; NDK_SHA1=03d29fbb57e3c05a7d53597dd011d856c1456a4f
      PY_TGZ=cpython-3.13.16+20261009-aarch64-apple-darwin-install_only.tar.gz
      PY_SHA256=3338fcd572a670ec45ac9ef75480b95595ea9b6b5dd320952fcf5df185089941
      GL_ZIP=glslang-16.6.0-macos-universal-release.zip
      GL_SHA256=1086bc6c7ce8ff188e4e1d1a69078e9e38156a4c2c3f13e4f91b9289a1d90453
      PT_ZIP=platform-tools-latest-darwin.zip ;;
   Darwin-x86_64)
      NDK_ZIP=android-ndk-r29-darwin.zip; NDK_SHA1=03d29fbb57e3c05a7d53597dd011d856c1456a4f
      PY_TGZ=cpython-3.13.16+20261009-x86_64-apple-darwin-install_only.tar.gz
      PY_SHA256=1c3ecfe6c0c1c200508ff9f93b436e3d8125faf9ff9e70a9dee65699593297da
      GL_ZIP=glslang-16.6.0-macos-universal-release.zip
      GL_SHA256=1086bc6c7ce8ff188e4e1d1a69078e9e38156a4c2c3f13e4f91b9289a1d90453
      PT_ZIP=platform-tools-latest-darwin.zip ;;
   Linux-x86_64)
      NDK_ZIP=android-ndk-r29-linux.zip; NDK_SHA1=87e2bb7e9be5d6a1c6cdf5ec40dd4e0c6d07c30b
      PY_TGZ=cpython-3.13.16+20261009-x86_64-unknown-linux-gnu-install_only.tar.gz
      PY_SHA256=1cca1041f693f110a2e0d16417090b8996258c7a121e90eeab66c2f40ea407f2
      GL_ZIP=glslang-16.6.0-linux-x86_64-release.zip
      GL_SHA256=a3fc4f083b1793eb53e55fa3577ac9649ffbe0340715e30d116290fb5382393f
      PT_ZIP=platform-tools-latest-linux.zip ;;
   *) echo "error: unsupported host $(uname -s) $(uname -m); on Windows use tools\\setup.ps1" >&2; exit 1 ;;
esac

fetch() { # url name -> path
   mkdir -p "$DOWNLOADS"
   curl -sS --fail --location --retry 5 --retry-delay 5 -C - -o "$DOWNLOADS/$2" "$1"
   echo "$DOWNLOADS/$2"
}

check() { # algorithm expected file
   got="$(shasum -a "$1" "$3" | cut -d' ' -f1)"
   if [ "$got" != "$2" ]; then
      echo "error: checksum mismatch for $(basename "$3"): expected $2, got $got" >&2
      exit 1
   fi
}

# Android NDK r29, the version the driver port is tested with.
if [ -f "$TOOLS/android-ndk-r29/source.properties" ]; then
   echo "NDK r29: present"
else
   zip="$(fetch "https://dl.google.com/android/repository/$NDK_ZIP" "$NDK_ZIP")"
   check 1 "$NDK_SHA1" "$zip"
   unzip -q "$zip" -d "$TOOLS"
   rm -f "$zip"
   echo "NDK r29: installed (SHA-1 matches Google's)"
fi

# Python 3.13.16, a relocatable build from python-build-standalone.
PY="$TOOLS/python/bin/python3"
if [ -x "$PY" ]; then
   echo "Python: present"
else
   tgz="$(fetch "https://github.com/astral-sh/python-build-standalone/releases/download/20261009/$(echo "$PY_TGZ" | sed 's/+/%2B/')" python.tar.gz)"
   check 256 "$PY_SHA256" "$tgz"
   tar -xzf "$tgz" -C "$TOOLS"
   rm -f "$tgz"
   echo "Python 3.13.16: installed (SHA-256 matches GitHub's)"
fi
"$PY" -m pip install --disable-pip-version-check --quiet meson==1.12.1 ninja==1.13.2 mako==1.4.3 packaging==26.3 pyyaml==6.0.3 pymsi==0.0.0rc1
echo "Python packages: meson, ninja, mako, packaging, pyyaml, pymsi"

# glslang 16.6.0. Mesa's build calls it glslangValidator.
if [ -x "$TOOLS/glslang/bin/glslangValidator" ]; then
   echo "glslang: present"
else
   zip="$(fetch "https://github.com/KhronosGroup/glslang/releases/download/16.6.0/$GL_ZIP" "$GL_ZIP")"
   check 256 "$GL_SHA256" "$zip"
   mkdir -p "$TOOLS/glslang"
   unzip -qo "$zip" -d "$TOOLS/glslang"
   rm -f "$zip"
   [ -e "$TOOLS/glslang/bin/glslangValidator" ] || cp "$TOOLS/glslang/bin/glslang" "$TOOLS/glslang/bin/glslangValidator"
   echo "glslang 16.6.0: installed (SHA-256 matches GitHub's)"
fi

# ADB, unless one is already on PATH.
if command -v adb >/dev/null 2>&1; then
   echo "adb: on PATH ($(command -v adb))"
else
   if [ ! -x "$HOME/platform-tools/adb" ]; then
      zip="$(fetch "https://dl.google.com/android/repository/$PT_ZIP" platform-tools.zip)"
      unzip -qo "$zip" -d "$HOME"
      rm -f "$zip"
   fi
   if [ "$(uname -s)" = Darwin ] && ! codesign -dv "$HOME/platform-tools/adb" 2>&1 | grep -q 'Google LLC'; then
      echo "error: ~/platform-tools/adb isn't signed by Google LLC" >&2
      exit 1
   fi
   echo "adb: ~/platform-tools/adb. pipeline/run.py finds it there; tools/env.sh adds it to PATH."
fi

rm -rf "$DOWNLOADS"
echo "Done. Build the driver with: . tools/env.sh && driver/build.sh"
