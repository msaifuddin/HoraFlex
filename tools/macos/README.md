# Building on macOS

The supported build is Debian or the Docker image (see the top-level README). On macOS without
Docker these extra steps were used:

1. SDCC's build needs Boost headers: download a Boost release, unpack it, and run the first
   `make` with `CPPFLAGS=-I/path/to/boost CXXFLAGS=-I/path/to/boost`.
2. Build [argp-standalone](https://github.com/argp-standalone/argp-standalone) as `libargp.a`,
   then `ARGP=/path/to/argp-standalone tools/macos/host-tools.sh <MACHINE>`.
3. Put `tools/macos` on `PATH` for `objcopy` (it only converts Intel HEX to binary) and skip the
   tools subdirectory, which also builds a simulator that needs json-c:

```
PATH=$PWD/tools/macos:$PATH make MACHINE=SWTG024AS_A_2_0_1_5C_1SFP SUBDIRS= UNIFI_MODEL=USWED35
```

For the release script: `PATH=$PWD/tools/macos:$PATH MAKE_EXTRA=SUBDIRS= unifi/release/build.sh`.
