# Manual completo de instalación y validación de LocalEGA

Este manual describe el despliegue completo de LocalEGA en una VM Linux usando:

- `podman` rootless
- `podman compose`
- CEGA TEST real para submission, DAC y permisos
- distribution dentro de contenedores

El objetivo es que, al terminar, puedas validar de extremo a extremo:

1. login SFTP en `inbox`
2. subida de un `.c4gh`
3. `ingest` y `accession`
4. `mapping`, `release` y `permission`
5. actualización de claves de usuario
6. descarga por `distribution`

## Quickstart

Si ya conoces el stack y solo necesitas el orden correcto, este es el recorrido mínimo:

1. Preparar permisos y estructura de `data/` según [Estructura y permisos correctos](#estructura-y-permisos-correctos).
2. Construir imágenes según [Construcción de imágenes](#construcción-de-imágenes).
3. Arrancar `vault-db`, `mq`, `inbox`, `handler`, `distribution` y `nss-sync` según [Arranque correcto](#arranque-correcto).
4. Validar que los contenedores escuchan y consumen según [Validaciones iniciales](#validaciones-iniciales).
5. Subir el fichero al `inbox`, completar la submission y aprobarla según [Validación funcional contra CEGA TEST](#validación-funcional-contra-cega-test).
6. Solicitar acceso al dataset y aprobarlo en el DAC Portal.
7. Añadir una clave pública al perfil TEST del usuario y esperar `keys.updated`.
8. Descargar el fichero desde `distribution` según [Descarga por distribution](#descarga-por-distribution).

El resto del documento desarrolla esos pasos y recoge los fallos reales más frecuentes.

## Alcance y modos

Este documento se centra en el flujo que se ha validado en una VM real:

- `inbox`, `mq`, `handler`, `vault-db`, `distribution` y `nss-sync` en contenedores
- CEGA TEST real para los mensajes centrales
- acceso de requesters por SFTP a `distribution`

El modo `Fake CEGA` sigue siendo útil para pruebas locales aisladas, pero no sustituye el flujo real de:

- Submitter Portal
- DAC Portal
- keys del perfil de usuario

## Arquitectura mínima

| Componente | Rol |
|---|---|
| `inbox` | SFTP de subida |
| `mq` | RabbitMQ local con federación hacia CEGA TEST |
| `handler` | procesa `ingest`, `accession`, `mapping`, `release`, `permission`, `keys.updated`, etc. |
| `vault-db` | PostgreSQL con Vault, funciones SQL y exportación NSS |
| `distribution` | SFTP de descarga |
| `nss-sync` | sincroniza homes y artefactos NSS para distribution |

Puertos por defecto en este despliegue:

- `2222`: `inbox`
- `2224`: `distribution` (internamente `2223`)
- `5432`: `vault-db`
- `15672`: management de RabbitMQ

## Preparación e instalación

### Sistema

- Linux tipo RHEL/Ubuntu/Debian
- `podman`, `podman-compose` o `podman compose`
- `crypt4gh`
- `ssh-keygen`
- `psql`

### Podman rootless

Este punto es crítico. Si el rango de `subuid`/`subgid` es pequeño, fallará el login de `inbox` con errores tipo:

```text
setresuid ... Invalid argument
```

Verifica el mapeo:

```bash
podman unshare cat /proc/self/uid_map
podman unshare cat /proc/self/gid_map
```

Debes tener un rango suficientemente grande para cubrir los UIDs/GIDs que usa LocalEGA y los usuarios NSS remotos. Un ejemplo funcional es:

```text
         0       1003          1
         1      60000     200000
```

Si el rango es pequeño, hay que ampliar `/etc/subuid` y `/etc/subgid` para el usuario que ejecuta Podman rootless.

### Rutas y variables

Se asume el repo en:

```bash
/opt/localEGA/LocalEGA
```

y el trabajo desde:

```bash
/opt/localEGA/LocalEGA/deploy/docker
```

Define estas variables al comienzo:

```bash
export LEGA_UID=$(id -u)
export LEGA_GID=$(id -g)
export LOCALEGA_BASE=./data
```

### Configuración de CEGA TEST real

En `deploy/docker/docker-compose.yml`, para CEGA TEST real:

- `inbox` debe apuntar a `https://nss.test.ega-archive.org`
- `mq` debe apuntar a `rabbitmq4.test.ega-archive.org:5677/affiliates`
- debe existir `AFFILIATE_NAME`

Ejemplo validado:

```yaml
inbox:
  environment:
    - CEGA_ENDPOINT=https://nss.test.ega-archive.org
    - CEGA_ENDPOINT_CREDS=<affiliate-user>:<affiliate-secret>
    - MQ_CONNECTION=amqp://admin:secret@mq:5672/%2F
    - MQ_EXCHANGE=cega
    - MQ_ROUTING_KEY=files.inbox

mq:
  environment:
    - AFFILIATE_NAME=isciii-ciber
    - CEGA_CONNECTION=amqps://<affiliate>:<secret>@rabbitmq4.test.ega-archive.org:5677/affiliates
```

En este modo:

- no hace falta arrancar `cega`
- no hace falta arrancar `cega-mq`

### Preparación de ficheros

Desde `deploy/docker`:

```bash
cp docker-compose.yml.sample docker-compose.yml
cp ../../src/vault/pg.conf.sample pg.conf
cp ../../src/vault/pg_hba.conf.sample pg_hba.conf
cp ../../src/handler/conf.ini.sample lega.ini
```

Genera claves:

```bash
ssh-keygen -t ed25519 -f service.key -C "service_key@LocalEGA"
ssh-keygen -t ed25519 -f master.key -C "master_key@LocalEGA"
```

El `handler` usa:

- `service.key`
- `master.key.pub`
- `lega.ini`

Más adelante estas tres rutas tendrán que ser legibles por el UID real del `handler`.

## Estructura y permisos correctos

Este bloque es el punto más importante del manual. Si no queda así, aparecerán errores de `Permission denied` en cascada.

### Crear estructura

```bash
mkdir -p data/{inbox,staging,vault,vault.bkp,vault-db,etc/nss,etc/authorized_keys,homes,sqlite-boxes}
touch data/etc/nss/{users,groups,passwords}
```

### Permisos correctos para Podman rootless

Antes de arrancar nada:

```bash
podman unshare chown -R ${LEGA_UID}:${LEGA_GID} data/inbox data/staging
podman unshare chmod 2770 data/inbox
podman unshare chmod 2770 data/staging

podman unshare chown -R 999:999 data/vault-db data/etc/nss data/etc/authorized_keys
podman unshare chmod 775 data/etc/nss
podman unshare chmod 775 data/etc/authorized_keys
podman unshare chmod 664 data/etc/nss/users data/etc/nss/groups
podman unshare chmod 640 data/etc/nss/passwords

podman unshare chown ${LEGA_UID}:${LEGA_GID} service.key master.key.pub lega.ini
podman unshare chmod 400 service.key
podman unshare chmod 444 master.key.pub lega.ini
```

### Permisos correctos para `vault` y `vault.bkp`

El `handler` debe poder escribir en `vault`, pero `distribution` necesita leer los ficheros como grupo `requesters` (`gid 20000`).

Por tanto:

```bash
podman unshare chown -R ${LEGA_UID}:20000 data/vault data/vault.bkp
podman unshare find data/vault -type d -exec chmod 2750 {} \;
podman unshare find data/vault.bkp -type d -exec chmod 2750 {} \;
podman unshare find data/vault -type f -exec chmod 640 {} \;
podman unshare find data/vault.bkp -type f -exec chmod 640 {} \;
```

Este punto evita dos fallos distintos:

- `handler` no puede hacer `accession` en `vault`
- `distribution` lista datasets pero `get` falla con `No such file or directory`

## Construcción de imágenes

Desde `deploy/docker`:

```bash
make -j3 images LEGA_UID=$(id -u) LEGA_GID=$(id -g)
podman build -t localega/distribution:latest -f distribution/Dockerfile ../..
```

Si `podman compose up` intenta hacer `pull` de imágenes como `crg/fega-inbox:latest` o `fega/vault-db:latest`, retaga las locales:

```bash
podman tag localhost/crg/fega-inbox:latest crg/fega-inbox:latest
podman tag localhost/fega/handler:latest fega/handler:latest
podman tag localhost/fega/vault-db:latest fega/vault-db:latest
```

## Arranque correcto

Orden validado:

```bash
podman compose up -d vault-db mq inbox
podman compose up -d --no-deps handler
podman compose -f docker-compose.distribution.yml up -d
```

Comprueba:

```bash
podman ps -a --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}"
```

Deberías ver:

- `mq`
- `inbox`
- `vault-db`
- `handler`
- `distribution`
- `nss-sync`

## Validaciones iniciales

### Comprobar `mq`

```bash
podman logs --tail=50 mq
```

Debe acabar conectando la federación con CEGA TEST.

### Comprobar `inbox`

```bash
podman logs --tail=50 inbox
```

Debe mostrar algo como:

```text
Server listening on 0.0.0.0 port 9000.
```

### Comprobar `handler`

```bash
podman logs --tail=50 handler
```

Debe mostrar:

```text
Setup completed
Consuming
Start consuming from from_cega
```

### Comprobar `vault-db`

```bash
podman logs --tail=50 vault-db
```

Debe llegar a:

```text
database system is ready to accept connections
```

### Comprobar `distribution`

```bash
podman logs --tail=50 distribution
```

Debe escuchar en `2223` dentro del contenedor.

## Validación funcional contra CEGA TEST

### Login en `inbox`

```bash
sftp -P 2222 'your-mail@gmail.com'@localhost
```

Si sale warning de host key por recreación del contenedor:

```bash
ssh-keygen -R '[localhost]:2222'
```

Si al entrar aparece:

```text
remote readdir("/"): Permission denied
```

y ese usuario ya tenía home viejo en `data/inbox/<user>`, bórralo y vuelve a entrar:

```bash
podman unshare rm -rf data/inbox/'your-mail@gmail.com'
```

`inbox` lo recreará con ownership correcto al siguiente login.

### Subir un fichero de prueba

```bash
echo "LocalEGA CEGA TEST upload $(date -Iseconds)" > /tmp/test.txt
crypt4gh encrypt --recipient_pk service.key.pub < /tmp/test.txt > /tmp/test.txt.c4gh
```

Subida:

```bash
sftp -P 2222 'your-mail@gmail.com'@localhost
put /tmp/test.txt.c4gh
bye
```

### Completar la submission en CEGA TEST

En Submitter Portal:

1. crear Study / Sample / Experiment / Analysis / Dataset / Policy
2. enlazar el fichero subido
3. finalizar la submission
4. esperar la aprobación

Importante:

- subir al `inbox` no basta para ingestar
- CEGA solo envía `ingest` y el resto de mensajes cuando la submission sigue su flujo en Portal/API

### Qué mensajes deben llegar al `handler`

Observa en vivo:

```bash
podman logs -f handler
```

Secuencia esperada:

- `Job type: ingest`
- `Verification completed`
- `Job type: accession`
- `Publishing to exchange: cega [routing key: files.completed]`
- `Job type: mapping`
- `Job type: release`
- `Job type: permission`
- `Job type: keys.updated`

### Validar ingestión en base de datos

Ejemplo para el fichero de prueba:

```bash
podman exec -it vault-db psql -U postgres -d ega -c "SELECT stable_id, display_name, created_at FROM public.file_table WHERE display_name = 'test.txt.c4gh';"
podman exec -it vault-db psql -U postgres -d ega -c "SELECT stable_id, mount_point, relative_path FROM private.file_table WHERE stable_id = 'EGAF50000106649';"
```

### Validar release y permission

```bash
podman exec -it vault-db psql -U postgres -d ega -c "SELECT stable_id, is_released FROM public.dataset_table WHERE stable_id = 'EGAD50000000515';"
podman exec -it vault-db psql -U postgres -d ega -c "SELECT * FROM private.dataset_permission_table WHERE dataset_stable_id = 'EGAD50000000515';"
```

## Claves del requester y distribution

Para descargar por `distribution`, no basta con `permission`: el requester debe tener una clave registrada.

### Añadir clave pública en el perfil TEST

Genera una SSH key de prueba:

```bash
ssh-keygen -t ed25519 -f /tmp/ega_test_ed25519 -C "your-mail@gmail.com"
cat /tmp/ega_test_ed25519.pub
```

Añádela en:

- perfil del usuario en CEGA TEST
- pestaña `Public Keys`

Guarda y espera el mensaje `keys.updated`.

### Verificar la clave en DB

```bash
podman exec -it vault-db psql -U postgres -d ega -c "SELECT user_id, type FROM public.user_key_table WHERE user_id = 4;"
```

Debe aparecer:

```text
ssh-ed25519
```

## Descarga por distribution

### Probar el login

```bash
sftp -P 2224 'your-mail@gmail.com'@localhost
```

### Listar datasets y descargar

Dentro del SFTP:

```text
ls
ls EGAD50000000515/
get EGAD50000000515/test.txt.c4gh
bye
```

La validación final correcta es:

- el dataset aparece en `ls`
- el fichero aparece en `ls EGAD.../`
- `get` descarga el `.c4gh`

## Troubleshooting

### `setresuid ... Invalid argument` en `inbox`

Causa:

- rango `subuid`/`subgid` insuficiente en Podman rootless

Solución:

- ampliar `/etc/subuid` y `/etc/subgid`
- verificar con:

```bash
podman unshare cat /proc/self/uid_map
podman unshare cat /proc/self/gid_map
```

### `data directory "/ega/data" has wrong ownership`

Causa:

- `data/vault-db` quedó con ownership incompatible

Solución:

```bash
podman unshare chown -R 999:999 data/vault-db
podman rm -f vault-db
podman compose up -d vault-db
```

### `Permission denied: /ega/staging/<user>`

Causa:

- `data/staging` no pertenece al UID/GID real del `handler`

Solución:

```bash
podman unshare chown -R ${LEGA_UID}:${LEGA_GID} data/staging
podman unshare chmod 2770 data/staging
```

### `Permission denied: /etc/ega/service.seckey`

Causa:

- `service.key` no es legible por el `handler`

Solución:

```bash
podman unshare chown ${LEGA_UID}:${LEGA_GID} service.key master.key.pub lega.ini
podman unshare chmod 400 service.key
podman unshare chmod 444 master.key.pub lega.ini
```

### `Permission denied: /opt/LocalEGA/vault/...` durante `accession`

Causa:

- `vault` y `vault.bkp` no son escribibles por el `handler`

Solución:

```bash
podman unshare chown -R ${LEGA_UID}:20000 data/vault data/vault.bkp
podman unshare find data/vault -type d -exec chmod 2750 {} \;
podman unshare find data/vault.bkp -type d -exec chmod 2750 {} \;
podman unshare find data/vault -type f -exec chmod 640 {} \;
podman unshare find data/vault.bkp -type f -exec chmod 640 {} \;
```

### `could not open file "/etc/ega/nss/users" for writing`

Causa:

- PostgreSQL no puede escribir `data/etc/nss`

Solución:

```bash
podman unshare chown -R 999:999 data/etc/nss data/etc/authorized_keys
podman unshare chmod 775 data/etc/nss data/etc/authorized_keys
podman unshare chmod 664 data/etc/nss/users data/etc/nss/groups
podman unshare chmod 640 data/etc/nss/passwords
```

Y regenera:

```bash
podman exec -it vault-db psql -U postgres -d ega -c "SELECT * FROM nss.make_users();"
podman exec -it vault-db psql -U postgres -d ega -c "SELECT * FROM nss.make_groups();"
podman exec -it vault-db psql -U postgres -d ega -c "SELECT * FROM nss.make_passwords();"
podman exec -it vault-db psql -U postgres -d ega -c "SELECT nss.make_authorized_keys(id) FROM public.user_table;"
```

### `dac.dataset` se perdió por un `NACK`

No hay DLQ configurada por defecto. Si el mensaje se perdió y ya tienes el payload, puede reprocesarse manualmente desde SQL usando:

```sql
SELECT public.process_dac_dataset_message(<jsonb>);
```

Esto fue necesario cuando `dac.dataset` falló antes de corregir los permisos de `nss`.

### `ls` en `distribution` se queda colgado

Causa típica:

- sesión `internal-sftp` colgada
- mount FUSE stale
- lock stale del usuario

Limpieza:

```bash
podman exec -it distribution sh -lc "pkill -9 -f 'sshd: your-mail@gmail.com@internal-sftp' || true"
podman exec -it distribution sh -lc "pkill -9 -f 'crypt4gh-db.fs.*your-mail@gmail.com/outbox' || true"
podman exec -it distribution sh -lc "umount -l /opt/LocalEGA/homes/'your-mail@gmail.com'/outbox || true"
podman exec -it distribution sh -lc "rm -f /run/'your-mail@gmail.com'.lock"
```

### `get` falla con `No such file or directory` aunque el fichero se liste

Causa real:

- `distribution` sí ve el dataset y el fichero en SQL
- pero el proceso FUSE no puede hacer `open()` sobre el fichero real del `vault`
- el código traduce ese fallo a `ENOENT`

Verifica:

```bash
podman exec -it vault-db psql -U postgres -d ega -c "SELECT stable_id, mount_point, relative_path FROM private.file_table WHERE stable_id = 'EGAF50000106649';"
podman exec -it distribution sh -lc "ls -l /opt/LocalEGA/vault/EGA/F50/000/106/649"
```

La corrección es que `vault` tenga grupo `requesters` (`gid 20000`) y modo `640/2750`, no `1003:1003`.

### El dataset aparece, pero `ls EGAD.../` devuelve `Permission denied`

Causa:

- el usuario tiene `permission`, pero no tiene claves en `public.user_key_table`

Comprueba:

```bash
podman exec -it vault-db psql -U postgres -d ega -c "SELECT user_id, type FROM public.user_key_table WHERE user_id = 4;"
```

Si no hay filas:

- añade una clave pública al perfil TEST
- espera `keys.updated`

## Anexo: modo Fake CEGA

Para pruebas locales aisladas:

```bash
podman compose up -d cega-mq cega
```

En este modo:

- no dependes de Submitter Portal ni DAC Portal
- puedes simular mensajes localmente

Pero para validar el flujo real de afiliado, usa CEGA TEST.
