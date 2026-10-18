#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
build="$(realpath -- "${1:?Usage: test-native.sh BUILD_DIR source|published [SOURCE_DIR]}")"
phase="${2:?Usage: test-native.sh BUILD_DIR source|published [SOURCE_DIR]}"
source_root="$(realpath -- "${3:-$root}")"
[[ "$phase" == source || "$phase" == published ]] || exit 2
results="$root/packaging/ci-results/$phase"
mkdir -p "$results"

# Each invocation has a fresh desktop profile and a separate D-Bus/X server.
test_profile="$(mktemp -d -t mathom-ci-profile.XXXXXX)"
trap 'rm -rf -- "$test_profile"' EXIT
export XDG_CONFIG_HOME="$test_profile/config"
export XDG_DATA_HOME="$test_profile/data"
export XDG_CACHE_HOME="$test_profile/cache"
export XDG_STATE_HOME="$test_profile/state"
export XDG_RUNTIME_DIR="$test_profile/runtime"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME" "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
export QT_QPA_PLATFORM=xcb
export QT_LOGGING_RULES='qt.multimedia.*=false'

if [[ "$phase" == published ]]; then
    test -x /usr/bin/mathom
    mapfile -t installed_libraries < <(dpkg-query -L mathom | sed -n '\@/libLibBasket\.so\.[^/]*$@p')
    [[ ${#installed_libraries[@]} -gt 0 ]] || { echo 'Packaged LibBasket not found.' >&2; exit 1; }
    installed_library="$(realpath -- "${installed_libraries[0]}")"
    export LD_LIBRARY_PATH="$(dirname -- "$installed_library")"

    # The regression executables were built before publication, but they must now
    # load the library installed from the freshly downloaded release asset.
    : > "$results/loaded-libraries.txt"
    for test_binary in "$build"/src/tests/*test; do
        [[ -x "$test_binary" && -f "$test_binary" ]] || continue
        dependencies="$(ldd "$test_binary")"
        printf '%s\n%s\n' "$test_binary" "$dependencies" >> "$results/loaded-libraries.txt"
        loaded_library="$(printf '%s\n' "$dependencies" | awk '$1 ~ /^libLibBasket\.so/ {print $3}')"
        if [[ -n "$loaded_library" && "$(realpath -- "$loaded_library")" != "$installed_library" ]]; then
            echo "Test still loads a library outside the installed package: $test_binary" >&2
            exit 1
        fi
    done
fi

timeout --signal=TERM --kill-after=10s 20m \
    xvfb-run --auto-servernum dbus-run-session -- \
    ctest --test-dir "$build" --output-on-failure --no-tests=error \
    --timeout 120 --parallel 1 --output-junit "$results/ctest.xml" \
    2>&1 | tee "$results/ctest.log"

if [[ "$phase" == published ]]; then
    version="$(tr -d '[:space:]' < "$source_root/VERSION")"
    timeout --signal=TERM --kill-after=5s 30s \
        xvfb-run --auto-servernum dbus-run-session -- /usr/bin/mathom --version \
        > "$results/version.log" 2>&1
    cat "$results/version.log"
    grep -F -- "$version" "$results/version.log"

    # This is a bounded startup check, not a substitute for the interactive recipe.
    # A healthy GUI stays alive until timeout; a crash or early exit is a failure.
    if timeout --signal=TERM --kill-after=5s 20s \
        xvfb-run --auto-servernum dbus-run-session -- \
        /usr/bin/mathom --data-folder "$test_profile/application" \
        > "$results/startup.log" 2>&1; then
        echo 'The published application exited before the startup observation ended.' >&2
        exit 1
    else
        status=$?
        if [[ "$status" != 124 ]]; then
            cat "$results/startup.log" >&2
            exit "$status"
        fi
    fi
    test -s "$test_profile/application/baskets/baskets.xml"
    printf 'Published package: version, installed-library regressions and bounded startup passed.\n' \
        | tee "$results/result.txt"
fi
