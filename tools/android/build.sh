#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
. tools/release/deps.sh
if [ -z "${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}" ]; then
    echo "Set ANDROID_HOME or ANDROID_SDK_ROOT to an Android SDK with platform 35 and an NDK." >&2
    exit 1
fi
release_deps ""
if ! java -version >/dev/null 2>&1; then
    echo "Java 17 or newer is required to build the Android app." >&2
    exit 1
fi
if [ "$#" -eq 0 ]; then set -- assembleDebug; fi
exec "$SDL_SRC/android-project/gradlew" -p "$PWD/tools/android" "$@"
