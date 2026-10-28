# Host unit tests for the UniFi agent: make -C unifi/test
CC ?= cc
CFLAGS = -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter \
	-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -I..
SRCS = main.c ut.c fake_hw.c test_crypto.c test_tnbu.c test_cfg.c test_agent.c \
	../uf_crypto.c ../uf_util.c ../uf_tnbu.c ../uf_json.c ../uf_cfg.c \
	../uf_store.c ../uf_msg.c ../uf_http.c ../uf_agent.c

all: run

unit: $(SRCS) $(wildcard ../*.h) *.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

run: unit
	./unit
	python3 -I -c 'import json,sys; d=json.load(open("inform_sample.json")); assert len(d["port_table"])==5; print("inform JSON valid,", len(open("inform_sample.json").read()), "bytes")'

clean:
	rm -rf unit unit.dSYM hostrun hostrun.dSYM tnbu_ours.txt inform_sample.json

.PHONY: all run clean

# Agent on the host against a real controller (protocol checks, not part of "run")
hostrun: hostrun.c fake_hw.h $(filter ../%,$(SRCS)) $(wildcard ../*.h)
	$(CC) -std=c11 -g -O1 -Wall -Wextra -Werror -Wno-unused-parameter -I.. -o $@ hostrun.c $(filter ../%,$(SRCS))
