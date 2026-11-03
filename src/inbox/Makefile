SHELL := /bin/bash
COMMIT ?= $(shell git rev-parse HEAD)
ARGS = 

IMG=crg/fega-inbox:$(COMMIT)

.PHONY: build latest

all: latest

ARCH=$(shell uname -m)
ifeq ($(ARCH), arm64) # reset for MacOS
	ARCH=aarch64
endif


build: ARGS+=--target=BUILD
build latest:
ifeq ($(LEGA_GID),)
	$(error "Please specify the group id via the LEGA_GID variable")
endif
	docker build $(ARGS) \
	       --build-arg ARCH=$(ARCH) \
               --build-arg LEGA_GID=$(LEGA_GID) \
	       -t $(IMG) .
	docker tag $(IMG) crg/fega-inbox:$@
