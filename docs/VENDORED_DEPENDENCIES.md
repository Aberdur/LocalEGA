# Vendored dependencies for the institutional LocalEGA snapshot

This repository includes the source of its former Git submodules as ordinary
files. The paths below were exported from the exact upstream commits recorded
by the previous LocalEGA snapshot (`0fced776406317ae920d3a645ecda1b755046f2d`). Nested dependencies are included too; a
recursive clone requires only this GitLab project, not GitHub.

| Path in LocalEGA | Upstream repository | Pinned commit |
|---|---|---|
| `src/vault/pg_crypt4gh` | `https://github.com/silverdaz/pg_crypt4gh.git` | `9da41e20b2b7334e00ebdcb04768adc1d871b7ae` |
| `src/vault/libsodium` | `https://github.com/jedisct1/libsodium.git` | `2d14733a5ee74512aa3e8eb100aefc7d549004e1` |
| `src/inbox` | `https://github.com/EGA-archive/LocalEGA-inbox.git` | `37acf6c5198337aca1bc32c1b745edd9d5ab00a7` |
| `src/inbox/src/openssh` | `https://github.com/openssh/openssh-portable.git` | `80a2f64b8c1d27383cc83d182b73920d1e6a91f1` |
| `src/inbox/src/auth` | `https://github.com/EGA-archive/EGA-auth.git` | `0e07f8e74f058074e60a439c1b21207962448c80` |
| `src/distribution` | `https://github.com/EGA-archive/LocalEGA-distribution.git` | `448c5ed371fb112524fabe2d420c00e8862ddc43` |
| `src/distribution/src/openssh/src` | `https://github.com/openssh/openssh-portable.git` | `1ca166dbb3c0ce632b98869cd955f69320aa6fe8` |
| `src/distribution/src/crypt4gh-sqlite` | `https://github.com/silverdaz/crypt4gh-sqlite.git` | `edfd7bf946bf0f5c51578875b226b591bcb7dc1a` |
| `src/distribution/src/pg_sqlite_fs` | `https://github.com/silverdaz/pg_sqlite_fs.git` | `491c80d110f5bf7a7d4f500e04c29650f84956f1` |
| `src/vault/pg_sqlite_fs` | `https://github.com/silverdaz/pg_sqlite_fs.git` | `491c80d110f5bf7a7d4f500e04c29650f84956f1` |

The original source files and license notices remain at their respective
paths. Upstream changes are **not** imported automatically: to update a
dependency, review its new commit and license, re-export its tracked files,
update this table, rebuild the affected images, and rerun installation and
end-to-end checks before approving a new LocalEGA commit.
