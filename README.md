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

The switch needs a fixed address in your home network. Pick one that is free and outside the range
your router hands out automatically (the "DHCP range", shown in your router's or UniFi's network
settings; for example, if the router hands out .100-.200, use .240).

In the web interface open **System > Startup configuration**, add these lines with your own values,
and save:

```
ip 192.168.1.240
gw 192.168.1.1
netmask 255.255.255.0
passwd MyNewPassword
```

| Line | Meaning |
|---|---|
| `ip` | the switch's new address |
| `gw` | gateway: your router's address (with UniFi, the gateway's address, e.g. the UCG or UDM) |
| `netmask` | your network's subnet mask; `255.255.255.0` for most home networks |
| `passwd` | new password for the switch's web interface (replaces `1234`) |

Then:

1. Reboot the switch: **System > Reboot switch**.
2. Set your computer back to automatic network settings (undo step 1 of the installation).
3. Connect **port 1** of the switch to your network (a port on your router or another switch).
4. Open `http://192.168.1.240` (your new address) to check the switch is reachable.

Use a fixed address as above. The `ip dhcp` option (address from the router) can fail: the switch
asks for an address while still using its old one, and some routers, including UniFi gateways,
ignore that request.

### 3. Adopt it in UniFi

1. In **System > Startup configuration**, add two more lines and save:

   ```
   unifi uplink 1
   unifi controller 192.168.1.1
   ```

   - `unifi controller` is the address of your UniFi Network controller: on a UniFi gateway
     (UCG, UDM, Cloud Key) it is that device's address; for the UniFi Network application on a
     computer, it is that computer's address.
   - `unifi uplink 1` tells the switch that port 1 is the cable to the rest of your network. The
     switch then refuses any setting from UniFi that would cut it off, such as disabling port 1 or
     removing the Default network from it. If you used another port in step 2, use its number.
2. Reboot the switch. Within about 30 seconds it appears in **UniFi Network > Devices** as
   **USW Flex 2.5G 5**, waiting for adoption.
3. Click **Adopt**. After adoption, manage ports and VLANs from UniFi as usual.

## Everyday tasks

| Task | How |
|---|---|
| Update the firmware | **Firmware** page: upload the new `...-SWTG024AS_A_2_0_1_5C_1SFP.bin`. Adoption and settings are kept. |
| Stop UniFi management | Remove the `unifi controller` line from the startup configuration and reboot. The ports keep their last settings. |
| Return to a plain switch | Remove the device in UniFi (this resets all ports to the Default network), then remove the `unifi controller` line. |
| See what the switch is doing | See "Reading the switch's log" below. |
| Back up the switch's memory | See [doc/unifi.md](doc/unifi.md#flash-backup-without-a-programmer). |

Going back to the stock Horaco firmware is only possible by opening the switch and writing a saved copy
of its original memory chip with a SPI programmer (a small USB device with a clip for the chip).

### Reading the switch's log

Status messages, including the output of the `unifi` command, are not shown on the web page. They go
to the switch's log, which it can send over the network ("syslog") to a computer:

1. Add to the startup configuration, with your computer's address, and save:

   ```
   syslog ip 192.168.1.50
   syslog on
   ```

2. On that computer, run a syslog receiver listening on UDP port 514, for example the free
   *Visual Syslog Server* on Windows, or on macOS/Linux:

   ```
   python3 -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.bind(('',514));[print(s.recv(4096).decode(errors='replace'),flush=True) for _ in iter(int,1)]"
   ```

   (On Linux this needs `sudo`.)
3. In **System > Console** of the switch, type `unifi` and press Enter. The status appears in the
   receiver: whether the switch is adopted, how many messages succeeded or failed, and any error.

## Troubleshooting

| Problem | What to check |
|---|---|
| The switch never appears in UniFi | The switch must be able to reach the UniFi controller. Check that the `unifi controller` line has the controller's correct address and that the `gw` line has your router's address. The controller listens on port 8080; a firewall between them must allow it. |
| UniFi shows "Adopt failed" | Remove the device in UniFi, wait a minute and adopt again. Make sure the configuration does not contain `unifi discovery on`: with it, UniFi tries to adopt through a remote login (SSH) this switch does not have. |
| A change made in UniFi has no effect | Read the switch's log (above) and type `unifi`. A "Config error" other than 0 means the switch refused the change; error 8 or 9 means it would have disabled port 1 or removed the Default network from it (see `unifi uplink`). |
| The switch's web page cannot be reached | Check the address you set with `ip`. Unplug the power for a few seconds and plug it back in; the switch starts with the saved startup configuration. |
| Unsure which firmware is running | The web interface's Dashboard shows the software version. |

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
