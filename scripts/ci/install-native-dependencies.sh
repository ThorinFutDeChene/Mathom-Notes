#!/usr/bin/env bash
set -euo pipefail

# Run inside the disposable ubuntu:26.04 CI container.
. /etc/os-release
if [[ "$ID" != ubuntu || "$VERSION_ID" != 26.04 ]]; then
    echo 'Native CI dependencies require Ubuntu 26.04.' >&2
    exit 1
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update -o Acquire::Retries=3
# Ubuntu 26.04's build-essential selects the uutils provider. Install the
# compiler toolchain explicitly so the official packager keeps GNU coreutils.
apt-get install -y --no-install-recommends \
    gcc g++ make libc6-dev dpkg-dev cmake ninja-build pkg-config extra-cmake-modules \
    git ca-certificates curl gh jq file gettext coreutils-from-gnu \
    qt6-base-dev qt6-multimedia-dev qt6-5compat-dev qt6-tools-dev \
    libpoppler-qt6-dev libsane-dev libgit2-dev \
    libkf6archive-dev libkf6codecs-dev libkf6colorscheme-dev \
    libkf6completion-dev libkf6config-dev libkf6configwidgets-dev \
    libkf6coreaddons-dev libkf6crash-dev libkf6dbusaddons-dev \
    libkf6filemetadata-dev libkf6globalaccel-dev libkf6guiaddons-dev \
    libkf6i18n-dev libkf6iconthemes-dev libkf6jobwidgets-dev \
    libkf6kcmutils-dev libkf6kio-dev libkf6parts-dev libkf6service-dev \
    libkf6textwidgets-dev libkf6widgetsaddons-dev libkf6xmlgui-dev \
    xvfb xauth dbus-x11 libxcb-cursor0 fonts-dejavu-core
