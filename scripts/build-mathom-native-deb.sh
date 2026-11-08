#!/bin/sh
set -eu

ARCH="$(dpkg --print-architecture)"
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"

if printf '%s\n' "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+-dev[0-9]+$'; then
    BASE_VERSION="${VERSION%%-dev*}"
    DEV_NUMBER="${VERSION##*-dev}"
    PACKAGE_VERSION="${BASE_VERSION}-0dev${DEV_NUMBER}"
elif printf '%s\n' "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    PACKAGE_VERSION="${VERSION}-1"
else
    echo "Erreur : version Mathom invalide : $VERSION"
    exit 1
fi

PACKAGING="$ROOT/packaging"
BUILD="$ROOT/build-native"
DEBROOT="$PACKAGING/mathom-native-debroot"
APPROOT="$DEBROOT/opt/mathom"
OUTPUT="$PACKAGING/mathom_${PACKAGE_VERSION}_${ARCH}.deb"

echo "=== Mathom Notes $VERSION ==="
echo "=== Construction native conformite /opt/mathom ==="

rm -rf "$BUILD" "$DEBROOT"

echo
echo "=== 1/6 Configuration CMake native ==="

cmake \
    -S "$ROOT" \
    -B "$BUILD" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/opt/mathom \
    -DBUILD_TESTING=OFF \
    -DBUILD_DEVTOOLS=OFF \
    -DENABLE_GPG=OFF \
    -DENABLE_GIT=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_KF6DocTools=TRUE

echo
echo "=== 2/6 Compilation ==="

cmake --build "$BUILD"

echo
echo "=== 3/6 Installation dans l'arborescence Debian ==="

DESTDIR="$DEBROOT" cmake --install "$BUILD"

if [ ! -x "$APPROOT/bin/mathom" ]; then
    echo "Erreur : /opt/mathom/bin/mathom introuvable." >&2
    exit 1
fi

# All application executables, internal libraries and resources stay in /opt.
# The Debian launcher and desktop/host integration keep their Linux paths.
install -Dm755 \
    "$ROOT/packaging/debian/mathom-wrapper" \
    "$DEBROOT/usr/bin/mathom"

DESKTOP="$APPROOT/share/applications/fr.thorinux.mathom.desktop"
if [ ! -f "$DESKTOP" ]; then
    echo "Erreur : fichier desktop introuvable." >&2
    exit 1
fi
install -Dm644 \
    "$DESKTOP" \
    "$DEBROOT/usr/share/applications/fr.thorinux.mathom.desktop"
rm -f "$DESKTOP"

METAINFO="$APPROOT/share/metainfo/fr.thorinux.mathom.metainfo.xml"
if [ -f "$METAINFO" ]; then
    install -Dm644 \
        "$METAINFO" \
        "$DEBROOT/usr/share/metainfo/fr.thorinux.mathom.metainfo.xml"
    rm -f "$METAINFO"
fi

MIME="$APPROOT/share/mime/packages/basket.xml"
if [ -f "$MIME" ]; then
    install -Dm644 \
        "$MIME" \
        "$DEBROOT/usr/share/mime/packages/basket.xml"
    rm -f "$MIME"
fi

if [ -d "$APPROOT/share/icons" ]; then
    mkdir -p "$DEBROOT/usr/share/icons"
    cp -a "$APPROOT/share/icons/." "$DEBROOT/usr/share/icons/"
    rm -rf "$APPROOT/share/icons"
fi

# KDE's thumbnailer runs outside Mathom and therefore requires a host plugin.
if [ -d "$APPROOT/lib" ]; then
    find "$APPROOT/lib" -type f \
        -path '*/kf6/thumbcreator/*' -name '*.so' |
    while IFS= read -r plugin; do
        install -Dm755 \
            "$plugin" \
            "$DEBROOT/usr/lib/$MULTIARCH/qt6/plugins/kf6/thumbcreator/$(basename "$plugin")"
        rm -f "$plugin"
    done
fi

# Exact official application logo (unchanged).
install -Dm644 \
    "$ROOT/resources/icons/app/mathom.png" \
    "$DEBROOT/usr/share/icons/hicolor/48x48/apps/fr.thorinux.mathom.png"

install -Dm644 \
    "$ROOT/resources/icons/app/mathom.png" \
    "$DEBROOT/usr/share/pixmaps/fr.thorinux.mathom.png"

sed -i \
    's|^Icon=.*$|Icon=/usr/share/pixmaps/fr.thorinux.mathom.png|' \
    "$DEBROOT/usr/share/applications/fr.thorinux.mathom.desktop"

mkdir -p "$DEBROOT/DEBIAN"

install -Dm755 \
    "$ROOT/packaging/debian/postinst" \
    "$DEBROOT/DEBIAN/postinst"

install -Dm755 \
    "$ROOT/packaging/debian/postrm" \
    "$DEBROOT/DEBIAN/postrm"

echo
echo "=== 4/6 Calcul automatique des dépendances ==="

ELF_LIST="$PACKAGING/mathom-native-elf-list.txt"
: > "$ELF_LIST"

find "$APPROOT/bin" "$APPROOT/lib" "$DEBROOT/usr/lib" -type f 2>/dev/null |
while IFS= read -r binary; do
    if file -b "$binary" | grep -q '^ELF '; then
        printf '%s\n' "$binary"
    fi
