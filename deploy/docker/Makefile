SHELL := /bin/bash

LEGA_UID := $(shell id -u lega 2>/dev/null)
LEGA_GID := $(shell id -g lega 2>/dev/null)
# LEGA_UID := $(shell id -u)
# LEGA_GID := $(shell id -g)

DOCKER ?= podman
SUDO ?= sudo
LOCALEGA_BASE ?= /impact_data/lega_data/lega
LOCALEGA_RUNTIME_BASE ?= ./data
NFS_IDMAP_DIR ?= /tmp/localega-idmap
REQUESTERS_GID ?= 20000
POSTGRES_UID ?= 999
POSTGRES_GID ?= 999
INBOX_GID ?= 1003

#############################
## Build docker images
#############################

define error-lega-gid-message
Please specify the group id via the LEGA_GID variable.
For example, by calling "make $@ LEGA_UID=$$(id -g lega)"
endef

image-inbox:
ifeq ($(LEGA_GID),)
	$(error $(error-lega-gid-message))
endif
	make -C ../../src/inbox latest LEGA_GID=$(LEGA_GID)

define error-lega-ugid-message
Please specify the user and group ids via the LEGA_UID/LEGA_GID variable.
For example, by calling "make $@ LEGA_UID=$$(id -u lega) LEGA_GID=$$(id -g lega)"
endef


image-handler:
ifeq ($(LEGA_UID),)
	$(error $(error-lega-ugid-message))
endif
ifeq ($(LEGA_GID),)
	$(error $(error-lega-ugid-message))
endif
	make -C ../../src/handler latest LEGA_UID=$(LEGA_UID) LEGA_GID=$(LEGA_GID)

image-vault:
	make -C ../../src/vault latest

images: image-inbox image-handler image-vault


#############################
## Initialize the database
#############################
init-vault:
ifeq ("$(wildcard pg_vault_su_password)","")
	$(error 'Please specify the superuser password file "pg_vault_su_password"')
endif
	make -C ../../src/vault init DB_DATA=$(shell pwd)/data/vault-db PG_SU_PASSWORD=$(file < pg_vault_su_password)

# Connect to the database from the host machine
export PGHOST=localhost
export PGPORT=5432
export PGUSER=postgres
export PGPASSWORD=$(file < pg_vault_su_password)
export PGDATABASE=ega
psql:
	psql


#############################
## Utility targets
#############################

down:
	docker-compose down -v
logs:
	docker-compose logs -f
ps:
	docker-compose ps

