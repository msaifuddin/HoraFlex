# HoraFlex

Horaco HC-SWTGW215AS as a UniFi Switch Flex Mini 2.5G.

Firmware that lets a UniFi Network controller adopt and manage a Horaco HC-SWTGW215AS
(5 x 2.5GbE RJ45 + 1 x 10G SFP+) as a **USW-Flex-2.5G-5** (Switch Flex Mini 2.5G).

The HC-SWTGW215AS uses the same class of Realtek RTL8372 switch chip as small 2.5G switches
in general and costs a fraction of the UniFi model. This project replaces its stock firmware with
[RTLPlayground](https://github.com/logicog/RTLPlayground), an open-source firmware for RTL837x
switches, and adds a UniFi device agent that runs on the switch's own 8051 CPU. The agent
implements the device side of the UniFi inform protocol: discovery by the controller, adoption,
AES-GCM encrypted informs, and applying the port and VLAN configuration the controller sends.
No extra hardware, Raspberry Pi or helper service is needed.

> Not affiliated with or endorsed by Ubiquiti or Horaco. UniFi is a trademark of Ubiquiti Inc.
> The UniFi device protocol is undocumented; this implementation follows observed controller
> behaviour. Replacing switch firmware can make the device unusable; without a SOIC-8 clip and SPI
> programmer a switch that does not boot cannot be recovered.

## What works

Tested on one HC-SWTGW215AS (stock firmware V200.1.10) with a UniFi Cloud Gateway Ultra running
UniFi Network 10.6.109:

| Feature | Status |
|---|---|
| Shows as Pending Adoption, adopts, stays online (AES-GCM) | tested |
| Port enable / disable | tested (link goes down and up) |
| Native VLAN, blocked tagged VLANs, port profile changes | tested |
| Tagged VLANs | tested in the switch's VLAN table; not checked with a packet capture |
| Port names | tested |
| Reboot and firmware update without re-adoption | tested |
| Forget / remove in UniFi (ports return to a plain switch) | tested |
| Link state, speed and packet counters per port | tested |
| 2.5G links, SFP+ port | not tested (the SFP+ is not shown in UniFi and keeps forwarding on VLAN 1) |
| Byte counters, client lists, LLDP topology, PoE | not provided |
| Management VLAN other than 1 | not supported |

Other controllers (UniFi OS consoles, self-hosted Network application) and other Network versions
may work but are untested. Other RTL8372/8373 switches supported by RTLPlayground may work with a
different `MACHINE`, but only this model has been tried.

## Port mapping

UniFi ports 1-5 are the front-panel RJ45 ports 1-5 (left to right). The SFP+ cage is not reported
to UniFi.

## Installation

You need a computer with a wired Ethernet port and the files from the
[latest release](../../releases/latest):

- `...-oem-upgrade.bin` for a switch still on its stock firmware
- `...-SWTG024AS_A_2_0_1_5C_1SFP.bin` for a switch already running RTLPlayground
- `SHA256SUMS` to check the downloads

### 1. Install on a switch with stock firmware

1. Connect the computer directly to any RJ45 port of the switch and give the computer a fixed
   address in the switch's network, for example `192.168.2.2`, netmask `255.255.255.0`:
   - Windows: Settings > Network > Ethernet > IP assignment > Edit > Manual > IPv4.
   - macOS: System Settings > Network > (the Ethernet adapter) > Details > TCP/IP > Configure IPv4: Manually.
2. Open `http://192.168.2.1` and log in to the stock web interface (default `admin` / `admin`).
   Note the firmware version under System; this was tested with V200.1.10.
3. Open System > Firmware Upgrade, confirm, and upload the **`-oem-upgrade.bin`** file. Do not upload
   the other `.bin` to the stock firmware.
4. Wait at least 5 minutes and do not power off the switch. It reboots into the new firmware.
5. Open `http://192.168.2.1` again. The new login page asks only for a password: `1234`.

### 2. Put the switch on your network

In the web interface open System > Startup configuration, set an address in your
LAN that is outside your DHCP range, and save:

```
ip 192.168.1.240
gw 192.168.1.1
netmask 255.255.255.0
passwd <a new password>
```

Reboot the switch (System > Reboot switch), move the computer back to your normal network, and connect one
switch port to your LAN. The switch is now at the address you set.

A static address is recommended: with `ip dhcp` the switch sends its DHCP request from its old
address, which some routers (including UniFi gateways) drop.

### 3. Adopt it in UniFi

1. Add two lines to the startup configuration, using your controller's address, and save:

   ```
   unifi uplink 1
   unifi controller 192.168.1.1
   ```

   `unifi uplink 1` protects the port that connects to your LAN (use that port's number): the switch
   refuses any controller setting that would disable it or remove VLAN 1 from it.
2. Reboot the switch. Within about 30 seconds it appears in UniFi Network > Devices as
   **USW Flex 2.5G 5**, pending adoption.
3. Click Adopt. After adoption, manage ports and VLANs from UniFi as usual.

## Everyday tasks

| Task | How |
|---|---|
| Update the firmware | Web interface > Firmware: upload the new `...-SWTG024AS_A_2_0_1_5C_1SFP.bin`. Adoption and settings are kept. |
| Check the UniFi status | System > Console: `unifi` (output goes to the serial console and syslog) |
| See the log | Set `syslog ip <computer>` and `syslog on` in the configuration and run a syslog receiver |
| Stop UniFi management | Remove the `unifi controller` line from the configuration and reboot |
| Return to a plain switch | Remove the device in UniFi (resets the ports), then remove the `unifi controller` line |
| Back up the switch's flash | See [doc/unifi.md](doc/unifi.md#flash-backup-without-a-programmer) |

Going back to the stock Horaco firmware needs a SPI programmer and a backup of the original flash.

## Troubleshooting

| Symptom | Check |
|---|---|
| Device never appears in UniFi | The switch must reach the controller on TCP 8080. Check the `unifi controller` address and `gw`. |
| Adoption fails / "Adopt failed" | Make sure `unifi discovery on` is not set: with discovery broadcasts the controller tries SSH adoption, which this switch does not have. Remove the device in UniFi and adopt again. |
| Settings from UniFi are not applied | Run `unifi`: "Config error" and "apply error" give the reason; error 9 means the change would have cut off the uplink port. |
| Web interface unreachable after a change | Power-cycle the switch; if the address was changed, the switch uses the saved configuration. |

## How it works

The agent (`unifi/`) informs the controller over HTTP every 10 seconds. Before adoption it uses
AES-128-CBC with UniFi's default key; the adoption response carries the device key, which is stored in
flash, and from then on all messages use AES-128-GCM. Port and VLAN settings arrive as `system_cfg`
properties; they are applied only from messages whose GCM tag verified, and the whole configuration is
rejected if any part of it is invalid. The applied configuration and its version are kept in flash and
applied again at boot. Details, build options and limits: [doc/unifi.md](doc/unifi.md).

## Building

See [RTLPlayground.md](RTLPlayground.md) for the toolchain (Debian or Docker); the first build compiles
the patched SDCC it needs. Then:

```
make MACHINE=SWTG024AS_A_2_0_1_5C_1SFP UNIFI_MODEL=USWED35 UNIFI_VERSION=2.1.8.971
make -C installer        # OEM upgrade image
make -C unifi/test       # host unit tests
unifi/release/build.sh   # both images and SHA256SUMS in release/
```

macOS without Docker: [tools/macos/README.md](tools/macos/README.md).

## Credits

- [RTLPlayground](https://github.com/logicog/RTLPlayground) (MIT License) by the RTLPlayground
  contributors: the switch firmware this project builds on. Its documentation is in
  [RTLPlayground.md](RTLPlayground.md) and [doc/](doc/).
- [unifi-stubd](https://github.com/konstruktor1/unifi-stubd) (AGPL-3.0): used as the protocol
  reference and to generate test vectors. No unifi-stubd code is included in the firmware.
- The board support for this switch comes from RTLPlayground issue #277 and its contributors.

## License

MIT, see [LICENSE](LICENSE). RTLPlayground, which this firmware is built on, is also MIT licensed; its
license and copyright notice are in [LICENSE.RTLPlayground](LICENSE.RTLPlayground).
