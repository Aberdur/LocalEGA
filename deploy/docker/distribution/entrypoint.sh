#!/usr/bin/env bash
set -euo pipefail

SSHD_CONFIG=/opt/LocalEGA/etc/sshd/sshd_config
SSHD_CONFIG_TEMPLATE=/usr/local/share/ega/sshd_config
PAM_SSHD_TEMPLATE=/usr/local/share/ega/pam.sshd
FUSE_DB_CONF=/opt/LocalEGA/etc/fuse-vault-db.conf
FUSE_DB_CONF_TEMPLATE=/usr/local/share/ega/fs.conf.sample
SSHD_BIN=/opt/LocalEGA/sbin/sshd

if [[ ! -x "${SSHD_BIN}" ]]; then
  SSHD_BIN=/opt/LocalEGA/bin/sshd
fi

mkdir -p /run/ega-sshd /opt/LocalEGA/etc/sshd /opt/LocalEGA/homes /opt/LocalEGA/etc/nss
chmod 0755 /run/ega-sshd
chown root:root /opt/LocalEGA/homes
chmod 0755 /opt/LocalEGA/homes

if [[ -f "${SSHD_CONFIG_TEMPLATE}" ]]; then
  cp "${SSHD_CONFIG_TEMPLATE}" "${SSHD_CONFIG}"
fi

if [[ -f "${PAM_SSHD_TEMPLATE}" ]]; then
  cp "${PAM_SSHD_TEMPLATE}" /etc/pam.d/ega
  cp "${PAM_SSHD_TEMPLATE}" /etc/pam.d/sshd
fi

if [[ ! -f "${FUSE_DB_CONF}" ]]; then
  if [[ -z "${FUSE_DB_DSN:-}" ]]; then
    echo "FATAL: ${FUSE_DB_CONF} missing and FUSE_DB_DSN not set" >&2
    exit 1
  fi
  {
    echo "dsn = ${FUSE_DB_DSN}"
    if [[ -f "${FUSE_DB_CONF_TEMPLATE}" ]]; then
      grep -v '^dsn = ' "${FUSE_DB_CONF_TEMPLATE}"
    fi
  } > "${FUSE_DB_CONF}"
fi

if [[ "${EGA_PRECREATE_HOMES:-1}" != "0" ]]; then
  USERS_FILE=/opt/LocalEGA/etc/nss/users
  if [[ -f "${USERS_FILE}" ]]; then
    while IFS=: read -r username _ uid gid _ homedir _; do
      if [[ -n "${homedir}" && ! -d "${homedir}" ]]; then
        mkdir -p "${homedir}"
        chown root:"${gid}" "${homedir}"
        chmod 0550 "${homedir}"
      fi
    done < "${USERS_FILE}"
  fi
fi


if [[ ! -f /opt/LocalEGA/etc/sshd/host_ed25519_key ]]; then
  /opt/LocalEGA/bin/ssh-keygen -t ed25519 -N '' -f /opt/LocalEGA/etc/sshd/host_ed25519_key
fi

if [[ ! -f /opt/LocalEGA/etc/sshd/host_rsa_key ]]; then
  /opt/LocalEGA/bin/ssh-keygen -t rsa -b 3072 -N '' -f /opt/LocalEGA/etc/sshd/host_rsa_key
fi

touch /opt/LocalEGA/etc/sshd/banner

exec "${SSHD_BIN}" -D -e -f "${SSHD_CONFIG}"
