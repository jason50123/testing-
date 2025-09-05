#!/bin/bash
set -euo pipefail
img="${1:?usage: $0 <disk.img>}"
d="$(dirname "$0")"

# 保留 statdir-layout 在最前，之後依序灌入三種 workload
"$d/statdir-layout.sh" "$img"
"$d/grep-many.sh"      "$img"
"$d/md5-many.sh"       "$img"
"$d/stats-many.sh"     "$img"
