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
export TMPDIR="$test_profile/tmp"
mkdir -p "$XDG_CONFIG_HOME" "$XDG_DATA_HOME" "$XDG_CACHE_HOME" "$XDG_STATE_HOME" "$XDG_RUNTIME_DIR" "$TMPDIR"
chmod 700 "$XDG_RUNTIME_DIR"
export QT_QPA_PLATFORM=xcb
export QT_LOGGING_RULES='qt.multimedia.*=false'

# Permission-failure tests need an ordinary UID: root would bypass read-only
# destination directories and skip that regression. setpriv preserves the
# existing environment, including HOME, while changing the real/effective IDs.
test_command_prefix=()
if [[ "$(id -u)" == 0 ]]; then
    if ! command -v setpriv >/dev/null 2>&1; then
        echo 'Root CI requires setpriv from util-linux to run unprivileged tests.' >&2
        exit 1
    fi
    test_uid="$(id -u nobody)"
    test_gid="$(id -g nobody)"
    [[ "$test_uid" != 0 ]] || { echo 'The test account must have a non-root UID.' >&2; exit 1; }
    test_command_prefix=(setpriv --reuid "$test_uid" --regid "$test_gid" --clear-groups --no-new-privs)

    # Only disposable output directories become writable by the test user.
    # Sources, binaries, build metadata and their parent directories keep their
    # existing ownership and permissions.
    mkdir -p "$build/Testing/Temporary"
    chown -R -- "$test_uid:$test_gid" "$test_profile" "$results" "$build/Testing"
fi

# Refuse an inaccessible checkout rather than broadening permissions on it.
for required_file in "$build/CTestTestfile.cmake" "$source_root/CMakeLists.txt"; do
    if ! "${test_command_prefix[@]}" test -r "$required_file"; then
        echo "The test UID cannot read $required_file; use a traversable CI checkout." >&2
        exit 1
    fi
done
"${test_command_prefix[@]}" id | tee "$results/test-identity.txt"

if [[ "$phase" == published ]]; then
    test -x /usr/bin/mathom
    mapfile -t installed_libraries < <(dpkg-query -L mathom | sed -n '\@/libLibBasket\.so\.[^/]*$@p')
    [[ ${#installed_libraries[@]} -gt 0 ]] || { echo 'Packaged LibBasket not found.' >&2; exit 1; }
    installed_library="$(realpath -- "${installed_libraries[0]}")"
    export LD_LIBRARY_PATH="$(dirname -- "$installed_library")"

    # The regression executables were built before publication, but they must now
    # load the library installed from the freshly downloaded release asset.
    library_log="$results/loaded-libraries.txt"
    printf 'Expected packaged LibBasket: %s\n' "$installed_library" | tee "$library_log"
    inspected_test_count=0
    linked_test_count=0
    declare -A linked_tests=()
    for test_binary in "$build"/bin/*test; do
        [[ -x "$test_binary" && -f "$test_binary" ]] || continue
        dependencies="$(ldd "$test_binary")"
        ((inspected_test_count += 1))
        printf '\n%s\n%s\n' "$test_binary" "$dependencies" >> "$library_log"
        loaded_library="$(printf '%s\n' "$dependencies" | awk '$1 ~ /^libLibBasket\.so/ {print $3}')"
        if [[ -n "$loaded_library" ]]; then
            if [[ "$(realpath -- "$loaded_library")" != "$installed_library" ]]; then
                echo "Test still loads a library outside the installed package: $test_binary" \
                    | tee -a "$library_log" >&2
                exit 1
            fi
            ((linked_test_count += 1))
            linked_tests["${test_binary##*/}"]=1
            printf 'Verified packaged LibBasket: %s\n' "${test_binary##*/}" | tee -a "$library_log"
        else
            printf 'Inspected without a LibBasket dependency: %s\n' "${test_binary##*/}" | tee -a "$library_log"
        fi
    done
    printf 'Inspected %d test executables; %d load the packaged LibBasket.\n' \
        "$inspected_test_count" "$linked_test_count" | tee -a "$library_log"
    if [[ "$inspected_test_count" == 0 || "$linked_test_count" == 0 ]]; then
        echo "No test executable loading the packaged LibBasket was verified in $build/bin." \
            | tee -a "$library_log" >&2
        exit 1
    fi
    # Point-1/3 archive and point-2 backup regression suites must load the
    # published packaged library; rejected multi-fix DEV changes are excluded.
    for priority_test in archivetest backuptest; do
        if [[ "${linked_tests[$priority_test]:-0}" != 1 ]]; then
            echo "Priority suite was not verified against the packaged LibBasket: $priority_test" \
                | tee -a "$library_log" >&2
            exit 1
        fi
    done
    printf 'Point-1/3 archive and point-2 backup suites load the packaged LibBasket.\n' | tee -a "$library_log"
fi

timeout --signal=TERM --kill-after=10s 20m \
    "${test_command_prefix[@]}" xvfb-run --auto-servernum dbus-run-session -- \
    ctest --test-dir "$build" --output-on-failure --no-tests=error \
    --timeout 120 --parallel 1 --output-junit "$results/ctest.xml" \
    2>&1 | tee "$results/ctest.log"

if [[ "$phase" == published ]]; then
    version="$(tr -d '[:space:]' < "$source_root/VERSION")"
    timeout --signal=TERM --kill-after=5s 30s \
        "${test_command_prefix[@]}" xvfb-run --auto-servernum dbus-run-session -- /usr/bin/mathom --version \
        > "$results/version.log" 2>&1
    cat "$results/version.log"
    grep -F -- "$version" "$results/version.log"

    # This is a bounded startup check, not a substitute for the interactive recipe.
    # A healthy GUI stays alive until timeout; a crash or early exit is a failure.
    if timeout --signal=TERM --kill-after=5s 20s \
        "${test_command_prefix[@]}" xvfb-run --auto-servernum dbus-run-session -- \
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
