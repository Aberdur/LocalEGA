#!/usr/bin/env bash
set -euo pipefail

SYNC_INTERVAL_SECONDS="${SYNC_INTERVAL_SECONDS:-60}"

ensure_homes() {
  local users_file="/opt/LocalEGA/etc/nss/users"
  if [[ ! -f "${users_file}" ]]; then
    return
  fi
  while IFS=: read -r _ _ _ gid _ homedir _; do
    if [[ -n "${homedir}" && ! -d "${homedir}" ]]; then
      mkdir -p "${homedir}"
      chown root:"${gid}" "${homedir}"
      chmod 0550 "${homedir}"
    fi
  done < "${users_file}"
}

while true; do
  ensure_homes
  sleep "${SYNC_INTERVAL_SECONDS}"
done