done > "$ELF_LIST"

set --

while IFS= read -r binary; do
    set -- "$@" "$binary"
done < "$ELF_LIST"

if [ "$#" -eq 0 ]; then
    echo "Erreur : aucun binaire ELF trouvé."
    exit 1
fi

LIBBASKET="$(
    find "$APPROOT/lib" \
        -type f \
        -name 'libLibBasket.so.*' \
        | head -n 1
)"

if [ -z "$LIBBASKET" ]; then
    echo "Erreur : LibBasket native introuvable."
    exit 1
fi

PRIVATE_LIBDIR="$(dirname "$LIBBASKET")"

# dpkg-shlibdeps exige une arborescence Debian minimale pour
# déterminer les dépendances des bibliothèques liées.
SHLIBSROOT="$PACKAGING/mathom-native-shlibdeps"
SHLIBSLOG="$PACKAGING/dpkg-shlibdeps-native.log"

rm -rf "$SHLIBSROOT"
mkdir -p "$SHLIBSROOT/debian"

cat > "$SHLIBSROOT/debian/control" <<'CONTROL'
Source: mathom
Section: office
Priority: optional
Maintainer: Thorinux Systems
Standards-Version: 4.7.0

Package: mathom
Architecture: any
Depends: ${shlibs:Depends}
Description: Mathom Notes
 Mathom Notes dependency calculation helper.
CONTROL

if ! SHLIBS_OUTPUT="$(
    cd "$SHLIBSROOT"
    dpkg-shlibdeps \
        --ignore-missing-info \
        -O \
        -l"$PRIVATE_LIBDIR" \
        "$@" \
        2>"$SHLIBSLOG"
)"; then
    echo "Erreur : dpkg-shlibdeps a échoué :"
    cat "$SHLIBSLOG"
    exit 1
fi

SHLIBS_DEPENDS="$(
    printf '%s\n' "$SHLIBS_OUTPUT" |
    sed -n 's/^shlibs:Depends=//p'
)"

if [ -z "$SHLIBS_DEPENDS" ]; then
    echo "Erreur : aucune dépendance Qt/KF6 calculée."
    echo
    cat "$SHLIBSLOG"
    exit 1
fi

EXTRA_DEPENDS="kio6, kf6-breeze-icon-theme, fonts-opendyslexic, xdg-utils, apport, systemd"
DEPENDS="$SHLIBS_DEPENDS, $EXTRA_DEPENDS"

cat > "$DEBROOT/DEBIAN/control" <<CONTROL
Package: mathom
Version: $PACKAGE_VERSION
Section: office
Priority: optional
Architecture: $ARCH
Maintainer: Thorinux Systems
Depends: $DEPENDS
Description: Mathom Notes - notes and information organizer
 Mathom Notes is an application for recording ideas as mathoms and
 organizing them into Mathom-Houses and shelves.
 .
 Mathom is developed by Thorinux Systems and is based on
 BasKet Note Pads.
CONTROL

echo "=== 5/6 Construction du paquet ==="

rm -f "$OUTPUT"

dpkg-deb \
    --build \
    --root-owner-group \
    "$DEBROOT" \
    "$OUTPUT"

echo
echo "=== 6/6 Contrôles ==="

test "$(dpkg-deb -f "$OUTPUT" Package)" = "mathom"
test "$(dpkg-deb -f "$OUTPUT" Version)" = "$PACKAGE_VERSION"
test "$(dpkg-deb -f "$OUTPUT" Architecture)" = "$ARCH"

CONTENTS_LIST="$PACKAGING/mathom-native-deb-contents.txt"
dpkg-deb -c "$OUTPUT" > "$CONTENTS_LIST"

grep -q './opt/mathom/bin/mathom' "$CONTENTS_LIST"
grep -q './usr/bin/mathom' "$CONTENTS_LIST"
grep -q './usr/share/applications/fr.thorinux.mathom.desktop' "$CONTENTS_LIST"
grep -q './usr/share/pixmaps/fr.thorinux.mathom.png' "$CONTENTS_LIST"

if find "$DEBROOT/usr/lib" -type f -name 'libLibBasket.so*' \
    -print | grep -q .; then
    echo "Erreur : LibBasket doit etre installee sous /opt/mathom." >&2
    exit 1
fi

# Every shipped translation catalog must be installed in the native package.
# Check the staging tree: it is the exact filesystem tree passed to dpkg-deb.
LOCALE_ROOT="$APPROOT/share/locale"
for catalog in "$ROOT"/po/*/basket.po; do
    [ -f "$catalog" ] || continue
    lang="$(basename "$(dirname "$catalog")")"
    mo="$LOCALE_ROOT/$lang/LC_MESSAGES/basket.mo"
    if [ ! -s "$mo" ]; then
        echo "Erreur : traduction compilee manquante : $mo"
        exit 1
    fi
done

echo
echo "=============================================="
echo "Paquet natif construit avec succès :"
echo "$OUTPUT"
echo
du -h "$OUTPUT"
echo
echo "Dépendances :"
dpkg-deb -f "$OUTPUT" Depends
echo "=============================================="
