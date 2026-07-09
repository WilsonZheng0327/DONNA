#!/usr/bin/env bash
# flash a desk node by its full desk identifier, e.g.:
#   ./flash.sh US-SVL-CRBN100-4-4T434G
#
# the id is COUNTRY-SITE-OFFICE-FLOOR-DESKID, the same path a desk uses in the
# database/ui (/US/SVL/CRBN100/4/4T434G). it is split on the first four dashes,
# so the deskId may itself contain dashes; the first four parts must not.
#
# those five parts become the node's build-time identity and its database path.
# a stable numeric NODE_ID (1..254, for the heartbeat stagger and node_id
# telemetry) is derived from the id, so nodes never need hand-assigned numbers.
#
# pass a second arg "build" to compile without uploading.
set -euo pipefail

if [ $# -lt 1 ]; then
  echo "usage: $0 <COUNTRY-SITE-OFFICE-FLOOR-DESKID> [build]" >&2
  echo "example: $0 US-SVL-CRBN100-4-4T434G" >&2
  exit 1
fi

id="$1"
target="upload"
[ "${2:-}" = "build" ] && target="build"

# split on the first four dashes; deskid keeps any remaining dashes
country="${id%%-*}"; rest="${id#*-}"
site="${rest%%-*}";  rest="${rest#*-}"
office="${rest%%-*}"; rest="${rest#*-}"
floor="${rest%%-*}"; deskid="${rest#*-}"

# require five non-empty parts (if no split happened, country still equals id)
if [ "$country" = "$id" ] || [ -z "$country" ] || [ -z "$site" ] \
   || [ -z "$office" ] || [ -z "$floor" ] || [ -z "$deskid" ]; then
  echo "error: id must be COUNTRY-SITE-OFFICE-FLOOR-DESKID (5 dash-separated parts): $id" >&2
  exit 1
fi

# db path segments allow only letters, digits, _ and -
if printf '%s' "$country$site$office$floor$deskid" | grep -q '[^A-Za-z0-9_-]'; then
  echo "error: id parts may only contain letters, digits, _ and -" >&2
  exit 1
fi

# stable per-desk number in 1..254 for the heartbeat stagger + node_id telemetry
node_id=$(( ($(printf '%s' "$id" | cksum | cut -d' ' -f1) % 254) + 1 ))

echo "flashing desk /$country/$site/$office/$floor/$deskid  (node_id=$node_id)"

# escaped quotes so the string values survive as C string literals, matching the
# -DDESK_ID_STR=\"...\" style platformio expects in build flags
export PLATFORMIO_BUILD_FLAGS="-DNODE_ID=$node_id \
-DDESK_COUNTRY=\\\"$country\\\" \
-DDESK_SITE=\\\"$site\\\" \
-DDESK_OFFICE=\\\"$office\\\" \
-DDESK_FLOOR=\\\"$floor\\\" \
-DDESK_ID_STR=\\\"$deskid\\\""

cd "$(dirname "$0")"
if [ "$target" = "upload" ]; then
  pio run -e node -t upload
else
  pio run -e node
fi
