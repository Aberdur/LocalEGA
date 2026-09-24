# Reinstalar los contenedores de la VM conservando los datos

Procedimiento para `dcontainers00` desde
`/opt/containers_apps/localega/LocalEGA`. El código nuevo utilizará el NFS y
los directorios locales que ya figuran en `deploy/docker/.env` de ese árbol.
Los contenedores actuales proceden de `TEST` y están detenidos. Su PostgreSQL
y sus claves son distintos; no copiar configuración ni datos de `TEST`.

Esta operación reinstala servicios sobre el estado existente. Para crear una
base vacía y claves nuevas, seguir [LEAME.md](../LEAME.md). **Aquí no ejecutar
`make init-vault`, ni borrar o cambiar de propietario los datos del NFS.**
Tampoco usar `podman compose down -v` ni `podman volume prune`.

## 1. Traer el código al checkout de referencia

Como `bioinfo`:

```bash
cd /opt/containers_apps/localega/LocalEGA
git status --short --branch
git fetch https://github.com/Aberdur/LocalEGA.git docs/production-deployment-guide
git switch -c docs/production-deployment-guide FETCH_HEAD
git submodule update --init --recursive
git status --short --branch
git rev-parse HEAD
```

La rama versiona el código y la guía. Los archivos locales ignorados por Git
(`.env`, claves y configuración) permanecen en `deploy/docker`. Si `git switch`
indica un conflicto, detenerse y revisar esos archivos; no usar `git clean`.

## 2. Comprobar la configuración y construir imágenes

```bash
cd /opt/containers_apps/localega/LocalEGA/deploy/docker
chmod 600 .env
podman unshare chown 0:999 pg.conf pg_hba.conf
podman unshare chmod 640 pg.conf pg_hba.conf

export LOCALEGA_MQ_VOLUME="$(podman inspect --format '{{range .Mounts}}{{if eq .Destination "/var/lib/rabbitmq"}}{{.Name}}{{end}}{{end}}' mq)"
test -n "$LOCALEGA_MQ_VOLUME"
podman volume exists "$LOCALEGA_MQ_VOLUME"

localega_compose() {
  local diagnostic rc
  diagnostic=$(mktemp /tmp/localega-compose.XXXXXX) || return 1
  chmod 600 "$diagnostic"
  if podman compose -p localega \
    -f docker-compose.yml \
    -f docker-compose.distribution.yml \
    -f docker-compose.existing-mq.yml \
    "$@" 2>"$diagnostic"; then
    rm -f "$diagnostic"
  else
    rc=$?
    printf 'Compose falló (%s); diagnóstico privado: %s\n' "$rc" "$diagnostic" >&2
    return "$rc"
  fi
}

localega_compose config >/dev/null
localega_compose build
```

El override reutiliza el volumen RabbitMQ actual. `config` se redirige porque
su salida incluiría credenciales. Algunas versiones de `podman-compose` también
imprimen la configuración completa por stderr al combinar varios YAML; la
función guarda ese diagnóstico con modo `600` y lo borra cuando el comando
termina bien. Si falla, revisar el archivo solo en la VM y eliminarlo después.
Mantener la misma sesión de shell en los pasos siguientes, o redefinir la
función y `LOCALEGA_MQ_VOLUME` al volver a entrar. Si `mq` ya se renombró,
obtener el volumen con `podman inspect previous-mq`.

Comprobar sin mostrar secretos que `.env` contiene
`LOCALEGA_DATA_BASE=/impact_data/lega_data/lega`,
`LOCALEGA_BIND_BASE=/srv/containers/bind/localega`, `LEGA_UID=1000`,
`LEGA_GID=1000` e `INBOX_GID=1003`. En esta VM,
`DISTRIBUTION_PORT=8087` publica el puerto interno `2223`.

## 3. Liberar los nombres y arrancar PostgreSQL

Los contenedores antiguos se renombran y permanecen detenidos. Así no se
elimina su base `TEST` ni su configuración. Comprobar antes que todos están
parados:

```bash
for c in inbox mq handler vault-db distribution nss-sync; do
  test "$(podman inspect --format '{{.State.Status}}' "$c")" = exited || exit 1
  if podman container exists "previous-$c"; then
    echo "Ya existe previous-$c" >&2
    exit 1
  fi
done

for c in inbox mq handler vault-db distribution nss-sync; do
  podman rename "$c" "previous-$c" || exit 1
done

localega_compose up -d --no-deps vault-db
podman exec vault-db pg_isready -U postgres -d ega
podman inspect --format '{{range .Mounts}}{{if eq .Destination "/ega/data"}}{{.Source}}{{end}}{{end}}' vault-db
podman logs --tail 100 vault-db
```

El montaje `/ega/data` debe apuntar a
`/impact_data/lega_data/lega/vault-db`. El arranque normal de PostgreSQL puede
escribir en esa base existente; si hay un snapshot disponible, conservarlo
antes de este paso.

## 4. Validar el esquema y arrancar los demás servicios

```bash
podman exec vault-db psql -U postgres -d ega -Atc \
  "SELECT to_regnamespace('fs') IS NOT NULL,
          to_regnamespace('nss') IS NOT NULL,
          to_regprocedure('public.sys2db_user_id(bigint)') IS NOT NULL,
          to_regclass('private.inbox_cleanup_table') IS NOT NULL;"
```

Las tres primeras columnas deben ser `t`. Si alguna es `f`, parar y revisar
esa base antes de aplicar SQL de Distribution: sus `CREATE SCHEMA` no son
idempotentes. Si solamente la cuarta columna es `f`, aplicar una vez la
migración del cleaner y repetir la consulta:

```bash
make apply-inbox-cleanup-migration DOCKER=podman
```

Tras validar la base:

```bash
localega_compose up -d --no-deps mq
localega_compose up -d --no-deps inbox
localega_compose up -d --no-deps handler
localega_compose up -d --no-deps nss-sync distribution
localega_compose ps
```

Comprobar que todos los bind mounts nuevos apuntan al checkout `localega` o
a las rutas persistentes previstas, y ninguno a `TEST`:

```bash
for c in inbox mq handler vault-db distribution nss-sync; do
  echo "=== $c ==="
  podman inspect --format '{{range .Mounts}}{{println .Source "->" .Destination}}{{end}}' "$c"
done
```

Seguir con las [validaciones funcionales](../LEAME.md#validaciones). Mantener
`INBOX_CLEANUP_DRY_RUN=true`; el cleaner no necesita arrancar para comprobar
los demás servicios. No eliminar los contenedores `previous-*` ni la base
`TEST` hasta cerrar la validación. Una vuelta a `TEST` después de procesar
mensajes en `localega` requiere revisar las diferencias de estado entre las
dos bases.
