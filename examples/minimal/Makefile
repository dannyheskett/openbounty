# examples/minimal/Makefile
#
# Standalone build of the minimal external consumer. Assumes the parent
# repo's `make` has already produced ../../build/libobengine.a. Run from
# this directory.

REPO     := ../..
ENGINE   := $(REPO)/engine
LIBRARY  := $(REPO)/build/libobengine.a

CFLAGS  := -std=c99 -Wall -Wextra -O2 \
           -I$(ENGINE)/headless -I$(ENGINE)/include \
           -I$(REPO)/third_party/cjson
LDFLAGS := -lm -lpthread

minimal: main.c $(ENGINE)/host_noop.c $(LIBRARY)
	gcc $(CFLAGS) main.c $(ENGINE)/host_noop.c $(LIBRARY) -o minimal $(LDFLAGS)

run: minimal
	./minimal $(REPO)/assets/kings-bounty

clean:
	rm -f minimal

.PHONY: run clean
