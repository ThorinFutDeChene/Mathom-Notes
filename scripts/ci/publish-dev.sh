#!/usr/bin/env bash
set -euo pipefail

if [[ "${GITHUB_ACTIONS:-false}" != true || "${PUBLISH_DEV:-false}" != true ]]; then
    echo 'DEV publication must be explicitly enabled in the GitHub workflow.' >&2
    exit 1
fi
if [[ "${GITHUB_REPOSITORY:-}" != ThorinFutDeChene/Mathom-Notes \
    || "${GITHUB_REF:-}" != refs/heads/ci/5.0.2-priorites-fichiers ]]; then
    echo 'DEV publication is limited to the reviewed native CI branch.' >&2
    exit 1
fi

root="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
source_root="$(realpath -- "${1:?Usage: publish-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
package="$(realpath -- "${2:?Usage: publish-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}")"
source_commit="${3:?Usage: publish-dev.sh SOURCE_DIR PACKAGE.deb APPLICATION_COMMIT}"
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
git merge-base --is-ancestor "$source_commit" origin/fix/5.0.2-priorites-fichiers
# The source commit is selected before CI runs and contains only the application
# change. Never publish the infrastructure commit or rewrite a refused target.
git diff --exit-code origin/main "$source_commit" -- .github/workflows
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
DEV corrective des cinq priorités de fiabilité des fichiers :

- vérification réelle du contenu des archives ;
- conservation d'une sauvegarde précédente en cas d'échec ;
- remontée des erreurs de création et d'extraction d'archives ;
- renommage cohérent des pièces jointes, y compris en cas de collision ;
- reprise temporisée des sauvegardes sans boucle bloquant l'interface.

Paquet Debian natif pour Ubuntu 26.04 amd64, construit avec le script officiel.
Compilation et tests de régression exécutés avant publication.
Commit applicatif : $source_commit
Commit de l'infrastructure CI : $GITHUB_SHA
Résultats de la recette du paquet retéléchargé :
$GITHUB_SERVER_URL/$GITHUB_REPOSITORY/actions/runs/$GITHUB_RUN_ID

La validation interactive reste à effectuer avant toute version stable.
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
