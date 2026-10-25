#!/usr/bin/env bash
set -euo pipefail

if [[ "${GITHUB_ACTIONS:-false}" != true || "${PUBLISH_POINT3_DEV:-false}" != true ]]; then
    echo 'Point-3 DEV publication must be explicitly enabled in the GitHub workflow.' >&2
    exit 1
fi
if [[ "${GITHUB_REPOSITORY:-}" != ThorinFutDeChene/Mathom-Notes \
    || "${GITHUB_REF:-}" != refs/heads/ci/5.0.4-point3-erreurs-archives ]]; then
    echo 'Point-3 DEV publication is limited to the reviewed native CI branch.' >&2
    exit 1
fi

root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
source_root="$(realpath -- "${1:?Usage: publish-point3-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
package="$(realpath -- "${2:?Usage: publish-point3-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
source_commit="${3:?Usage: publish-point3-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}"
if [[ ! "$source_commit" =~ ^[0-9a-f]{40}$ ]]; then
    echo 'The application commit must be an explicit 40-character SHA.' >&2
    exit 1
fi
cd "$source_root"
version="$(tr -d '[:space:]' < VERSION)"
if [[ ! "$version" =~ ^([0-9]+\.[0-9]+\.[0-9]+)-dev([0-9]+)$ ]]; then
    echo 'Only X.Y.Z-devN versions can be published by this workflow.' >&2
    exit 1
fi
deb_version="${BASH_REMATCH[1]}-0dev${BASH_REMATCH[2]}"
tag="v$version"
checksum="$package.sha256"
test "$(git rev-parse HEAD)" = "$source_commit"
git merge-base --is-ancestor "$source_commit" origin/fix/5.0.4-point3-erreurs-archives
# Reject accidental mixing of another change into this single-point DEV.
test "$(git rev-parse origin/main)" = "1b377a316a65848b0cf4403d8c3f451055c77fb5"
changed_paths="$(git diff --name-only v5.0.3 "$source_commit")"
expected_paths="$(printf 'VERSION\nsrc/archive.cpp\nsrc/archive.h\nsrc/tests/archivetest.cpp')"
if [[ "$changed_paths" != "$expected_paths" ]]; then
    printf 'Unexpected point-3 source files:\n%s\n' "$changed_paths" >&2
    exit 1
fi
test "$version" = "5.0.4-dev1"
# The source commit is selected before CI runs and contains only the application
# change. Never publish the infrastructure commit or rewrite a refused target.
git diff --exit-code v5.0.3 "$source_commit" -- .github/workflows
git diff --exit-code
git diff --cached --exit-code
test "$(dpkg-deb -f "$package" Package)" = mathom
test "$(dpkg-deb -f "$package" Version)" = "$deb_version"
test "$(dpkg-deb -f "$package" Architecture)" = amd64
(cd -- "$(dirname -- "$package")" && sha256sum --check -- "$(basename -- "$checksum")")

# A new DEV never replaces a previously published tag or asset. Reserve the new
# tag atomically so a competing publication fails instead of reusing a tag.
existing_tag="$(git ls-remote --tags origin "refs/tags/$tag" "refs/tags/$tag^{}")"
if [[ -n "$existing_tag" ]]; then
    echo "Tag $tag already exists. Increment devN; never replace this release." >&2
    exit 1
fi

results="$root/packaging/ci-results/publication"
mkdir -p "$results"
cat > "$results/release-notes.txt" <<EOF
Mathom Notes 5.0.4-dev1 — POINT 3 UNIQUEMENT

Correction de la remontee des erreurs de creation et d'extraction :
- l'API detecte les fichiers manquants, les destinations inaccessibles
  et les lectures ou ecritures incompletes ;
- verification stricte des longueurs des blocs binaires des archives ;
- les archives tronquees/incompletes et les erreurs d'extraction sont
  signalees et les fichiers temporaires incomplets nettoyes ;
- export utilisateur : controle des ajouts KTar, de la fermeture du tar
  et publication atomique du fichier final ;
- message d'erreur explicite en cas d'export impossible.

Les formats .mathom et .baskets restent identiques.
Les regles d'importation/renommage ne changent pas.
La DEV 5.0.2-dev1 rejetee n'est PAS reutilisee.
Les points 4 et 5 restent hors du perimetre.

Tests de regression negatifs : entree manquante, entete sans archive,
apercu ou archive tronques, chemins impossibles, sauvegarde existante
preservee lors d'un echec et destination protegee.

Base : stable 5.0.3, points 1 et 2 conserves.
Paquet Debian natif Ubuntu 26.04 amd64.
Commit applicatif : $source_commit
Infrastructure CI : $GITHUB_SHA
Resultats de validation du paquet publie :
$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID

Validation interactive restante : export/import d'une Mathom-House,
export/import de toutes les Mathom-Houses et erreur volontaire sans
modifier les donnees existantes.
La stable 5.0.3 et main restent inchangees.

EOF
# Ordinary GitHub permissions apply. If repository policy or workflow-write
# restrictions refuse either call, stop and report the error. Do not retry with
# alternate credentials, rewrite the target commit, or remove the reserved tag.
gh api --method POST "repos/$GITHUB_REPOSITORY/git/refs" \
    -f "ref=refs/tags/$tag" -f "sha=$source_commit" > "$results/tag.json"
gh release create "$tag" "$package" "$checksum" \
    --repo "$GITHUB_REPOSITORY" --verify-tag --target "$source_commit" \
    --title "Mathom Notes $version (DEV)" \
    --notes-file "$results/release-notes.txt" --prerelease --latest=false \
    | tee "$results/release-url.txt"
