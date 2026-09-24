
DB_DATA=$(shell pwd)/data
DOCKER ?= podman
INIT_USER ?= root
SERVICE=vault-db
IMG=fega/vault-db:latest

DATABASE_NAME=ega

all: latest

$(DB_DATA):
	mkdir -p $@
	-chown -R 999:999 $@ # that the postgres user and group in the container

build: ARGS+=--target BUILD
build latest:
	$(DOCKER) build $(ARGS) -t fega/vault-db:$@ .

###########################
rm:
	rm -rI $(DB_DATA)

###########################

init: | $(DB_DATA)
init: MOUNTPOINTS=
init: ENVVARS=

init: MOUNTPOINTS+=-v $(DB_DATA):/ega/data
init: MOUNTPOINTS+=-v $(shell pwd)/build.sh:/ega/build.sh
init: MOUNTPOINTS+=-v $(shell pwd)/initdb.d:/ega/initdb.d
init: ENVVARS+=-e PGDATA=/ega/data
init: ENVVARS+=-e POSTGRES_PASSWORD
init: ENVVARS+=-e POSTGRES_DB=$(DATABASE_NAME)

ifeq ($(DEBUG),1)
init: ENTRYPOINT=-d --entrypoint /bin/sleep
init: CMD=356d
else
init: ENTRYPOINT=-it --entrypoint /ega/build.sh --user $(INIT_USER)
endif
init:
	$(DOCKER) run --rm \
	$(MOUNTPOINTS) \
	$(ENVVARS) \
	--hostname "$(SERVICE)-build" \
	--name "$(SERVICE)-build" \
	$(ENTRYPOINT) $(IMG) $(CMD)

down:
	-$(DOCKER) stop $(SERVICE)-build
	-$(DOCKER) rm $(SERVICE)-build
