#!/usr/bin/env bash
set -euo pipefail

if [[ "${GITHUB_ACTIONS:-false}" != true || "${PUBLISH_STABLE:-false}" != true ]]; then
    echo 'Stable publication must be explicitly enabled in the GitHub workflow.' >&2
    exit 1
fi
if [[ "${GITHUB_REPOSITORY:-}" != ThorinFutDeChene/Mathom-Notes \
    || "${GITHUB_REF:-}" != refs/heads/ci/5.0.4-stable-point3-erreurs-archives ]]; then
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
if [[ "$version" != 5.0.4 ]]; then
    echo 'Only validated stable 5.0.4 can be published by this workflow.' >&2
    exit 1
fi
deb_version="5.0.4-1"
tag="v$version"
checksum="$package.sha256"
test "$(git rev-parse HEAD)" = "$source_commit"
# Tag exactly the commit already promoted into main.
test "$(git rev-parse origin/main)" = "$source_commit"
# Only point-3 archive error fixes, translations, tests and VERSION may differ from v5.0.3.
actual_paths="$(git diff --name-only v5.0.3 "$source_commit")"
expected_paths="$(printf 'VERSION\npo/fr/basket.po\nsrc/archive.cpp\nsrc/archive.h\nsrc/bnpview.cpp\nsrc/tests/archivetest.cpp')"
if [[ "$actual_paths" != "$expected_paths" ]]; then
    printf 'Unexpected stable source files:\n%s\n' "$actual_paths" >&2
    exit 1
fi
git diff --exit-code v5.0.3 "$source_commit" -- .github/workflows
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
Mathom Notes 5.0.4 — VERSION STABLE

Cette version integre exclusivement le POINT 3 valide :
signalement fiable des echecs d'exportation et d'importation des
archives Mathom-House.

- controles stricts des ouvertures, lectures et ecritures de fichiers ;
- detection des blocs d'archives incomplets ou tronques ;
- detection des echecs d'extraction et nettoyage des fichiers temporaires ;
- controle des ajouts KTar, de la fermeture et de l'ecriture finale ;
- publication atomique pour conserver l'archive precedente en cas d'echec ;
- messages d'erreur traduits en francais ;
- evenement EXPORT_MATHOM_OK uniquement en cas de succes,
  EXPORT_MATHOM_FAILED sinon ;
- tests de regression des erreurs, des fichiers inexistants et des
  destinations protegees, en plus des tests d'integrite precedents.

Les formats .mathom et .baskets restent compatibles.
Aucun changement des regles de renommage des pieces jointes (point 4),
ni des reprises temporisees de sauvegarde (point 5).
Les corrections des points 1 et 2 restent presentes via la stable 5.0.3.
La DEV 5.0.2-dev1 invalidee n'est PAS incluse.

Validation DEV 5.0.4-dev3 :
compilation, publication, retelechargement, controle SHA-256,
installation et 12/12 suites de tests reussies sur les sources
et sur le paquet DEV publie.
Validation interactive sur l'ordinateur utilisateur :
export normal et signature correcte, erreur volontaire d'export
correctement detectee, archive preexistante conservee SHA-256 OK,
import valide dans un profil de test isole et refus d'importation
d'une archive corrompue sans modification des donnees.

Paquet Debian natif Ubuntu 26.04 amd64.
Commit stable : $source_commit
Infrastructure CI : $GITHUB_SHA
Resultats de construction et validation du paquet stable :
$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID

EOF
# Ordinary GitHub permissions apply. If repository policy or workflow-write
# restrictions refuse either call, stop and report the error. Do not retry with
# alternate credentials, rewrite the target commit, or remove the reserved tag.
gh api --method POST "repos/$GITHUB_REPOSITORY/git/refs" \
    -f "ref=refs/tags/$tag" -f "sha=$source_commit" > "$results/tag.json"
gh release create "$tag" "$package" "$checksum" \
    --repo "$GITHUB_REPOSITORY" --verify-tag --target "$source_commit" \
    --title "Mathom Notes $version (stable)" \
    --notes-file "$results/release-notes.txt" --latest \
    | tee "$results/release-url.txt"
