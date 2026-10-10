# Puts this project's portable build tools on PATH for the current shell only (macOS/Linux).
# Usage: . tools/env.sh        (the Windows equivalent is: call tools\env.cmd)
if [ -n "${BASH_SOURCE:-}" ]; then XD_TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
elif [ -n "${ZSH_VERSION:-}" ]; then XD_TOOLS="$(cd "$(dirname "${(%):-%x}")" && pwd)"
else XD_TOOLS="$(cd "$(dirname "$0")" && pwd)"; fi
export XD_TOOLS
export ANDROID_NDK_HOME="$XD_TOOLS/android-ndk-r29"
export PATH="$XD_TOOLS/python/bin:$XD_TOOLS/glslang/bin:$HOME/platform-tools:$PATH"
