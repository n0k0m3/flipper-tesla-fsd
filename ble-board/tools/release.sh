#!/usr/bin/env bash
# Publish a BLE board firmware release: a GitHub release (n0k0m3/flipper-tesla-fsd) and the flash site on surge.
#
#   tools/release.sh [--beta] [--notes FILE] [SITE_DIR]   a new release
#   tools/release.sh --promote VERSION                     make a test release stable
#
# SITE_DIR is the output of tools/make-flash-site.py (default build/flash-site). A new release:
#   1. reads the version and the commit from SITE_DIR/builds.json and checks the SHA-256 of every image in it,
#   2. makes the signed tag ble-board-v<version> on that commit and pushes it to Forgejo and to GitHub
#      (the commit must be on Forgejo already),
#   3. makes the GitHub release from the tag, with the flash site and all images as assets,
#   4. deploys the flash site (builds.json with "tag") to surge: a test release to scrl-flash-preview.surge.sh,
#      a stable release to scrl-flash.surge.sh.
#
# --beta, or a version with "-" (0.7.0-rc.1), makes a test release (a GitHub pre-release). The live TSL6 app reads
# only scrl-flash.surge.sh. The preview app also reads scrl-flash-preview.surge.sh, so test the release there.
# --promote then marks the GitHub release stable and deploys its own assets (the same bytes) to scrl-flash.surge.sh.
#
# Make the tag here, never on GitHub only: Forgejo is the main repo.
set -euo pipefail

REPO=n0k0m3/flipper-tesla-fsd
GITHUB_URL="https://github.com/$REPO"
LIVE=scrl-flash.surge.sh
PREVIEW=scrl-flash-preview.surge.sh
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

beta=0
notes=""
promote=""
site="$ROOT/build/flash-site"
while [ $# -gt 0 ]; do
  case "$1" in
    --beta) beta=1 ;;
    --notes) notes="$2"; shift ;;
    --promote) promote="$2"; shift ;;
    -h|--help) sed -n '2,19p' "$0"; exit 0 ;;
    -*) echo "unknown option: $1" >&2; exit 2 ;;
    *) site="$1" ;;
  esac
  shift
done

die() { echo "release: $*" >&2; exit 1; }
ask() { read -r -p "$1 [y/N] " ok; [ "$ok" = y ] || die "stopped"; }
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

# Check every image of DIR/builds.json against its SHA-256. Prints the image names.
check_images() {
  local sums
  sums=$(jq -r '.builds[] | "\(.app_sha256)  \(.app)\n\(.sha256)  scrl-\(.id)-full.bin"' "$1/builds.json")
  (cd "$1/firmware" && sha256sum -c --quiet <<<"$sums") || die "an image does not match builds.json"
  jq -r '.builds[] | .app, "scrl-\(.id)-full.bin"' "$1/builds.json"
}

# Deploy DIR to a surge domain and check that the domain serves its builds.json.
deploy() {
  echo "*" >"$1/CORS"
  surge "$1" "$2"
  local got
  got=$(curl -fsS "https://$2/builds.json?t=$(date +%s)" | jq -r '.tag')
  [ "$got" = "$(jq -r '.tag' "$1/builds.json")" ] || die "$2 serves $got, not the new release"
  echo "$2 serves $got."
}

if [ -n "$promote" ]; then
  tag="ble-board-v$promote"
  [[ "$promote" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "only a version without \"-\" can be stable. Make a new release for it."
  [ "$(gh release view "$tag" -R "$REPO" --json isPrerelease --jq .isPrerelease)" = true ] || die "$tag is not a test release"
  mkdir -p "$stage/firmware"
  gh release download "$tag" -R "$REPO" --dir "$stage" --pattern builds.json --pattern index.html --pattern scrl.js --pattern ble-update.js
  gh release download "$tag" -R "$REPO" --dir "$stage/firmware" --pattern 'scrl-*.bin'
  [ "$(jq -r '.tag' "$stage/builds.json")" = "$tag" ] || die "builds.json of $tag has another tag"
  mapfile -t images < <(check_images "$stage")
  ask "Make $tag stable (${#images[@]} images) and deploy it to $LIVE?"
  gh release edit "$tag" -R "$REPO" --prerelease=false --latest
  deploy "$stage" "$LIVE"
  exit 0
fi

[ -f "$site/builds.json" ] || die "no $site/builds.json. Run tools/make-flash-site.py first."
for f in index.html scrl.js ble-update.js; do [ -f "$site/$f" ] || die "no $site/$f"; done
version=$(jq -r '.version' "$site/builds.json")
commit=$(jq -r '.commit' "$site/builds.json")
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]] || die "bad version in builds.json: $version"
[[ "$commit" =~ ^[0-9a-f]{7,40}$ ]] || die "the site is from a dirty tree or has no commit: $commit"
[[ "$version" == *-* ]] && beta=1
tag="ble-board-v$version"
domain=$([ "$beta" = 1 ] && echo "$PREVIEW" || echo "$LIVE")

# The site with "tag" in builds.json. The release and surge get the same files.
cp -r "$site/." "$stage/"
jq --arg tag "$tag" '. + {tag: $tag}' "$site/builds.json" >"$stage/builds.json"
mapfile -t images < <(check_images "$stage")

cd "$ROOT"
git fetch -q forgejo
full=$(git rev-parse --verify "$commit^{commit}") || die "commit $commit is not in this clone"
git branch -r --contains "$full" | grep -q '^ *forgejo/' || die "commit $commit is not on Forgejo. Push it first."
if [ "$beta" = 0 ]; then
  git merge-base --is-ancestor "$full" forgejo/main || die "a stable release must be on forgejo/main"
fi
if git ls-remote --exit-code "$GITHUB_URL" "refs/tags/$tag" >/dev/null; then die "$tag is on GitHub already"; fi
if gh release view "$tag" -R "$REPO" >/dev/null 2>&1; then die "the release $tag exists already"; fi

ask "Release $tag: commit ${full:0:7}, ${#images[@]} images, $([ "$beta" = 1 ] && echo "test release" || echo stable), then deploy to $domain?"

if ! git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
  git tag -a -m "BLE board firmware $version" "$tag" "$full"
fi
[ "$(git rev-parse "$tag^{commit}")" = "$full" ] || die "the local tag $tag is on another commit"
git push forgejo "refs/tags/$tag"
# The forgejo remote can push to GitHub too. Push there anyway, so the tag is on GitHub before the release.
git push "$GITHUB_URL" "refs/tags/$tag"

if [ -z "$notes" ]; then
  notes="$stage.notes.md"
  trap 'rm -rf "$stage" "$notes"' EXIT
  cat >"$notes" <<EOF
BLE board firmware $version (commit ${full:0:7}).

- USB: flash a board with the flash site at https://$domain/.
- Bluetooth: the TSL6 app (Device > Firmware) shows an update when the board runs an older version.
- \`scrl-<build>.bin\` is the app image for the Bluetooth update. \`scrl-<build>-full.bin\` is the image at offset 0 for USB.
- \`builds.json\` lists each build: board ID, CAN controller, SHA-256.
EOF
fi

flags=(--verify-tag --title "BLE board $version" --notes-file "$notes")
[ "$beta" = 1 ] && flags+=(--prerelease --latest=false)
files=(builds.json index.html scrl.js ble-update.js)
for f in "${images[@]}"; do files+=("firmware/$f"); done
(cd "$stage" && gh release create "$tag" -R "$REPO" "${flags[@]}" "${files[@]}")

deploy "$stage" "$domain"
[ "$beta" = 1 ] && echo "Test it from the preview app. Then: tools/release.sh --promote $version"
exit 0
