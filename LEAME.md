# Instalación limpia de LocalEGA con Podman rootless

Esta guía describe una instalación limpia de LocalEGA en una VM Linux mediante Podman rootless. Incluye la preparación del host, el despliegue y la comprobación del flujo básico de subida, ingesta, autorización y descarga.

La guía separa expresamente:

- los datos de gran tamaño alojados en NFS;
- la configuración y el runtime alojados en disco local;
- los identificadores internos de los contenedores;
- los UID/GID subordinados que Podman representa en el host.

No asume una instalación previa ni datos existentes. Para sustituir los contenedores de la VM conservando la base, Vault y claves actuales, seguir [el procedimiento de sustitución](docs/redeploy_existing_vm.md). Las demás operaciones de actualización, migración y recuperación deben documentarse y ejecutarse por separado.

## Índice

- [Alcance](#alcance)
- [Arquitectura](#arquitectura)
- [Decisiones previas de infraestructura](#decisiones-previas-de-infraestructura)
- [Requisitos](#requisitos)
- [Usuario rootless y mapeo de IDs](#usuario-rootless-y-mapeo-de-ids)
- [Rutas persistentes](#rutas-persistentes)
- [Preparar las rutas y SELinux](#preparar-las-rutas-y-selinux)
- [Obtener el código](#obtener-el-código)
- [Generar las claves](#generar-las-claves)
- [Configurar el despliegue](#configurar-el-despliegue)
- [Preparar permisos](#preparar-permisos)
- [Construir las imágenes](#construir-las-imágenes)
- [Inicializar Vault DB](#inicializar-vault-db)
- [Arrancar LocalEGA](#arrancar-localega)
- [Configurar la limpieza del Inbox](#configurar-la-limpieza-del-inbox)
- [Arranque automático](#arranque-automático)
- [Validaciones](#validaciones)
- [Operación diaria](#operación-diaria)
- [Backups](#backups)
- [Actualización y rollback](#actualización-y-rollback)
- [Troubleshooting](#troubleshooting)
- [Checklist de producción](#checklist-de-producción)

## Alcance

Este documento cubre:

- despliegue con Podman rootless;
- servicios `inbox`, `mq`, `handler`, `vault-db`, `distribution`, `nss-sync` e `inbox-cleaner`;
- persistencia en NFS y disco local;
- configuración centralizada en `deploy/docker/.env` siempre que el Compose lo permita;
- integración con CEGA TEST o CEGA de producción;
- validaciones de servicios, DNS, RabbitMQ, PostgreSQL, permisos y flujo funcional.

No cubre:

- alta administrativa del afiliado en CEGA;
- gestión funcional de submissions, datasets o DAC en portales externos;
- diseño de firewall, monitorización, backup o recuperación ante desastres;
- migración de una instalación existente.

## Arquitectura

| Componente | Función |
|---|---|
| `inbox` | Servicio SFTP de entrada para ficheros cifrados `.c4gh`. |
| `mq` | RabbitMQ local, conectado con CEGA mediante shovel/federation. |
| `handler` | Procesa eventos de ingesta, accession, mapping, release, permisos y claves. |
| `vault-db` | PostgreSQL con el estado y las funciones SQL de LocalEGA. |
| `distribution` | Servicio SFTP para descarga autorizada. |
| `nss-sync` | Sincroniza usuarios, grupos, claves y homes para `distribution`. |
| `inbox-cleaner` | Elimina de forma auditable ficheros del Inbox cuya ingesta finalizó y superó la retención configurada. |

Puertos configurados en esta guía:

| Variable | Puerto | Servicio |
|---|---:|---|
| `INBOX_PORT` | `8086` | SFTP Inbox |
| `MQ_MANAGEMENT_PORT` | `15672` | RabbitMQ Management |
| `VAULT_DB_PORT` | `5432` | PostgreSQL |
| `DISTRIBUTION_PORT` | `2224` | SFTP Distribution |

Publicar únicamente los puertos necesarios y limitar sus redes de origen. RabbitMQ Management y PostgreSQL no deben quedar expuestos sin una necesidad operativa y controles de red explícitos.

## Decisiones previas de infraestructura

Antes de instalar hay que acordar:

1. Usuario técnico que ejecutará Podman rootless.
2. Rangos `subuid` y `subgid` asignados a ese usuario.
3. Rutas de NFS y disco local.
4. Política de backup del NFS y PostgreSQL.
5. Puertos y reglas de firewall.
6. Método de arranque automático: systemd de usuario, Quadlet u otro mecanismo administrado.
7. Formato, retención y recogida de logs.

## Requisitos

En la VM:

- `git`;
- `podman`;
- `podman-compose` o un proveedor compatible con `podman compose`;
- `make`;
- `openssl`;
- `ssh-keygen`;
- cliente PostgreSQL para comprobaciones administrativas;
- herramientas SELinux (`semanage` y `restorecon`) si SELinux está activo.

El módulo Python `crypt4gh` se necesita en el entorno que extrae el secreto master de `master.key` (en los comandos de esta guía, la VM). Las herramientas para cifrar ficheros pueden ejecutarse fuera de la VM.

Comprobaciones iniciales:

```bash
podman info
podman ps
podman-compose version || podman compose version
git --version
getenforce 2>/dev/null || true
```

No se deben ejecutar los contenedores con `sudo`. Todos los comandos Podman del stack deben ejecutarse con el mismo usuario rootless.

## Usuario rootless y mapeo de IDs

Definir un usuario técnico para operar LocalEGA. Puede ser `lega` u otro usuario existente. En esta guía se representa como:

```text
<USUARIO_PODMAN>
<GRUPO_PODMAN>
```

Si hay que crearlo:

```bash
sudo useradd -m -s /bin/bash <USUARIO_PODMAN>
```

Comprobar su identidad en el host:

```bash
id <USUARIO_PODMAN>
```

### Subuid y subgid

El usuario necesita rangos suficientes en `/etc/subuid` y `/etc/subgid`. Sin ellos, los servicios pueden fallar con errores como `setresuid: Invalid argument`.

Ejecutado como `<USUARIO_PODMAN>`:

```bash
podman unshare cat /proc/self/uid_map
podman unshare cat /proc/self/gid_map
```

Ejemplo:

```text
         0       1003          1
         1      60000     200000
```

Si los rangos no existen o son insuficientes, corregirlos como administrador. Ejemplo orientativo:

```bash
sudo usermod \
  --add-subuids 60000-259999 \
  --add-subgids 60000-259999 \
  <USUARIO_PODMAN>
```

Después es necesario cerrar todas las sesiones del usuario y volver a entrar. `podman system migrate` solo debe ejecutarse si es necesario para aplicar el nuevo mapeo y después de evaluar los demás contenedores rootless del mismo usuario.

### IDs internos frente a IDs del host

`LEGA_UID` y `LEGA_GID` son los IDs internos con los que se construye y ejecuta el usuario `lega` en el contenedor. No tienen por qué coincidir con el UID/GID del usuario rootless del host.

Configuración utilizada en esta guía:

```env
LEGA_UID=1000
LEGA_GID=1000
INBOX_GID=1003
```

Para conocer cómo se representa un ID interno en el host:

```bash
PROBE=$(mktemp /tmp/localega-idmap.XXXXXX)
podman unshare chown 1000:1000 "$PROBE"
stat -c 'UID:GID del host = %u:%g' "$PROBE"
rm -f "$PROBE"
```

Esta traducción es especialmente importante en una implementación en un servidor NFS. Con `root_squash`, el usuario rootless puede no tener permiso para cambiar propietarios; en ese caso, aplicar los UID/GID traducidos desde una cuenta administrativa o desde el servidor NFS.

## Rutas persistentes

Rutas recomendadas para una instalación definitiva. Para ensayar en una VM
que ya contiene LocalEGA, utilizar el conjunto aislado indicado debajo:

```bash
REPO_BASE=/opt/containers_apps/localega
REPO_DIR=/opt/containers_apps/localega/LocalEGA
LOCALEGA_DATA_BASE=/impact_data/lega_data/lega
LOCALEGA_BIND_BASE=/srv/containers/bind/localega
LOCALEGA_RUNTIME_BASE=/srv/containers/bind/localega/runtime
LOCALEGA_LOG_DIR=/var/log/local/localega/app
```

### Ensayo limpio junto a una instalación existente

Para comprobar esta guía en `dcontainers00` sin tocar sus datos, utilizar un
checkout y subdirectorios exclusivos. El árbol definitivo, las claves y la
base actual permanecen intactos:

```bash
export REPO_BASE=/opt/containers_apps/localega
export REPO_DIR=/opt/containers_apps/localega/LocalEGA-cleancheck
LOCALEGA_DATA_BASE=/impact_data/lega_data/lega/cleancheck
LOCALEGA_BIND_BASE=/srv/containers/bind/localega/cleancheck
LOCALEGA_RUNTIME_BASE=/srv/containers/bind/localega/cleancheck/runtime
LOCALEGA_LOG_DIR=/var/log/local/localega/app/cleancheck
```

Al crear `.env` desde `.env.example`, usar esas cuatro rutas y añadir:

```env
COMPOSE_PROJECT_NAME=localega-cleancheck
CONTAINER_PREFIX=cleancheck-
IMAGE_TAG=cleancheck
DISTRIBUTION_PORT=8087
```

Los puertos pueden conservar los de la VM mientras los contenedores antiguos
estén detenidos. El proyecto, los nombres de contenedor, las imágenes y el
volumen nombrado de RabbitMQ quedan separados de los anteriores. Generar
claves nuevas y crear una base nueva solo dentro de `cleancheck`. No copiar
`.env`, claves ni archivos de PostgreSQL desde `LocalEGA` o `TEST`.

### NFS: datos de gran tamaño

En esta configuración se alojan en NFS:

```text
${LOCALEGA_DATA_BASE}/inbox
${LOCALEGA_DATA_BASE}/staging
${LOCALEGA_DATA_BASE}/vault
${LOCALEGA_DATA_BASE}/vault-db
```

El almacenamiento de `vault-db` debe cumplir los requisitos de consistencia y durabilidad de PostgreSQL. Si se utiliza almacenamiento local para PostgreSQL, cambiar esta ruta antes de desplegar.

### Disco local

En disco local se alojan:

```text
${LOCALEGA_BIND_BASE}/etc/nss
${LOCALEGA_BIND_BASE}/etc/authorized_keys
${LOCALEGA_BIND_BASE}/sqlite-boxes
${LOCALEGA_RUNTIME_BASE}/homes
${LOCALEGA_LOG_DIR}
```

No se crea `vault.bkp` en esta guía. El Vault debe protegerse mediante la política de backup réplica definida para el almacenamiento.

## Preparar las rutas y SELinux

Como administrador:

```bash
sudo mkdir -p "${REPO_BASE:-/opt/containers_apps/localega}"
mkdir -p "${LOCALEGA_DATA_BASE:-/impact_data/lega_data/lega}"
sudo mkdir -p "${LOCALEGA_RUNTIME_BASE:-/srv/containers/bind/localega/runtime}"
sudo mkdir -p "${LOCALEGA_LOG_DIR:-/var/log/local/localega/app}"

sudo chown <USUARIO_PODMAN>:<GRUPO_PODMAN> \
  "${REPO_BASE:-/opt/containers_apps/localega}"
sudo chown -R <USUARIO_PODMAN>:<GRUPO_PODMAN> \
  "${LOCALEGA_BIND_BASE:-/srv/containers/bind/localega}" \
  "${LOCALEGA_LOG_DIR:-/var/log/local/localega/app}"
```

Si el usuario rootless no puede crear el directorio del NFS, solicitarlo al administrador del almacenamiento. La propiedad definitiva de sus subdirectorios se configura más adelante según los IDs internos de cada contenedor. No hacer `chown -R` sobre datos de una instalación existente.

### SELinux en disco local

Si SELinux está activo:

```bash
sudo semanage fcontext -a \
  -t container_file_t \
  '/srv/containers/bind/localega(/.*)?'

sudo semanage fcontext -a \
  -t container_file_t \
  '/var/log/local/localega(/.*)?'

sudo restorecon -Rv \
  /srv/containers/bind/localega \
  /var/log/local/localega
```

Para NFS no se debe asumir que `restorecon` puede etiquetar cada fichero. El montaje debe proporcionar un contexto compatible, por ejemplo:

```text
context=system_u:object_r:container_file_t:s0
```

Configurar el montaje NFS de forma persistente antes de arrancar LocalEGA.

## Obtener el código

```bash
REPO_BASE=${REPO_BASE:-/opt/containers_apps/localega}
REPO_DIR=${REPO_DIR:-$REPO_BASE/LocalEGA}
cd "$REPO_BASE"
git clone --recurse-submodules \
  <URL_REPOSITORIO_LOCALEGA> \
  "$(basename "$REPO_DIR")"
cd "$REPO_DIR"
```

Seleccionar exclusivamente una rama o tag aprobada:

```bash
git fetch --all --tags
git checkout <TAG_O_RAMA_APROBADA>
git submodule update --init --recursive
git status --short
```

`git status --short` debe estar vacío antes de comenzar una instalación de producción.

## Generar las claves

Las claves tienen funciones distintas:

| Fichero | Uso |
|---|---|
| `service.key` | Clave privada utilizada por LocalEGA para procesar ficheros cifrados para el afiliado. |
| `service.key.pub` | Clave pública distribuida a las herramientas o submitters que cifran para este LocalEGA. |
| `master.key` | Clave privada master; debe custodiarse y no distribuirse a submitters. |
| `master.key.pub` | Clave pública utilizada por el flujo de archivado en Vault. |

Desde `deploy/docker`:

```bash
cd "${REPO_DIR:-/opt/containers_apps/localega/LocalEGA}/deploy/docker"

ssh-keygen \
  -t ed25519 \
  -f service.key \
  -C 'service_key@LocalEGA'

ssh-keygen \
  -t ed25519 \
  -f master.key \
  -C 'master_key@LocalEGA'
```

Guardar ambas passphrases y las claves privadas en un gestor de secretos.

El handler monta:

- `service.key`;
- `master.key.pub`;
- su configuración de aplicación.

El secreto master requerido por Vault DB se obtiene de la clave privada
master después de crear `pg.conf`. No imprimirlo en la terminal.

## Configurar el despliegue

```bash
cd "${REPO_DIR:-/opt/containers_apps/localega/LocalEGA}/deploy/docker"
cp .env.example .env
cp ../../src/vault/pg.conf.sample pg.conf
cp ../../src/vault/pg_hba.conf.sample pg_hba.conf
cp lega.ini.sample lega.ini
```

Comprobar que el módulo Python está disponible y escribir el secreto master
directamente en `pg.conf`. El script también fija `vault.dirpath` a la ruta
que monta Distribution. La passphrase se solicita de forma interactiva y no
aparece en el historial ni en la salida:

```bash
python3 -c 'import crypt4gh.keys'
python3 - <<'PY'
from getpass import getpass
from pathlib import Path
import os
import re
import tempfile
import crypt4gh.keys

path = Path("pg.conf")
config = path.read_text()
key = crypt4gh.keys.get_private_key(
    "master.key", lambda: getpass("Master key passphrase: ")
)
if len(key) != 32:
    raise SystemExit("La clave master debe tener 32 bytes")
config, count_key = re.subn(
    r"(?m)^crypt4gh\.master_seckey\s*=.*$",
    "crypt4gh.master_seckey = '" + key.hex() + "'",
    config,
)
config, count_vault = re.subn(
    r"(?m)^vault\.dirpath\s*=.*$",
    "vault.dirpath = '/opt/LocalEGA/vault'",
    config,
)
if (count_key, count_vault) != (1, 1):
    raise SystemExit("pg.conf no contiene exactamente una línea para cada ajuste")
fd, tmp = tempfile.mkstemp(prefix=".pg.conf.", dir=".")
try:
    os.fchmod(fd, 0o600)
    with os.fdopen(fd, "w") as stream:
        stream.write(config)
    os.replace(tmp, path)
finally:
    if os.path.exists(tmp):
        os.unlink(tmp)
print("pg.conf actualizado sin mostrar el secreto")
PY
```

El despliegue centraliza en `.env` las variables soportadas por Compose y los entrypoints.

Al combinar varios YAML, algunas versiones de `podman-compose` imprimen por
stderr toda la configuración interpolada, incluidas credenciales. Definir una
función que guarde ese diagnóstico de forma privada y lo elimine si el comando
termina bien:

```bash
localega_compose() {
  local diagnostic rc
  diagnostic=$(mktemp /tmp/localega-compose.XXXXXX) || return 1
  chmod 600 "$diagnostic"
  if podman compose \
    -f docker-compose.yml \
    -f docker-compose.distribution.yml \
    "$@" 2>"$diagnostic"; then
    rm -f "$diagnostic"
  else
    rc=$?
    printf 'Compose falló (%s); diagnóstico privado: %s\n' "$rc" "$diagnostic" >&2
    return "$rc"
  fi
}

localega_compose config >/dev/null
```

Volver a definir la función al iniciar otra sesión. Si un comando falla,
revisar el diagnóstico solo en la VM y borrarlo después.

### Variables locales

```env
APP_NAME=localega
CONTAINER_PREFIX=
IMAGE_TAG=latest

LOCALEGA_DATA_BASE=/impact_data/lega_data/lega
LOCALEGA_BIND_BASE=/srv/containers/bind/localega
LOCALEGA_LOG_DIR=/var/log/local/localega/app
LOCALEGA_RUNTIME_BASE=/srv/containers/bind/localega/runtime

INBOX_PORT=8086
MQ_MANAGEMENT_PORT=15672
VAULT_DB_PORT=5432
DISTRIBUTION_PORT=2224

LEGA_UID=1000
LEGA_GID=1000
INBOX_GID=1003

SYNC_INTERVAL_SECONDS=60
EGA_PRECREATE_HOMES=0
LEGA_LOG=info
EGA_SSH_BANNER="Affiliated EGA <nombre-afiliado>"
```

Los puertos publicados se eligen según el firewall de cada host. En la VM de referencia,
`DISTRIBUTION_PORT=8087`; el puerto interno de Distribution sigue siendo `2223`.

Usar `LEGA_LOG=debug` únicamente durante validaciones o troubleshooting controlado.

### Credenciales y parámetros CEGA

Obtener por un canal seguro los siguientes parámetros del entorno CEGA:

```env
CEGA_ENDPOINT=<URL_CEGA>
CEGA_ENDPOINT_CREDS=<USUARIO_AFILIADO>:<SECRETO_AFILIADO>
AFFILIATE_NAME=<NOMBRE_AFILIADO>
CEGA_CONNECTION=<AMQPS_CEGA>
```

Ejemplo de formato, no de valores:

```env
CEGA_ENDPOINT=https://nss.example.org
CEGA_ENDPOINT_CREDS=affiliate_user:affiliate_secret
AFFILIATE_NAME=affiliate-name
CEGA_CONNECTION=amqps://affiliate_user:affiliate_secret@rabbitmq.example.org:5677/affiliates
```

### RabbitMQ local

```env
MQ_USER=mqadmin
MQ_PASSWORD=<PASSWORD_RABBITMQ_LOCAL>
MQ_PASSWORD_HASH=<HASH_RABBITMQ_LOCAL>
MQ_EXCHANGE=cega
MQ_ROUTING_KEY=files.inbox

LEGA_MQ_CONNECTION=amqp://mqadmin:<PASSWORD_RABBITMQ_LOCAL>@mq:5672/%2F
```

Generar una contraseña fuerte:

```bash
openssl rand -base64 32
```

Generar el hash sin incluir la contraseña en el propio script:

```bash
read -rsp 'MQ password: ' MQ_PASSWORD_INPUT
echo

MQ_PASSWORD_INPUT="$MQ_PASSWORD_INPUT" python3 - <<'PY'
import base64
import hashlib
import os

password = os.environ["MQ_PASSWORD_INPUT"].encode()
salt = os.urandom(4)
digest = hashlib.sha256(salt + password).digest()
print(base64.b64encode(salt + digest).decode())
PY

unset MQ_PASSWORD_INPUT
```

`MQ_PASSWORD_HASH` debe recalcularse cada vez que cambie `MQ_PASSWORD`.

### Vault DB, Distribution y service key

Generar contraseñas diferentes para cada rol:

```env
LEGA_DB_PASSWORD=<PASSWORD_LEGA_DB>
DISTRIBUTION_DB_PASSWORD=<PASSWORD_DISTRIBUTION_DB>
SERVICE_KEY_PASSPHRASE=<PASSPHRASE_SERVICE_KEY>
```

Compose construye internamente `LEGA_DB_CONNECTION` y `FUSE_DB_DSN` a partir de estas contraseñas. No añadir los DSN completos a `.env`. Las contraseñas deben coincidir con las aplicadas posteriormente a los roles PostgreSQL.

### Configuración PostgreSQL

Editar `pg.conf` y `pg_hba.conf` según la política de red. `pg_hba.conf` debe limitar los orígenes a las redes y roles estrictamente necesarios.

Incluir en `pg.conf` el valor de `crypt4gh.master_seckey` generado anteriormente.
Configurar `vault.dirpath = '/opt/LocalEGA/vault'`: esta ruta se guarda con cada
accession y debe coincidir con el montaje de Vault dentro de `distribution`.
No usar la ruta del NFS tal como aparece en el host.


### Validar la configuración

Sin mostrar secretos:

```bash
localega_compose config >/dev/null

echo 'Compose configuration: OK'
```

Proteger los ficheros sensibles conservando el acceso del usuario `postgres`
dentro del contenedor. En el host rootless, `0:999` mantiene como propietario
al operador y asigna el grupo interno de PostgreSQL:

```bash
chmod 600 .env service.key master.key
podman unshare chown 0:999 pg.conf pg_hba.conf
podman unshare chmod 640 pg.conf pg_hba.conf
chmod 644 service.key.pub master.key.pub
```

No poner `pg.conf` en modo `600` con propietario interno `0`: el proceso
`vault-db`, que se ejecuta como `999:999`, no podría leerlo.

## Preparar permisos

Cargar únicamente las variables de rutas e IDs necesarias en esta sesión. Leer
`.env` como datos evita ejecutar su contenido como código de shell:

```bash
cd "${REPO_DIR:-/opt/containers_apps/localega/LocalEGA}/deploy/docker"
while IFS='=' read -r name value; do
  case "$name" in
    LOCALEGA_DATA_BASE|LOCALEGA_BIND_BASE|LOCALEGA_RUNTIME_BASE|LOCALEGA_LOG_DIR|LEGA_UID|LEGA_GID|INBOX_GID|IMAGE_TAG|CONTAINER_PREFIX|COMPOSE_PROJECT_NAME)
      export "$name=$value"
      ;;
  esac
done < .env

: "${LOCALEGA_DATA_BASE:?falta LOCALEGA_DATA_BASE}"
: "${LOCALEGA_BIND_BASE:?falta LOCALEGA_BIND_BASE}"
: "${LOCALEGA_RUNTIME_BASE:?falta LOCALEGA_RUNTIME_BASE}"
: "${LOCALEGA_LOG_DIR:?falta LOCALEGA_LOG_DIR}"
```

Crear la estructura:

```bash
mkdir -p "${LOCALEGA_DATA_BASE}"/{inbox,staging,vault,vault-db}
mkdir -p "${LOCALEGA_BIND_BASE}"/etc/{nss,authorized_keys}
mkdir -p "${LOCALEGA_BIND_BASE}"/sqlite-boxes
mkdir -p "${LOCALEGA_RUNTIME_BASE}"/homes
mkdir -p "${LOCALEGA_LOG_DIR}"/{inbox,mq,handler,vault-db,distribution,nss-sync,inbox-cleaner}
touch "${LOCALEGA_BIND_BASE}"/etc/nss/{users,groups,passwords}
```

### Resolver propietarios efectivos

Antes de aplicar permisos al NFS, calcular cómo representa el host los IDs internos:

```bash
IDMAP_DIR=$(mktemp -d /tmp/localega-idmap.XXXXXX)

for spec in \
  'handler 1000 1000' \
  'postgres 999 999' \
  'inbox-group 1000 1003' \
  'requesters 1000 20000'
do
  set -- $spec
  label=$1
  uid=$2
  gid=$3
  probe="$IDMAP_DIR/$label"
  touch "$probe"
  podman unshare chown "$uid:$gid" "$probe"
  stat -c "$label -> host %u:%g" "$probe"
done

rm -rf "$IDMAP_DIR"
```

Registrar los resultados de la instalación. No copiar IDs subordinados desde otra VM.

### Permisos del NFS

Los siguientes comandos usan IDs internos y funcionan cuando el usuario rootless puede cambiar propietarios:

```bash
podman unshare chown -R \
  "${LEGA_UID}:${LEGA_GID}" \
  "${LOCALEGA_DATA_BASE}/staging"

podman unshare find "${LOCALEGA_DATA_BASE}/staging" \
  -type d -exec chmod 2770 {} +
podman unshare find "${LOCALEGA_DATA_BASE}/staging" \
  -type f -exec chmod 660 {} +

podman unshare chown -R \
  "${LEGA_UID}:20000" \
  "${LOCALEGA_DATA_BASE}/vault"

podman unshare find "${LOCALEGA_DATA_BASE}/vault" \
  -type d -exec chmod 2750 {} +
podman unshare find "${LOCALEGA_DATA_BASE}/vault" \
  -type f -exec chmod 640 {} +

podman unshare chown -R \
  999:999 \
  "${LOCALEGA_DATA_BASE}/vault-db"

podman unshare find "${LOCALEGA_DATA_BASE}/vault-db" \
  -type d -exec chmod 700 {} +
podman unshare find "${LOCALEGA_DATA_BASE}/vault-db" \
  -type f -exec chmod 600 {} +
```

El Inbox requiere un tratamiento adicional:

- la raíz debe ser accesible por el grupo interno correspondiente;
- cada home debe pertenecer al UID del usuario CEGA dentro del contenedor;
- `.ssh` debe conservar permisos restrictivos;
- no se deben reasignar todos los homes al usuario `lega`.

Después de que `inbox` pueda resolver los usuarios CEGA, obtener sus IDs con:

```bash
podman exec "${CONTAINER_PREFIX:-}inbox" getent passwd '<USUARIO_EGA>'
podman exec "${CONTAINER_PREFIX:-}inbox" id '<USUARIO_EGA>'
```

Aplicar los propietarios traducidos al host mediante `podman unshare` o desde una cuenta con permisos administrativos sobre el NFS. Para cada home:

```text
propietario = UID interno del usuario EGA
grupo       = INBOX_GID
directorios = 2770 o 2775 según política
ficheros    = 660
.ssh        = 700
claves      = 600
```

### Permisos de configuración local

La clave privada de servicio se genera con el usuario del host, pero debe
pertenecer al UID/GID interno del handler antes de arrancarlo:

```bash
podman unshare chown "${LEGA_UID}:${LEGA_GID}" service.key
podman unshare chmod 600 service.key
```

```bash
podman unshare chown -R \
  999:999 \
  "${LOCALEGA_BIND_BASE}/etc/nss" \
  "${LOCALEGA_BIND_BASE}/etc/authorized_keys" \
  "${LOCALEGA_BIND_BASE}/sqlite-boxes"

podman unshare chmod 775 \
  "${LOCALEGA_BIND_BASE}/etc/nss" \
  "${LOCALEGA_BIND_BASE}/etc/authorized_keys"

podman unshare chmod 664 \
  "${LOCALEGA_BIND_BASE}/etc/nss/users" \
  "${LOCALEGA_BIND_BASE}/etc/nss/groups"

podman unshare chmod 640 \
  "${LOCALEGA_BIND_BASE}/etc/nss/passwords"
```

Los logs deben ser escribibles por el usuario del servicio correspondiente. Verificar cada directorio con un contenedor efímero o con el servicio arrancado antes de la validación funcional.

## Construir las imágenes

```bash
cd "${REPO_DIR:-/opt/containers_apps/localega/LocalEGA}/deploy/docker"

localega_compose build
```

Compose usa `LEGA_UID` y `LEGA_GID` de `.env` para construir el handler y
`LEGA_GID` para construir Inbox. También construye las imágenes de Vault DB y
Distribution. La imagen de Vault DB
resultante usa el tag de `IMAGE_TAG` (por defecto `latest`).

Registrar los IDs y tags de las imágenes desplegadas:

```bash
podman images --format \
  'table {{.Repository}}\t{{.Tag}}\t{{.ID}}\t{{.Created}}'
```

## Inicializar Vault DB

La inicialización crea el clúster PostgreSQL, los esquemas, roles, tablas, funciones y extensiones que necesita LocalEGA. Es obligatoria en una instalación limpia y no debe repetirse indiscriminadamente sobre una base existente.

Crear una contraseña de superusuario distinta de las contraseñas de aplicación:

```bash
read -rsp 'PostgreSQL superuser password: ' PG_SU_PASSWORD_INPUT
echo
umask 077
printf '%s\n' "$PG_SU_PASSWORD_INPUT" > pg_vault_su_password
unset PG_SU_PASSWORD_INPUT
```

Inicializar exactamente la ruta que monta Compose. El directorio del NFS ya
debe pertenecer al UID/GID interno `999:999`, como se indicó arriba:

```bash
make init-vault \
  DB_DATA="${LOCALEGA_DATA_BASE}/vault-db" \
  VAULT_INIT_IMAGE="localega/vault-db:${IMAGE_TAG:-latest}" \
  VAULT_INIT_USER=postgres \
  DOCKER=podman
```

Ejecutar el inicializador como `postgres` evita que el usuario rootless
intente reasignar recursivamente la propiedad del NFS. El comando se detiene
si ya existe `PG_VERSION`; no es un procedimiento de migración.

Arrancar `vault-db` y comprobarlo:

```bash
podman compose up -d vault-db

podman exec "${CONTAINER_PREFIX:-}vault-db" \
  pg_isready -U postgres -d ega

podman logs --tail 100 "${CONTAINER_PREFIX:-}vault-db"
```

Aplicar las contraseñas de los roles sin escribirlas en el historial. Una opción es entrar de forma interactiva:

```bash
podman exec -it "${CONTAINER_PREFIX:-}vault-db" \
  psql -U postgres -d ega
```

Dentro de `psql`:

```sql
\password lega
\password distribution
```

Las contraseñas introducidas deben coincidir con `LEGA_DB_PASSWORD` y `DISTRIBUTION_DB_PASSWORD`.

Cargar la capa SQL de Distribution. La inicialización de Vault ya creó el rol
`distribution`; este objetivo conserva las funciones de conversión de UID del
primer script y omite solamente su sentencia `CREATE USER`:

```bash
make load-distribution DOCKER=podman \
  VAULT_DB_CONTAINER="${CONTAINER_PREFIX:-}vault-db"
```

El objetivo usa `psql -v ON_ERROR_STOP=1` dentro de `vault-db` y detiene la
instalación ante un error SQL. Comprobar que los esquemas están presentes:

```bash
podman exec "${CONTAINER_PREFIX:-}vault-db" psql -U postgres -d ega -Atc \
  "SELECT to_regnamespace('fs'), to_regnamespace('nss');"
```

Validación mínima:

```bash
podman exec "${CONTAINER_PREFIX:-}vault-db" \
  psql -U postgres -d ega -Atc \
  'SELECT current_database(), current_user, now();'
```

## Arrancar LocalEGA

Arrancar por fases facilita localizar fallos:

```bash
cd "${REPO_DIR:-/opt/containers_apps/localega/LocalEGA}/deploy/docker"

podman compose up -d vault-db mq inbox
podman compose up -d --no-deps handler
localega_compose up -d --no-deps nss-sync distribution
```

Resultado esperado:

```text
mq             Up ...  docker_internal
vault-db       Up ...  docker_vault
inbox          Up ...  docker_internal,docker_external
handler        Up ...  docker_internal,docker_vault
nss-sync       Up ...  docker_vault
distribution   Up ...  docker_vault
```

Comprobar:

```bash
podman ps -a \
  --format 'table {{.Names}}\t{{.Status}}\t{{.Networks}}\t{{.Ports}}'
```

## Configurar la limpieza del Inbox

La política recomendada es conservar cada fichero durante 90 días desde que la ingesta se marca como completada. El cleaner solo debe borrar cuando:

1. existe un registro de ingesta completada;
2. el accession y el fichero archivado existen en Vault;
3. tamaño y metadatos del Inbox coinciden con el registro;
4. ha transcurrido la retención configurada.

Configuración inicial segura:

```env
INBOX_RETENTION_DAYS=90
INBOX_CLEANUP_DRY_RUN=true
INBOX_CLEANUP_INTERVAL_HOURS=24
INBOX_CLEANUP_BATCH_SIZE=1000
```

Crear el directorio de log con el propietario efectivo del usuario `lega`:

```bash
mkdir -p "${LOCALEGA_LOG_DIR}/inbox-cleaner"
podman unshare chown \
  "${LEGA_UID}:${LEGA_GID}" \
  "${LOCALEGA_LOG_DIR}/inbox-cleaner"
podman unshare chmod 750 \
  "${LOCALEGA_LOG_DIR}/inbox-cleaner"
```

Ejecutar primero una única simulación:

```bash
podman compose \
  run --rm --no-deps \
  inbox-cleaner --once
```

Revisar:

```bash
podman unshare tail -50 \
  "${LOCALEGA_LOG_DIR}/inbox-cleaner/cleanup.log"
```

Mantener `INBOX_CLEANUP_DRY_RUN=true` hasta validar candidatos, permisos, comprobación del Vault y auditoría. El cambio a `false` requiere aprobación operativa explícita.

Para ejecución periódica:

```bash
podman compose up -d inbox-cleaner
```

## Arranque automático

Elegir un mecanismo de autoarranque compatible con la administración de la VM. Las opciones habituales son:

- unidad systemd de usuario;
- Quadlet;
- otro sistema de automatización.

Para cualquier opción rootless es habitual habilitar linger:

```bash
sudo loginctl enable-linger <USUARIO_PODMAN>
```

No se proporciona una unidad universal porque el comportamiento de parada, recreación y dependencia entre servicios debe acordarse. En particular, un `podman compose down` elimina contenedores y redes del proyecto y no debe utilizarse como acción de parada por defecto sin validar su impacto.

El mecanismo elegido debe:

- arrancar después de que red y NFS estén disponibles;
- usar siempre el mismo usuario rootless;
- preservar datos y configuración;
- reiniciar servicios fallidos de forma controlada;
- permitir consultar estado y logs;
- no afectar otros stacks Podman de la VM.

## Validaciones

### Servicios, IP y DNS

```bash
podman ps -a \
  --format 'table {{.Names}}\t{{.Status}}\t{{.Networks}}'

podman inspect "${CONTAINER_PREFIX:-}mq" \
  --format '{{range .NetworkSettings.Networks}}{{.IPAddress}} {{end}}'
podman exec "${CONTAINER_PREFIX:-}inbox" getent hosts mq
podman exec "${CONTAINER_PREFIX:-}handler" getent hosts mq

podman inspect "${CONTAINER_PREFIX:-}vault-db" \
  --format '{{range .NetworkSettings.Networks}}{{.IPAddress}} {{end}}'
podman exec "${CONTAINER_PREFIX:-}handler" getent hosts vault-db
```

Los nombres deben resolver únicamente a las IP actuales de los contenedores en las redes compartidas.

### RabbitMQ

```bash
podman exec "${CONTAINER_PREFIX:-}mq" rabbitmq-diagnostics -q ping
podman exec "${CONTAINER_PREFIX:-}mq" rabbitmq-diagnostics check_local_alarms

podman exec "${CONTAINER_PREFIX:-}mq" rabbitmqctl list_queues \
  name messages messages_ready messages_unacknowledged consumers |
  egrep 'from_cega|to_cega|errors|system.errors'
```

Resultado esperado:

- `Ping succeeded`;
- sin alarmas locales;
- `to_cega` con un consumidor;
- `from_cega` con un consumidor;
- colas de error vacías o con mensajes conocidos e investigados.

### PostgreSQL

```bash
podman exec "${CONTAINER_PREFIX:-}vault-db" pg_isready -U postgres -d ega

podman exec "${CONTAINER_PREFIX:-}vault-db" \
  psql -U postgres -d ega -Atc \
  'SELECT current_database(), current_user, now();'
```

### Permisos del Inbox

Sustituir usuario e IDs por un usuario CEGA real:

```bash
podman exec --user <UID_EGA>:<INBOX_GID> "${CONTAINER_PREFIX:-}inbox" sh -c '
  test -r "/ega/inbox/<USUARIO_EGA>" && echo READABLE
  test -w "/ega/inbox/<USUARIO_EGA>" && echo WRITABLE
'
```

### Logs

```bash
for service in mq vault-db inbox handler nss-sync distribution; do
  echo "===== $service ====="
  podman logs --tail 100 "${CONTAINER_PREFIX:-}$service"
done

find "${LOCALEGA_LOG_DIR}" \
  -maxdepth 2 -type f -print | sort
```

No deben aparecer errores como:

- `Stale file handle`;
- `Permission denied`;
- resoluciones a IP antiguas;
- `connection refused` persistente;
- errores de descifrado con un fichero cifrado usando la clave pública vigente.

### Validación funcional

1. Obtener `service.key.pub` de la instalación vigente por un canal controlado.
2. Crear un fichero de prueba no sensible.
3. Cifrarlo con Crypt4GH usando esa clave pública.
4. Subirlo por SFTP al Inbox.
5. Confirmar el evento en `to_cega`/CEGA.
6. Completar el flujo de ingesta del entorno objetivo.
7. Confirmar el accession en Vault DB y el fichero en Vault.
8. Autorizar el acceso según el procedimiento CEGA/DAC.
9. Confirmar sincronización de identidad y claves.
10. Descargarlo desde `distribution`.

Un login SFTP y una escritura en Inbox no demuestran por sí solos que la ingesta completa funcione.

## Operación diaria

### Estado

```bash
podman ps -a \
  --format 'table {{.Names}}\t{{.Status}}\t{{.Networks}}\t{{.Ports}}'

podman exec "${CONTAINER_PREFIX:-}mq" rabbitmq-diagnostics -q ping
podman exec "${CONTAINER_PREFIX:-}vault-db" pg_isready -U postgres -d ega
```

### Logs

```bash
podman logs --tail 200 "${CONTAINER_PREFIX:-}handler"
podman logs -f "${CONTAINER_PREFIX:-}handler"
```

### Reiniciar un servicio

Antes de recrearlo, comprobar dependencias, redes y colas:

```bash
podman compose up -d \
  --force-recreate \
  --no-deps \
  handler
```

Después verificar DNS, consumidores y logs.

### Parar temporalmente

Usar `podman stop` sobre los servicios concretos cuando se quiera preservar contenedores y redes. Reservar `podman compose down` para operaciones planificadas que requieran retirar el proyecto.

## Backups

El backup debe cubrir por separado:

1. secretos y configuración;
2. PostgreSQL mediante backup lógico o físico compatible;
3. Vault e Inbox según la política del NFS;
4. NSS, authorized keys y SQLite auxiliares;
5. el volumen nombrado de RabbitMQ (`mq-data`), según la política de recuperación de colas;
6. documentación de versiones de código e imágenes.

### Configuración

Crear un destino protegido fuera del repositorio:

```bash
BACKUP_DIR=/ruta_backup_segura/localega_$(date +%Y%m%d_%H%M%S)
umask 077
mkdir -p "$BACKUP_DIR"

cp .env pg.conf pg_hba.conf \
  service.key service.key.pub master.key master.key.pub \
  "$BACKUP_DIR/"
```

El destino debe estar cifrado, restringido y sujeto a la política de gestión de secretos.

### PostgreSQL

```bash
podman exec "${CONTAINER_PREFIX:-}vault-db" \
  pg_dump -U postgres -d ega \
  > "$BACKUP_DIR/vault-db_ega.sql"
```

Comprobar que el dump no está vacío y realizar pruebas periódicas de restauración.

### Datos

No duplicar automáticamente grandes volúmenes de datos con `rsync`. La réplica, snapshot o backup del NFS debe documentar:

- frecuencia;
- retención;
- consistencia;
- recuperación;
- responsable;
- prueba de restauración.

## Actualización y rollback

Una actualización debe prepararse en una rama/tag aprobada y disponer de:

- backup verificado;
- inventario de imágenes actuales;
- plan de migración de base de datos;
- ventana de mantenimiento;
- procedimiento de rollback probado.

Secuencia orientativa:

```bash
cd "${REPO_DIR:-/opt/containers_apps/localega/LocalEGA}"
git fetch --all --tags
git checkout <TAG_O_RAMA_APROBADA>
git submodule update --init --recursive

cd deploy/docker
localega_compose config >/dev/null
```

Construir y recrear únicamente los servicios afectados. No ejecutar `podman system prune --volumes`, borrar rutas persistentes ni eliminar el almacenamiento rootless como parte de una actualización normal.

## Troubleshooting

### El nombre DNS apunta a varias IP antiguas

Comparar:

```bash
podman inspect "${CONTAINER_PREFIX:-}mq" \
  --format '{{range .NetworkSettings.Networks}}{{.IPAddress}} {{end}}'
podman exec "${CONTAINER_PREFIX:-}inbox" getent hosts mq
podman exec "${CONTAINER_PREFIX:-}handler" getent hosts mq
```

No reiniciar globalmente el DNS rootless sin comprobar qué otros stacks utiliza el mismo usuario. Primero identificar contenedores duplicados, almacenes Podman alternativos y redes antiguas.

### Podman rootless no puede mapear IDs

```bash
podman unshare cat /proc/self/uid_map
podman unshare cat /proc/self/gid_map
```

Corregir `subuid/subgid` como administrador. No cambiar mapeos con contenedores en ejecución.

### `Stale file handle` en NFS

Comprobar primero desde host y contenedor:

```bash
stat "${LOCALEGA_DATA_BASE}/inbox"
podman exec "${CONTAINER_PREFIX:-}inbox" stat /ega/inbox
findmnt -T "${LOCALEGA_DATA_BASE}/inbox"
```

Si el host funciona y el contenedor conserva un handle obsoleto, recrear únicamente el servicio afectado y volver a comprobar propietarios y permisos. Si el host también falla, revisar el montaje y el servidor NFS.

### Vault DB devuelve `Permission denied`

```bash
podman logs --tail 200 "${CONTAINER_PREFIX:-}vault-db"
podman top "${CONTAINER_PREFIX:-}vault-db" hpid huser hgroup user group args
findmnt -T "${LOCALEGA_DATA_BASE}/vault-db"
```

Comparar el propietario del NFS con el UID/GID subordinado que corresponde al usuario interno `postgres` (`999:999`). No aplicar `chown -R` hasta resolver correctamente la traducción.

### El handler no consume mensajes

```bash
podman logs --tail 200 "${CONTAINER_PREFIX:-}handler"
podman exec "${CONTAINER_PREFIX:-}handler" getent hosts mq
podman exec "${CONTAINER_PREFIX:-}handler" getent hosts vault-db
podman exec "${CONTAINER_PREFIX:-}mq" rabbitmqctl list_queues \
  name messages messages_ready messages_unacknowledged consumers
```

Validar `LEGA_MQ_CONNECTION`, `LEGA_DB_CONNECTION`, consumidores y DNS.

### Error decrypting this Crypt4GH file

Comprobar que el fichero fue cifrado con la `service.key.pub` correspondiente a la `service.key` privada actualmente desplegada. Regenerar claves SSH del Inbox no cambia la clave Crypt4GH, pero reemplazar `service.key` sí invalida los ficheros cifrados con otra clave pública.

### Distribution no permite login

```bash
ls -lah "${LOCALEGA_BIND_BASE}/etc/nss"
ls -lah "${LOCALEGA_BIND_BASE}/etc/authorized_keys"
podman logs --tail 200 "${CONTAINER_PREFIX:-}nss-sync"
podman logs --tail 200 "${CONTAINER_PREFIX:-}distribution"
```

### Espacio de Podman

```bash
df -h /srv/containers
podman system df
podman images --filter dangling=true
```

`podman image prune -f` elimina imágenes no referenciadas, pero debe revisarse antes en una VM compartida. No usar `--volumes`.

## Checklist de producción

- [ ] Usuario rootless y responsables operativos definidos.
- [ ] Rangos `subuid/subgid` suficientes y documentados.
- [ ] Rutas NFS y locales definidas.
- [ ] Montaje NFS y política SELinux comprobados.
- [ ] Política de backup y restauración aprobada.
- [ ] Repo limpio y tag/rama aprobada.
- [ ] `.env` completado sin `CHANGE_ME` ni variables Fake CEGA.
- [ ] Puertos y firewall restringidos.
- [ ] `service.key` y `master.key` generadas y custodiadas.
- [ ] Claves públicas distribuidas por el canal correcto.
- [ ] Secretos fuera de Git y del historial de shell.
- [ ] IDs internos y subordinados documentados.
- [ ] Propietarios y permisos de NFS verificados.
- [ ] Configuración local y logs con permisos correctos.
- [ ] Imágenes construidas y registradas.
- [ ] Vault DB inicializada en la ruta que monta Compose y roles configurados.
- [ ] `vault.dirpath` coincide con el montaje de Vault en Distribution.
- [ ] Capa SQL de Distribution cargada (`fs` y `nss`).
- [ ] `.env` tiene modo `600`; `pg.conf` y `pg_hba.conf` son legibles por el grupo interno `999` y no por otros.
- [ ] Servicios levantados en las redes esperadas.
- [ ] DNS interno consistente con las IP actuales.
- [ ] RabbitMQ sin alarmas y con consumidores activos.
- [ ] PostgreSQL acepta conexiones.
- [ ] Inbox legible y escribible por un usuario real.
- [ ] Flujo funcional completo comprobado.
- [ ] `inbox-cleaner` comprobado primero con `DRY_RUN=true`.
- [ ] Mecanismo de autoarranque aprobado y probado.
- [ ] Logs integrados con el sistema de monitorización.
- [ ] Backup inicial realizado y restauración documentada.
