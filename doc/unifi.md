# UniFi device emulation

An optional agent lets a UniFi Network controller adopt and manage an RTLPlayground switch as a
UniFi switch. It runs on the switch's 8051; no extra hardware or host is involved. The UniFi
identity is a build option and separate from the hardware `MACHINE`.

Tested on a Horaco HC-SWTGW215AS (`MACHINE=SWTG024AS_A_2_0_1_5C_1SFP`) as USWED35
(USW-Flex-2.5G-5) with a UCG Ultra running UniFi Network 10.6.109. Other switches and controller
versions are untested.

## Build

```
make MACHINE=SWTG024AS_A_2_0_1_5C_1SFP UNIFI_MODEL=USWED35 UNIFI_VERSION=2.1.8.971
```

| Option | Meaning |
|---|---|
| `UNIFI_MODEL` | UniFi model code reported to the controller; enables the agent (`-DUNIFI`) |
| `UNIFI_VERSION` | firmware version reported (default 2.1.8.971) |
| `UNIFI_MODEL_DISPLAY` | display name (default "Switch Flex Mini 2.5G 5") |
| `FLASH_DUMP=1` | only the flash backup features below; implied by `UNIFI_MODEL` |

The agent lives in code BANK5 (flash 0x34000-0x3FFFF). Without `UNIFI_MODEL` the build is unchanged.

UniFi ports 1-5 are the front-panel ports 1-5 (`machine.phys_to_log_port`). Other ports, the SFP+
in particular, are not reported and keep their VLAN membership when the controller changes VLANs.

## Use

The agent is idle until it is given a controller:

```
unifi controller 192.168.1.1 [8080]   start informing this controller
unifi uplink 1                        protect UniFi port 1 as the uplink (default: auto)
unifi                                  status
unifi off | on                         stop / resume informing
unifi forget                           drop the adoption and reset the five ports
unifi discovery on|off                 UDP discovery broadcasts (default off, see below)
```

Put the commands in the configuration to keep them across reboots. While they are only entered at
the console, a power cycle returns the switch to plain RTLPlayground. Command output goes to the
serial console and syslog.

Then adopt the device in the controller. The switch must reach the controller by IP;
`set-inform` over SSH is not supported.

## What works

- Adoption under the default key, then AES-GCM with the controller's key.
- Port enable/disable (`switch.port.N.status`), native VLAN (PVID), tagged and excluded VLANs,
  port names. Any configuration error rejects the whole configuration; nothing is applied
  partially.
- The applied configuration and its cfgversion are stored in flash and applied at boot, as a
  UniFi device does; the controller does not re-provision a rebooted device.
- Forget/remove from the controller resets the five ports to VLAN 1 untagged.
- Telemetry: link, speed and packet/error counters per port.

## Safety

- Configuration is only applied from responses whose GCM tag verified.
- The uplink guard refuses configurations that would disable the uplink or take the untagged
  management VLAN off it. Only management VLAN 1 is supported.
- Firmware upgrade and command requests from the controller are logged and ignored.
- Adoption state uses two flash sectors (0x180000, 0x181000) with CRC and sequence numbers; a
  sector holding data that is not the agent's is never erased.
- GCM nonces are a stored epoch (written before the first GCM message of each boot) plus a counter.

## Limitations

- UDP discovery is off by default: with it, the controller tries SSH adoption, which fails.
- Byte counters, LLDP, client/FDB tables and PoE are not reported. 10/100 links report full duplex.
- GCM takes 1.6-1.9 s per message on the 8051 (bit-serial GHASH); the management plane waits,
  forwarding does not.
- Responses larger than 8 KiB are rejected (largest seen: about 2.6 KiB).

## Flash backup without a programmer

`GET /flash.bin?NN` (logged in) returns 32 KiB block NN, address `NN * 0x8000`:

```
curl -c c -d pwd=1234 http://SWITCH/login
for i in $(seq 0 63); do curl -b c "http://SWITCH/flash.bin?$(printf %02x $i)"; done > flash.bin
```

A staged update image that fails its CRC is kept instead of erased, so data left in
0x80000-0xFFFFF after an OEM install can still be read.

## Tests

`make -C unifi/test` runs the host unit tests (ASan/UBSan). `unifi/test/hostrun.c` runs the agent on
a host against a real controller.
