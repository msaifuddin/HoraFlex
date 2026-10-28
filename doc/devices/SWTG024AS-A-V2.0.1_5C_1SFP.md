### SWTG024AS-A-V2.0.1_5C_1SFP
It is highly similar to SWTG024AS-V2.0, with the only difference being the GPIO configuration for the SFP port.

## Brands
|Brand|Type|Managed|PCB|Flash|Chip RTL|
|---|---|---|---|---|---|
| Horaco | HC-SWTGW215AS |  | PCB-SWTG024AS-A-V2.0.1_19650 | W25Q16JV | 8272N |

### Horaco HC-SWTGW215AS notes

Seen on a unit with stock firmware V200.1.10 (installed with the OEM web upgrade image):

- Flash JEDEC ID EF 40 15 (Winbond W25Q16, 2 MiB).
- Factory MAC at 0x1FC000 (factory data up to about 0x1FEA68); the board file reads it there.
- Vendor web logo at 0x1F9000; 0x101000-0x1F8FFF blank after installation.
- Front ports 1-5 are logical 4, 5, 6, 7, 3; amber LED for 10/100/1000, green for 2.5G.
- The reset hole is on the back next to the power input.

### Label specifications

- **Name**: 
- **Ports**:
  - 5 × RJ45: 10/100/1000/2500 Mbps
  - 1 × SFP+: 1000 / 2500 / 10000 Mbps

<!-- <img src="" width="300" /> -->

### What works
The device is fully supported:
- ALL 2.5GBASE-T RJ45 ports work at 10/100/1000/2500 Mbps
- The SFP+ port supports 1G, 2.5G and 10G modules 
- LEDs work with the same indiciations as the OEM firmware

### PCB overview

**Board markings**
- Top silkscreen: PCB-SWTG024AS-A-V2.0.1_19650

Top side

<!-- <img src="" width="300" /> -->

Bottom

<!-- <img src="" width="300" /> -->

### J1, serial console

| `J1` pin | Signal      |
| -------- | ----------- |
| 1        | 3V3         |
| 2        | GND         |
| 3        | RX (Input)  |
| 4        | TX (Output) |