clean:
	-rm -rf data/{vault,vault.bkp,staging,inbox}/*
	-rm -rf data/vault-db
	-rm -f data/example.txt.c4gh data/example.txt

#############################
## Distribution
#############################

# we need to sort to return the files in the desired order
SQL_FILES=$(sort $(notdir $(wildcard ../../src/distribution/src/db/*.sql)))

echo:
	@echo $(SQL_FILES)

load: $(SQL_FILES:%=load-%)

export PGHOST=localhost
export PGPORT=5432
export PGUSER=postgres
export PGPASSWORD=$(file < pg_vault_su_password)
export PGDATABASE=ega

load-%:
	psql < ../../src/distribution/src/db/$*

nss-users:
	psql -c 'SELECT * FROM nss.make_users();'
nss-passwords:
	psql -c 'SELECT * FROM nss.make_passwords();'
nss-groups:
	psql -c 'SELECT * FROM nss.make_groups();'
nss-keys:
	psql -c 'SELECT nss.make_authorized_keys(r.db_uid) FROM public.requesters r;'
nss: nss-users nss-passwords nss-groups nss-keys



#############################
## Upload example
#############################

data/example.txt.c4gh: data/example.txt
	crypt4gh encrypt --recipient_pk service.key.pub < $< > $@

data/example.txt:
	dd if=/dev/urandom bs=1024 count=2 > $@

example: data/example.txt.c4gh

sftp:
	sftp -P 2222 john@localhost

permission:
	curl http://localhost:8080/permission/jane/EGAD90000000123


#############################
## Rootless Podman + NFS deployment helpers
#############################

.PHONY: nfs-help nfs-vars nfs-check nfs-init-layout nfs-print-layout-sudo \
	nfs-prepare-inbox-user nfs-print-inbox-user-sudo \
	nfs-refresh-distribution-nss nfs-check-distribution-user \
	nfs-stack-ps nfs-stack-logs

nfs-help:
	@echo "LocalEGA rootless Podman + NFS helpers"
	@echo
	@echo "Variables:"
	@echo "  LOCALEGA_BASE=$(LOCALEGA_BASE)"
	@echo "  LOCALEGA_RUNTIME_BASE=$(LOCALEGA_RUNTIME_BASE)"
	@echo "  DOCKER=$(DOCKER)"
	@echo "  SUDO=$(SUDO)"
	@echo
	@echo "Non-sudo checks:"
	@echo "  make nfs-vars"
	@echo "  make nfs-check"
	@echo "  make nfs-stack-ps"
	@echo "  make nfs-stack-logs"
	@echo
	@echo "Base NFS layout:"
	@echo "  make nfs-print-layout-sudo"
	@echo "  make nfs-init-layout"
	@echo
	@echo "Inbox user preparation:"
	@echo "  make nfs-print-inbox-user-sudo USER_EMAIL=user@example.org"
	@echo "  make nfs-prepare-inbox-user USER_EMAIL=user@example.org"
	@echo
	@echo "Distribution requester preparation:"
	@echo "  make nfs-refresh-distribution-nss"
	@echo "  make nfs-check-distribution-user USER_EMAIL=user@example.org"
	@echo "  Distribution homes/outbox are local runtime data and are created by nss-sync."

nfs-vars:
	@echo "DOCKER=$(DOCKER)"
	@echo "SUDO=$(SUDO)"
	@echo "LOCALEGA_BASE=$(LOCALEGA_BASE)"
	@echo "LOCALEGA_RUNTIME_BASE=$(LOCALEGA_RUNTIME_BASE)"
	@echo "NFS_IDMAP_DIR=$(NFS_IDMAP_DIR)"
	@echo "INBOX_GID=$(INBOX_GID)"
	@echo "REQUESTERS_GID=$(REQUESTERS_GID)"
	@echo "POSTGRES_UID=$(POSTGRES_UID)"
	@echo "POSTGRES_GID=$(POSTGRES_GID)"

nfs-stack-ps:
	@$(DOCKER) ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" | egrep 'mq|vault-db|inbox|handler|distribution|nss-sync' || true

nfs-stack-logs:
	@for c in mq vault-db inbox handler distribution nss-sync; do \
		echo; \
		echo "===== $$c ====="; \
		$(DOCKER) ps -a --filter name="^$${c}$$" --format "STATUS: {{.Status}}"; \
		$(DOCKER) logs --tail=30 "$$c" 2>&1 | egrep -i 'error|fatal|failed|denied|refused|critical|panic|exception|ready|listening|consuming|connected|accept' || true; \
	done

nfs-check:
	@set -euo pipefail; \
	echo "Checking $(LOCALEGA_BASE)"; \
	test -d "$(LOCALEGA_BASE)" || { echo "Missing LOCALEGA_BASE=$(LOCALEGA_BASE)"; exit 1; }; \
	ls -ldn "$(LOCALEGA_BASE)" "$(LOCALEGA_BASE)"/inbox "$(LOCALEGA_BASE)"/staging "$(LOCALEGA_BASE)"/vault "$(LOCALEGA_BASE)"/vault.bkp "$(LOCALEGA_BASE)"/vault-db "$(LOCALEGA_BASE)"/etc/nss "$(LOCALEGA_BASE)"/etc/authorized_keys 2>/dev/null || true; \
	echo; \
	echo "Checking local runtime $(LOCALEGA_RUNTIME_BASE)"; \
	ls -ldn "$(LOCALEGA_RUNTIME_BASE)" "$(LOCALEGA_RUNTIME_BASE)"/homes 2>/dev/null || true; \
	echo; \
	$(DOCKER) ps --format "table {{.Names}}\t{{.Status}}\t{{.Ports}}" | egrep 'mq|vault-db|inbox|handler|distribution|nss-sync' || true

nfs-print-layout-sudo:
	@set -euo pipefail; \
	mkdir -p "$(NFS_IDMAP_DIR)"; \
	map_id() { \
		label="$$1"; uid="$$2"; gid="$$3"; f="$(NFS_IDMAP_DIR)/$$label"; \
		rm -f "$$f"; touch "$$f"; \
		$(DOCKER) unshare chown "$$uid:$$gid" "$$f"; \
		stat -c '%u:%g' "$$f"; \
	}; \
	inbox_owner="$$(map_id inbox-root $(INBOX_GID) $(INBOX_GID))"; \
	postgres_owner="$$(map_id postgres $(POSTGRES_UID) $(POSTGRES_GID))"; \
	vault_owner="$$(map_id vault $(INBOX_GID) $(REQUESTERS_GID))"; \
	echo "# Review and run with sudo if correct"; \
	echo "$(SUDO) mkdir -p $(LOCALEGA_BASE)/{inbox,staging,vault,vault.bkp,vault-db,etc/nss,etc/authorized_keys,sqlite-boxes}"; \
	echo "mkdir -p $(LOCALEGA_RUNTIME_BASE)/homes"; \
	echo "$(SUDO) touch $(LOCALEGA_BASE)/etc/nss/{users,groups,passwords}"; \
	echo "$(SUDO) chown -R $$inbox_owner $(LOCALEGA_BASE)/inbox $(LOCALEGA_BASE)/staging"; \
	echo "$(SUDO) chmod 2775 $(LOCALEGA_BASE)/inbox"; \
	echo "$(SUDO) chmod 2770 $(LOCALEGA_BASE)/staging"; \
	echo "$(SUDO) chown -R $$postgres_owner $(LOCALEGA_BASE)/vault-db $(LOCALEGA_BASE)/etc/nss $(LOCALEGA_BASE)/etc/authorized_keys $(LOCALEGA_BASE)/sqlite-boxes"; \
	echo "$(SUDO) chmod 700 $(LOCALEGA_BASE)/vault-db"; \
	echo "$(SUDO) chmod 775 $(LOCALEGA_BASE)/etc/nss $(LOCALEGA_BASE)/etc/authorized_keys $(LOCALEGA_BASE)/sqlite-boxes"; \
	echo "$(SUDO) chmod 664 $(LOCALEGA_BASE)/etc/nss/users $(LOCALEGA_BASE)/etc/nss/groups"; \
	echo "$(SUDO) chmod 640 $(LOCALEGA_BASE)/etc/nss/passwords"; \
	echo "$(SUDO) chown -R $$vault_owner $(LOCALEGA_BASE)/vault $(LOCALEGA_BASE)/vault.bkp"; \
	echo "$(SUDO) find $(LOCALEGA_BASE)/vault $(LOCALEGA_BASE)/vault.bkp -type d -exec chmod 2750 {} \\;"; \
	echo "$(SUDO) find $(LOCALEGA_BASE)/vault $(LOCALEGA_BASE)/vault.bkp -type f -exec chmod 640 {} \\;"

nfs-init-layout:
	@set -euo pipefail; \
	mkdir -p "$(NFS_IDMAP_DIR)"; \
	map_id() { \
		label="$$1"; uid="$$2"; gid="$$3"; f="$(NFS_IDMAP_DIR)/$$label"; \
		rm -f "$$f"; touch "$$f"; \
		$(DOCKER) unshare chown "$$uid:$$gid" "$$f"; \
		stat -c '%u:%g' "$$f"; \
	}; \
	inbox_owner="$$(map_id inbox-root $(INBOX_GID) $(INBOX_GID))"; \
	postgres_owner="$$(map_id postgres $(POSTGRES_UID) $(POSTGRES_GID))"; \
	vault_owner="$$(map_id vault $(INBOX_GID) $(REQUESTERS_GID))"; \
	echo "Creating NFS layout under $(LOCALEGA_BASE)"; \
	$(SUDO) mkdir -p "$(LOCALEGA_BASE)"/{inbox,staging,vault,vault.bkp,vault-db,etc/nss,etc/authorized_keys,sqlite-boxes}; \
	mkdir -p "$(LOCALEGA_RUNTIME_BASE)"/homes; \
	$(SUDO) touch "$(LOCALEGA_BASE)"/etc/nss/{users,groups,passwords}; \
	echo "Preparing inbox/staging as $$inbox_owner"; \
	$(SUDO) chown -R "$$inbox_owner" "$(LOCALEGA_BASE)"/inbox "$(LOCALEGA_BASE)"/staging; \
	$(SUDO) chmod 2775 "$(LOCALEGA_BASE)"/inbox; \
	$(SUDO) chmod 2770 "$(LOCALEGA_BASE)"/staging; \
	echo "Preparing vault-db/NSS as $$postgres_owner"; \
	$(SUDO) chown -R "$$postgres_owner" "$(LOCALEGA_BASE)"/vault-db "$(LOCALEGA_BASE)"/etc/nss "$(LOCALEGA_BASE)"/etc/authorized_keys "$(LOCALEGA_BASE)"/sqlite-boxes; \
	$(SUDO) chmod 700 "$(LOCALEGA_BASE)"/vault-db; \
	$(SUDO) chmod 775 "$(LOCALEGA_BASE)"/etc/nss "$(LOCALEGA_BASE)"/etc/authorized_keys "$(LOCALEGA_BASE)"/sqlite-boxes; \
	$(SUDO) chmod 664 "$(LOCALEGA_BASE)"/etc/nss/users "$(LOCALEGA_BASE)"/etc/nss/groups; \
	$(SUDO) chmod 640 "$(LOCALEGA_BASE)"/etc/nss/passwords; \
	echo "Preparing vault/vault.bkp as $$vault_owner"; \
	$(SUDO) chown -R "$$vault_owner" "$(LOCALEGA_BASE)"/vault "$(LOCALEGA_BASE)"/vault.bkp; \
	$(SUDO) find "$(LOCALEGA_BASE)"/vault "$(LOCALEGA_BASE)"/vault.bkp -type d -exec chmod 2750 {} \; ; \
	$(SUDO) find "$(LOCALEGA_BASE)"/vault "$(LOCALEGA_BASE)"/vault.bkp -type f -exec chmod 640 {} \; ; \
	$(MAKE) nfs-check LOCALEGA_BASE="$(LOCALEGA_BASE)" DOCKER="$(DOCKER)"

nfs-print-inbox-user-sudo:
	@test -n "$(USER_EMAIL)" || { echo "Usage: make $@ USER_EMAIL=user@example.org"; exit 2; }
	@set -euo pipefail; \
	uid="$$( $(DOCKER) exec inbox id -u '$(USER_EMAIL)' )"; \
	gid="$$( $(DOCKER) exec inbox id -g '$(USER_EMAIL)' )"; \
	mkdir -p "$(NFS_IDMAP_DIR)"; \
	f="$(NFS_IDMAP_DIR)/inbox-$$(echo '$(USER_EMAIL)' | tr -c 'A-Za-z0-9_.@-' '_')"; \
	rm -f "$$f"; touch "$$f"; \
	$(DOCKER) unshare chown "$$uid:$$gid" "$$f"; \
	host_owner="$$(stat -c '%u:%g' "$$f")"; \
	echo "# inbox user: $(USER_EMAIL)"; \
	echo "# container UID:GID $$uid:$$gid -> host $$host_owner"; \
	echo "$(SUDO) mkdir -p '$(LOCALEGA_BASE)/inbox/$(USER_EMAIL)'"; \
	echo "$(SUDO) chown $$host_owner '$(LOCALEGA_BASE)/inbox/$(USER_EMAIL)'"; \
	echo "$(SUDO) chmod 2755 '$(LOCALEGA_BASE)/inbox/$(USER_EMAIL)'"

nfs-prepare-inbox-user:
	@test -n "$(USER_EMAIL)" || { echo "Usage: make $@ USER_EMAIL=user@example.org"; exit 2; }
	@set -euo pipefail; \
	uid="$$( $(DOCKER) exec inbox id -u '$(USER_EMAIL)' )"; \
	gid="$$( $(DOCKER) exec inbox id -g '$(USER_EMAIL)' )"; \
	mkdir -p "$(NFS_IDMAP_DIR)"; \
	f="$(NFS_IDMAP_DIR)/inbox-$$(echo '$(USER_EMAIL)' | tr -c 'A-Za-z0-9_.@-' '_')"; \
	rm -f "$$f"; touch "$$f"; \
	$(DOCKER) unshare chown "$$uid:$$gid" "$$f"; \
	host_owner="$$(stat -c '%u:%g' "$$f")"; \
	echo "Preparing Inbox home for $(USER_EMAIL): container $$uid:$$gid -> host $$host_owner"; \
	$(SUDO) mkdir -p "$(LOCALEGA_BASE)/inbox/$(USER_EMAIL)"; \
	$(SUDO) chown "$$host_owner" "$(LOCALEGA_BASE)/inbox/$(USER_EMAIL)"; \
	$(SUDO) chmod 2755 "$(LOCALEGA_BASE)/inbox/$(USER_EMAIL)"; \
	$(DOCKER) exec --user "$$uid:$$gid" inbox stat -c '%n %u:%g %a %A' "/ega/inbox/$(USER_EMAIL)"

nfs-refresh-distribution-nss:
	@$(DOCKER) exec -i vault-db psql -U postgres -d ega -c "SELECT * FROM nss.make_users();"
	@$(DOCKER) exec -i vault-db psql -U postgres -d ega -c "SELECT * FROM nss.make_groups();"
	@$(DOCKER) exec -i vault-db psql -U postgres -d ega -c "SELECT * FROM nss.make_passwords();"
	@$(DOCKER) exec -i vault-db psql -U postgres -d ega -c "SELECT nss.make_authorized_keys(id) FROM public.user_table;"

nfs-check-distribution-user:
	@test -n "$(USER_EMAIL)" || { echo "Usage: make $@ USER_EMAIL=user@example.org"; exit 2; }
	@set -euo pipefail; \
	echo "Checking Distribution user $(USER_EMAIL)"; \
	$(DOCKER) exec distribution getent passwd "$(USER_EMAIL)"; \
	$(DOCKER) exec distribution getent shadow "$(USER_EMAIL)" >/dev/null; \
	$(DOCKER) exec distribution stat -c '%n %u:%g %a %A' "/opt/LocalEGA/homes/$(USER_EMAIL)" "/opt/LocalEGA/homes/$(USER_EMAIL)/outbox"
