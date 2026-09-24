#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:?Usage: generate-changelog.sh <version> [output.md]}"
OUTPUT="${2:-RELEASE_NOTES.md}"

# Hand-written notes win: a release with a story (docs/release-notes/v<version>.md)
# should not be replaced by a list of commit subjects.
CURATED="docs/release-notes/v${VERSION}.md"
if [[ -f "${CURATED}" ]]; then
  cp "${CURATED}" "${OUTPUT}"
  echo "Using curated release notes: ${CURATED}"
  exit 0
fi

PREV="$(git tag -l 'v*' --sort=-version:refname | head -n1 || true)"

{
  echo "# JamesGames v${VERSION}"
  echo
  echo "## Changes"
  echo
  if [[ -z "${PREV}" ]]; then
    git log --pretty=format:'- %s (%h)' --no-merges
  else
    echo "Since ${PREV}:"
    echo
    git log "${PREV}..HEAD" --pretty=format:'- %s (%h)' --no-merges
  fi
  echo
  echo
  echo "## Downloads"
  echo
  echo "- **Windows:** \`JamesGames-${VERSION}-Setup.exe\`"
  echo "- **Linux:** \`JamesGames-${VERSION}-x86_64.AppImage\`"
  echo
  echo "Verify checksums in \`checksums.sha256\`."
} >"${OUTPUT}"

echo "Wrote ${OUTPUT}"
