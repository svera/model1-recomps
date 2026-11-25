#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."

: "${ANDROID_VERSION_CODE:?Set ANDROID_VERSION_CODE}"
: "${ANDROID_VERSION_NAME:?Set ANDROID_VERSION_NAME}"
: "${ANDROID_KEYSTORE_BASE64:?Set ANDROID_KEYSTORE_BASE64}"
: "${ANDROID_KEYSTORE_PASSWORD:?Set ANDROID_KEYSTORE_PASSWORD}"
: "${ANDROID_KEY_ALIAS:?Set ANDROID_KEY_ALIAS}"
: "${ANDROID_KEY_PASSWORD:?Set ANDROID_KEY_PASSWORD}"
sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
: "${sdk:?Set ANDROID_HOME or ANDROID_SDK_ROOT}"
build_tools="$sdk/build-tools/35.0.0"

umask 077
signing_dir=$(mktemp -d "${TMPDIR:-/tmp}/model1-android-sign.XXXXXX")
cleanup() {
    rm -f "$signing_dir/keystore" "$signing_dir/aligned.apk"
    rmdir "$signing_dir"
}
trap cleanup EXIT
printf '%s' "$ANDROID_KEYSTORE_BASE64" | base64 -d > "$signing_dir/keystore"

tools/android/build.sh assembleVrRelease assembleSwaRelease assembleWingwarRelease --max-workers=3
mkdir -p build/android-release
for game in vr swa wingwar; do
    case "$game" in
        vr) name=VirtuaRacingRecomp-android.apk ;;
        swa) name=StarWarsArcadeRecomp-android.apk ;;
        wingwar) name=WingWarRecomp-android.apk ;;
    esac
    unsigned="tools/android/app/build/outputs/apk/$game/release/app-$game-release-unsigned.apk"
    output="build/android-release/$name"
    "$build_tools/zipalign" -f -P 16 4 "$unsigned" "$signing_dir/aligned.apk"
    "$build_tools/apksigner" sign --ks "$signing_dir/keystore" \
        --ks-key-alias "$ANDROID_KEY_ALIAS" --ks-pass env:ANDROID_KEYSTORE_PASSWORD \
        --key-pass env:ANDROID_KEY_PASSWORD --out "$output" "$signing_dir/aligned.apk"
    "$build_tools/zipalign" -c -P 16 4 "$output"
    "$build_tools/apksigner" verify --verbose "$output"
done
