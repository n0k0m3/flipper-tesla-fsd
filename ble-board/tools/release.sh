#!/usr/bin/env bash
# Publish a BLE board firmware release on GitHub (n0k0m3/flipper-tesla-fsd).
#
#   tools/release.sh [--beta] [--notes FILE] [SITE_DIR]
#
# SITE_DIR is the output of tools/make-flash-site.py (default build/flash-site).
# Test that site on scrl-flash-preview.surge.sh first. The script:
#   1. reads the version and the commit from SITE_DIR/builds.json and checks
#      the SHA-256 of every image in it,
#   2. makes the signed tag ble-board-v<version> on that commit and pushes it
#      to Forgejo and to GitHub (the commit must be on Forgejo already),
#   3. makes the GitHub release from the tag, with the flash site and all
#      images as assets,
#   4. runs the "pages" workflow, which copies the release to GitHub Pages:
#      /ble-board/ (stable) or /ble-board/beta/ (test release).
#
# --beta, or a version with "-" (0.7.0-rc.1), makes a pre-release. The live
# TSL6 app offers only stable releases. The preview app offers the test
# release too.
#
# Make the tag here, never on GitHub only: Forgejo is the main repo.
set -euo pipefail

REPO=n0k0m3/flipper-tesla-fsd
GITHUB_URL="https://github.com/$REPO"
PAGES="https://n0k0m3.github.io/flipper-tesla-fsd/ble-board"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

beta=0
notes=""
site="$ROOT/build/flash-site"
while [ $# -gt 0 ]; do
  case "$1" in
    --beta) beta=1 ;;
    --notes) notes="$2"; shift ;;
    -h|--help) sed -n '2,22p' "$0"; exit 0 ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
    *) site="$1" ;;
  esac
  shift
done

die() { echo "release: $*" >&2; exit 1; }
[ -f "$site/builds.json" ] || die "no $site/builds.json. Run tools/make-flash-site.py first."
for f in index.html scrl.js ble-update.js; do [ -f "$site/$f" ] || die "no $site/$f"; done

version=$(jq -r '.version' "$site/builds.json")
commit=$(jq -r '.commit' "$site/builds.json")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || die "bad version in builds.json: $version"
[[ "$commit" =~ ^[0-9a-f]{7,40}$ ]] || die "the site is from a dirty tree or has no commit: $commit"
[[ "$version" == *-* ]] && beta=1
tag="ble-board-v$version"

# Every image of builds.json, with its SHA-256.
sums=$(jq -r '.builds[] | "\(.app_sha256)  \(.app)\n\(.sha256)  scrl-\(.id)-full.bin"' "$site/builds.json")
(cd "$site/firmware" && sha256sum -c --quiet <<<"$sums") || die "an image does not match builds.json"
mapfile -t images < <(jq -r '.builds[] | .app, "scrl-\(.id)-full.bin"' "$site/builds.json")

cd "$ROOT"
git fetch -q forgejo
full=$(git rev-parse --verify "$commit^{commit}") || die "commit $commit is not in this clone"
git branch -r --contains "$full" | grep -q '^ *forgejo/' || die "commit $commit is not on Forgejo. Push it first."
if [ "$beta" = 0 ]; then
  git merge-base --is-ancestor "$full" forgejo/main || die "a stable release must be on forgejo/main"
fi
if git ls-remote --exit-code "$GITHUB_URL" "refs/tags/$tag" >/dev/null; then die "$tag is on GitHub already"; fi
if gh release view "$tag" -R "$REPO" >/dev/null 2>&1; then die "the release $tag exists already"; fi

echo "Release $tag: commit $full, ${#images[@]} images, $( [ "$beta" = 1 ] && echo pre-release || echo stable)."
read -r -p "Publish? [y/N] " ok
[ "$ok" = y ] || die "stopped"

if ! git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
  git tag -a -m "BLE board firmware $version" "$tag" "$full"
fi
[ "$(git rev-parse "$tag^{commit}")" = "$full" ] || die "the local tag $tag is on another commit"
git push forgejo "refs/tags/$tag"
# The forgejo remote can push to GitHub too. Push there anyway, so the tag is on GitHub before the release.
git push "$GITHUB_URL" "refs/tags/$tag"

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
cp "$site/builds.json" "$site/index.html" "$site/scrl.js" "$site/ble-update.js" "$stage/"
for f in "${images[@]}"; do cp "$site/firmware/$f" "$stage/"; done

if [ -z "$notes" ]; then
  notes="$stage/NOTES.md"
  cat >"$notes" <<EOF
BLE board firmware $version (commit ${full:0:7}).

- USB: flash a board with the flash site at $PAGES/$( [ "$beta" = 1 ] && echo "beta/" ).
- Bluetooth: the TSL6 app (Device > Firmware) shows an update when the board runs an older version.
- \`scrl-<build>.bin\` is the app image for the Bluetooth update. \`scrl-<build>-full.bin\` is the image at offset 0 for USB.
- \`builds.json\` lists each build: board ID, CAN controller, SHA-256.
EOF
fi

flags=(--verify-tag --title "BLE board $version" --notes-file "$notes")
[ "$beta" = 1 ] && flags+=(--prerelease --latest=false)
(cd "$stage" && gh release create "$tag" -R "$REPO" "${flags[@]}" builds.json index.html scrl.js ble-update.js "${images[@]}")

gh workflow run pages.yml -R "$REPO" --ref main
echo "The pages workflow runs. In about 2 min, check:"
echo "  curl -s $PAGES/$( [ "$beta" = 1 ] && echo "beta/" )builds.json | jq '{tag, version, commit}'"
