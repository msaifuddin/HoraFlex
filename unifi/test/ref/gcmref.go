// Generates AES-GCM (16-byte nonce) and AES-CBC/PKCS#7 vectors with Go's
// crypto/cipher, the implementation unifi-stubd uses against real controllers.
// Output lines: "gcm <key> <iv> <aad> <pt> <ct||tag>" and "cbc <key> <iv> <pt> <ct>".
package main

import (
	"crypto/aes"
	"crypto/cipher"
	"encoding/hex"
	"fmt"
	"math/rand"
)

func bytes(r *rand.Rand, n int) []byte {
	b := make([]byte, n)
	r.Read(b)
	return b
}

func main() {
	r := rand.New(rand.NewSource(8372))
	h := hex.EncodeToString
	for _, n := range []int{0, 1, 15, 16, 17, 31, 32, 100, 255, 256, 1023, 2902, 4000} {
		key, iv, aad, pt := bytes(r, 16), bytes(r, 16), bytes(r, 40), bytes(r, n)
		blk, _ := aes.NewCipher(key)
		g, _ := cipher.NewGCMWithNonceSize(blk, 16)
		fmt.Printf("gcm %s %s %s %s %s\n", h(key), h(iv), h(aad), h(pt), h(g.Seal(nil, iv, pt, aad)))
	}
	for _, n := range []int{0, 1, 15, 16, 17, 100, 2902} {
		key, iv, pt := bytes(r, 16), bytes(r, 16), bytes(r, n)
		pad := 16 - n%16
		in := append(append([]byte{}, pt...), make([]byte, pad)...)
		for i := n; i < len(in); i++ {
			in[i] = byte(pad)
		}
		blk, _ := aes.NewCipher(key)
		ct := make([]byte, len(in))
		cipher.NewCBCEncrypter(blk, iv).CryptBlocks(ct, in)
		fmt.Printf("cbc %s %s %s %s\n", h(key), h(iv), h(pt), h(ct))
	}
}
