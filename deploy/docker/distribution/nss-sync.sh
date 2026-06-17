#!/usr/bin/env bash
set -euo pipefail

SYNC_INTERVAL_SECONDS="${SYNC_INTERVAL_SECONDS:-60}"

ensure_homes() {
  local users_file="/opt/LocalEGA/etc/nss/users"
  if [[ ! -f "${users_file}" ]]; then
    return
  fi
  while IFS=: read -r _ _ uid gid _ homedir _; do
    if [[ -z "${homedir}" ]]; then
      continue
    fi
    mkdir -p "${homedir}/outbox"
    chown root:"${gid}" "${homedir}"
    chmod 0550 "${homedir}"
    chown "${uid}:${gid}" "${homedir}/outbox"
    chmod 2700 "${homedir}/outbox"
  done < "${users_file}"
}

while true; do
  ensure_homes
  sleep "${SYNC_INTERVAL_SECONDS}"
done
