#!/usr/bin/env bash
set -euo pipefail

if [[ "${GITHUB_ACTIONS:-false}" != true || "${PUBLISH_POINT4_DEV:-false}" != true ]]; then
    echo 'Point-4 DEV publication must be explicitly enabled in the GitHub workflow.' >&2
    exit 1
fi
if [[ "${GITHUB_REPOSITORY:-}" != ThorinFutDeChene/Mathom-Notes \
    || "${GITHUB_REF:-}" != refs/heads/ci/5.0.5-point4-renommage-pieces-jointes ]]; then
    echo 'Point-4 DEV publication is limited to the reviewed CI branch.' >&2
    exit 1
fi

root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
source_root="$(realpath -- "${1:?Usage: publish-point4-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
package="$(realpath -- "${2:?Usage: publish-point4-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
source_commit="${3:?Usage: publish-point4-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}"
if [[ ! "$source_commit" =~ ^[0-9a-f]{40}$ ]]; then
    echo 'The application commit must be an explicit 40-character SHA.' >&2
    exit 1
fi
cd "$source_root"
version="$(tr -d '[:space:]' < VERSION)"
if [[ "$version" != 5.0.5-dev1 ]]; then
    echo 'Only the reviewed point-4 5.0.5-dev1 candidate can be published.' >&2
    exit 1
fi
deb_version="5.0.5-0dev1"
tag="v$version"
checksum="$package.sha256"
test "$(git rev-parse HEAD)" = "$source_commit"
# Tag exactly the reviewed point-4 commit; do not modify main.
git merge-base --is-ancestor "$source_commit" origin/fix/5.0.5-point4-renommage-pieces-jointes
test "$(git rev-parse origin/main)" = "343b22d65f3a28146a339e06acb38e12c8420648"
# Only the reviewed point-4 attachment rename implementation, translations,
# regression tests and VERSION may differ from stable v5.0.4.
actual_paths="$(git diff --name-only v5.0.4 "$source_commit")"
expected_paths="$(printf 'VERSION\npo/fr/basket.po\nsrc/notecontent.cpp\nsrc/notecontent.h\nsrc/noteedit.cpp\nsrc/tests/CMakeLists.txt\nsrc/tests/notecontentrenametest.cpp')"
if [[ "$actual_paths" != "$expected_paths" ]]; then
    printf 'Unexpected point-4 DEV files:\n%s\n' "$actual_paths" >&2
    exit 1
fi
git diff --exit-code v5.0.4 "$source_commit" -- .github/workflows
git diff --exit-code
git diff --cached --exit-code
test "$(dpkg-deb -f "$package" Package)" = mathom
test "$(dpkg-deb -f "$package" Version)" = "$deb_version"
test "$(dpkg-deb -f "$package" Architecture)" = amd64
(cd -- "$(dirname -- "$package")" && sha256sum --check -- "$(basename -- "$checksum")")

# A DEV release never replaces a previously published tag or asset. Reserve the
# tag atomically so a competing publication fails instead of reusing a tag.
existing_tag="$(git ls-remote --tags origin "refs/tags/$tag" "refs/tags/$tag^{}")"
if [[ -n "$existing_tag" ]]; then
    echo "Tag $tag already exists. Never replace a published DEV." >&2
    exit 1
fi

results="$root/packaging/ci-results/publication"
mkdir -p "$results"
cat > "$results/release-notes.txt" <<EOF
Mathom Notes 5.0.5-dev1 — POINT 4 UNIQUEMENT

Cette version de developpement corrige le renommage manuel des
pieces jointes en preservant leurs fichiers et references.

- l'echec d'un renommage sur disque n'est plus annonce comme un succes ;
- les collisions creent un nom alternatif (-2, -3...) sans ecraser ;
- l'editeur affiche et persiste le nom REEL retenu ;
- les chemins surveilles sont mis a jour uniquement apres le succes ;
- les dossiers joints et fichiers binaires sont conserves ;
- noms dangereux ou invalides et erreurs d'entree/sortie signales ;
- huit methodes de regression couvrent ces conditions.

Base : stable 5.0.4. Les points 1, 2 et 3 demeurent inchanges.
Aucun changement des reprises de sauvegarde (point 5).
Aucun changement du format des archives .mathom/.baskets.
Aucun code de la DEV multi-correctifs 5.0.2-dev1 rejetee n'est fusionne.

Les deplacements inter-etageres et leurs transferts KIO asynchrones
restent a examiner dans le cycle point 4 avant stabilisation.
Ceci est une DEV de validation, PAS une version stable.

Recette interactive requise apres publication :
- renommer une piece jointe dans une Mathom-House de test ;
- renommer vers un nom deja pris, verifier le suffixe et les deux contenus ;
- provoquer un echec, verifier references et fichiers toujours valides ;
- fermer et rouvrir le profil de test et ouvrir les pieces jointes.

Paquet Debian natif Ubuntu 26.04 amd64.
Commit applicatif : $source_commit
Infrastructure CI : $GITHUB_SHA
Validation du paquet publie :
$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID

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
