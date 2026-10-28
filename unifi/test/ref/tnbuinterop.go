// TNBU interop helper: "enc" prints frames made by unifi-stubd's encoder,
// "dec" decodes frames (one hex line each) with unifi-stubd's decoder.
package main

import (
	"bufio"
	"encoding/hex"
	"fmt"
	"net"
	"os"
	"strings"

	"github.com/konstruktor1/unifi-stubd/internal/discovery"
	"github.com/konstruktor1/unifi-stubd/internal/inform"
)

func main() {
	key, _ := hex.DecodeString(os.Args[2])
	mac, _ := net.ParseMAC(os.Args[3])
	switch os.Args[1] {
	case "enc":
		for _, gcm := range []bool{false, true} {
			for _, p := range []string{"{}", `{"_type":"noop","interval":10}`, strings.Repeat("x", 2902)} {
				f, err := inform.EncodeJSON(mac, key, []byte(p), inform.Options{GCM: gcm})
				if err != nil {
					panic(err)
				}
				fmt.Printf("%t %s %s\n", gcm, hex.EncodeToString([]byte(p)), hex.EncodeToString(f))
			}
		}
	case "disc":
		// Fixed announcement matching test_agent.c's discovery check
		a := discovery.Announcement{MAC: mac, IP: net.IPv4(192, 0, 2, 20), Model: "USWED35", Version: "2.1.8.971",
			Hostname: "hc-swtgw215as", Default: true, Uptime: 1234, Sequence: 7}
		b, err := a.MarshalBinary()
		if err != nil {
			panic(err)
		}
		fmt.Println(hex.EncodeToString(b))
	case "dec":
		sc := bufio.NewScanner(os.Stdin)
		sc.Buffer(make([]byte, 1<<20), 1<<20)
		for sc.Scan() {
			f, _ := hex.DecodeString(strings.TrimSpace(sc.Text()))
			pkt, body, err := inform.Decode(f, key)
			if err != nil {
				fmt.Println("ERR", err)
				continue
			}
			fmt.Printf("%04x %s\n", pkt.Flags, hex.EncodeToString(body))
		}
	}
}
