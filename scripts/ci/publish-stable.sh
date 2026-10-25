#!/usr/bin/env bash
set -euo pipefail

if [[ "${GITHUB_ACTIONS:-false}" != true || "${PUBLISH_STABLE:-false}" != true ]]; then
    echo 'Stable publication must be explicitly enabled in the GitHub workflow.' >&2
    exit 1
fi
if [[ "${GITHUB_REPOSITORY:-}" != ThorinFutDeChene/Mathom-Notes \
    || "${GITHUB_REF:-}" != refs/heads/ci/5.0.3-stable-point2-sauvegardes ]]; then
    echo 'Stable publication is limited to the reviewed native CI branch.' >&2
    exit 1
fi

root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
source_root="$(realpath -- "${1:?Usage: publish-stable.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
package="$(realpath -- "${2:?Usage: publish-stable.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
source_commit="${3:?Usage: publish-stable.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}"
if [[ ! "$source_commit" =~ ^[0-9a-f]{40}$ ]]; then
    echo 'The application commit must be an explicit 40-character SHA.' >&2
    exit 1
fi
cd "$source_root"
version="$(tr -d '[:space:]' < VERSION)"
if [[ "$version" != 5.0.3 ]]; then
    echo 'Only validated stable 5.0.3 can be published by this workflow.' >&2
    exit 1
fi
deb_version="5.0.3-1"
tag="v$version"
checksum="$package.sha256"
test "$(git rev-parse HEAD)" = "$source_commit"
# Tag exactly the commit already promoted into main.
test "$(git rev-parse origin/main)" = "$source_commit"
# Only the approved point-2 backup fix, regression tests and VERSION may differ from v5.0.2.
actual_paths="$(git diff --name-only v5.0.2 "$source_commit")"
expected_paths="$(printf 'VERSION\nsrc/backup.cpp\nsrc/backup.h\nsrc/tests/CMakeLists.txt\nsrc/tests/backuptest.cpp')"
if [[ "$actual_paths" != "$expected_paths" ]]; then
    printf 'Unexpected stable source files:\n%s\n' "$actual_paths" >&2
    exit 1
fi
git diff --exit-code v5.0.2 "$source_commit" -- .github/workflows
git diff --exit-code
git diff --cached --exit-code
test "$(dpkg-deb -f "$package" Package)" = mathom
test "$(dpkg-deb -f "$package" Version)" = "$deb_version"
test "$(dpkg-deb -f "$package" Architecture)" = amd64
(cd -- "$(dirname -- "$package")" && sha256sum --check -- "$(basename -- "$checksum")")

# A stable release never replaces a previously published tag or asset. Reserve the
# tag atomically so a competing publication fails instead of reusing a tag.
existing_tag="$(git ls-remote --tags origin "refs/tags/$tag" "refs/tags/$tag^{}")"
if [[ -n "$existing_tag" ]]; then
    echo "Tag $tag already exists. Never replace the published stable." >&2
    exit 1
fi

results="$root/packaging/ci-results/publication"
mkdir -p "$results"
cat > "$results/release-notes.txt" <<EOF
Mathom Notes 5.0.3 — VERSION STABLE

Cette version integre exclusivement le POINT 2 valide :
la protection des sauvegardes precedentes lorsqu'une nouvelle
sauvegarde echoue.

La sauvegarde complete est desormais creee et verifiee dans un
emplacement temporaire avant le remplacement atomique via QSaveFile.
Une erreur de creation, d'ecriture ou de validation ne supprime plus
la sauvegarde precedente. Le dialogue de sauvegarde ne supprime plus
la destination en cas d'echec. Messages d'erreur clarifies.
La meme creation securisee est utilisee par les sauvegardes automatiques.

Tests ajoutes : echec de snapshot, echec reel d'ecriture d'archive,
echec de promotion, destination inaccessible, remplacement valide
avec restauration des donnees.

La base est Mathom Notes 5.0.2 stable : le point 1 reste inclus.
Les points 3, 4 et 5 de l'audit sont hors perimetre.
La DEV 5.0.2-dev1 invalidee ne fait pas partie de cette version.

Validation DEV 5.0.3-dev1 : compilation, publication, telechargement,
installation et 12/12 suites de tests reussies en source et sur le
paquet publie. Validation manuelle utilisateur : creation, remplacement,
echec attendu et conservation integrale de la sauvegarde precedente
(empreinte SHA-256 OK).

Paquet Debian natif Ubuntu 26.04 amd64.
Commit stable : $source_commit
Infrastructure CI : $GITHUB_SHA
Resultats de construction et validation :
$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID

EOF# Ordinary GitHub permissions apply. If repository policy or workflow-write
# restrictions refuse either call, stop and report the error. Do not retry with
# alternate credentials, rewrite the target commit, or remove the reserved tag.
gh api --method POST "repos/$GITHUB_REPOSITORY/git/refs" \
    -f "ref=refs/tags/$tag" -f "sha=$source_commit" > "$results/tag.json"
gh release create "$tag" "$package" "$checksum" \
    --repo "$GITHUB_REPOSITORY" --verify-tag --target "$source_commit" \
    --title "Mathom Notes $version (stable)" \
    --notes-file "$results/release-notes.txt" --latest \
    | tee "$results/release-url.txt"
