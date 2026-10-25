#!/usr/bin/env bash
set -euo pipefail

if [[ "${GITHUB_ACTIONS:-false}" != true || "${PUBLISH_POINT2_DEV:-false}" != true ]]; then
    echo 'Point-2 DEV publication must be explicitly enabled in the GitHub workflow.' >&2
    exit 1
fi
if [[ "${GITHUB_REPOSITORY:-}" != ThorinFutDeChene/Mathom-Notes \
    || "${GITHUB_REF:-}" != refs/heads/ci/5.0.3-point2-sauvegardes ]]; then
    echo 'Point-2 DEV publication is limited to the reviewed native CI branch.' >&2
    exit 1
fi

root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
source_root="$(realpath -- "${1:?Usage: publish-point2-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
package="$(realpath -- "${2:?Usage: publish-point2-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
source_commit="${3:?Usage: publish-point2-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}"
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
git merge-base --is-ancestor "$source_commit" origin/fix/5.0.3-point2-sauvegardes
# Reject accidental mixing of another change into this single-point DEV.
test "$(git rev-parse origin/main)" = "7f1ccbecde88171004be5e67953ed18f7388faef"
changed_paths="$(git diff --name-only v5.0.2 "$source_commit")"
expected_paths="$(printf 'VERSION\nsrc/backup.cpp\nsrc/tests/CMakeLists.txt\nsrc/tests/backuptest.cpp')"
if [[ "$changed_paths" != "$expected_paths" ]]; then
    printf 'Unexpected point-2 source files:\n%s\n' "$changed_paths" >&2
    exit 1
fi
test "$version" = "5.0.3-dev1"
# The source commit is selected before CI runs and contains only the application
# change. Never publish the infrastructure commit or rewrite a refused target.
git diff --exit-code v5.0.2 "$source_commit" -- .github/workflows
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
Mathom Notes 5.0.3-dev1 — POINT 2 UNIQUEMENT

Correction de la creation des sauvegardes completes :
- ancienne sauvegarde preservee pendant la creation et la verification de
  la nouvelle ;
- archive creee et controlee dans un dossier temporaire isole ;
- publication atomique via QSaveFile seulement apres validation ;
- aucune suppression du fichier precedent en cas d'echec dans le dialogue.

Cinq tests de regression couvrent l'ancien fichier en cas d'echec,
une vraie erreur d'ecriture de tar, un dossier non modifiable, une
destination inaccessible et le remplacement valide avec restauration.

Base : Mathom Notes 5.0.2 stable, avec son point 1 deja valide.
Aucun changement aux fonctions d'export/import des Mathom-Houses,
ni aux points 3, 4 ou 5 de l'audit.
La DEV 5.0.2-dev1 invalidee n'est pas incluse.

Paquet Debian natif pour Ubuntu 26.04 amd64.
Commit applicatif : $source_commit
Infrastructure CI : $GITHUB_SHA
Resultats des tests du paquet retelecharge :
$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID

La recette manuelle sur sauvegarde de TEST uniquement reste a faire.
La stable 5.0.2 et main ne changent pas.

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
