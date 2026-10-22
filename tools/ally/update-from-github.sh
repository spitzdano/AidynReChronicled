#!/usr/bin/env bash
# update-from-github.sh [version]: download a release of Aidyn ReChronicled from GitHub and install it (install.sh
# --no-steam: the Steam shortcut from the first install keeps working). Without a version: the newest release.
# Needs the GitHub CLI signed in (on Bazzite: brew install gh, then gh auth login); run it from anywhere in a clone.
#
#   tools/ally/update-from-github.sh            the newest release
#   tools/ally/update-from-github.sh 0.2.1      that one
# Saves and settings (~/.local/share/AidynReChronicled) are not touched.
set -euo pipefail
cd "$(dirname "$(readlink -f "$0")")/../.."
die() { echo "update-from-github.sh: $*" >&2; exit 1; }
command -v gh >/dev/null || die "the GitHub CLI isn't installed (brew install gh)"
gh auth status >/dev/null 2>&1 || die "not signed in to GitHub: run gh auth login"
pgrep -x AidynReChronicl >/dev/null && die "the game is running: quit it first"

tag=${1:+v${1#v}}
tag=${tag:-$(gh release view --json tagName -q .tagName)}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
echo "== downloading $tag"
gh release download "$tag" --pattern '*-linux-x86_64.tar.gz*' --dir "$tmp"
(cd "$tmp" && sha256sum -c ./*.sha256) || die "the download doesn't match its checksum"
tar -xzf "$tmp"/*-linux-x86_64.tar.gz -C "$tmp"
echo "== installing $tag"
bash "$tmp"/AidynReChronicled-*-linux-x86_64/install.sh --no-steam
echo "update-from-github.sh: Aidyn ReChronicled $tag installed"
