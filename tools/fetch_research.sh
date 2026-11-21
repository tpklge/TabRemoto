#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
fetch() {
  local dest="$1" url="$2" commit="$3"
  if [[ ! -d "$dest/.git" ]]; then
    git init "$dest"
    git -C "$dest" remote add origin "$url"
    git -C "$dest" fetch --depth 1 origin "$commit"
    git -C "$dest" checkout --detach FETCH_HEAD
  fi
  [[ "$(git -C "$dest" rev-parse HEAD)" == "$commit" ]]
  [[ -z "$(git -C "$dest" status --porcelain)" ]]
}
fetch research/FreeRDP https://github.com/FreeRDP/FreeRDP.git 90e7f064dc3ab8672b8ec714059d8cd51be0743f
python3 tools/prepare_network_sources.py
