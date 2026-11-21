// Temporary real WireGuard peer with a userspace netstack. No host TUN or routes.
package main

import (
	"bytes"
	"crypto/ecdh"
	"crypto/ed25519"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"golang.org/x/crypto/ssh"
	"golang.org/x/net/dns/dnsmessage"
	"golang.zx2c4.com/wireguard/conn"
	"golang.zx2c4.com/wireguard/device"
	"golang.zx2c4.com/wireguard/tun"
	"golang.zx2c4.com/wireguard/tun/netstack"
	"io"
	"net"
	"net/netip"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

func serveSSH(listener net.Listener, config *ssh.ServerConfig) {
	for {
		c, e := listener.Accept()
		if e != nil {
			return
		}
		go func(c net.Conn) {
			defer c.Close()
			connection, channels, requests, e := ssh.NewServerConn(c, config)
			if e != nil {
				return
			}
			defer connection.Close()
			go ssh.DiscardRequests(requests)
			for item := range channels {
				if item.ChannelType() != "session" {
					item.Reject(ssh.UnknownChannelType, "session only")
					continue
				}
				channel, reqs, e := item.Accept()
				if e != nil {
					continue
				}
				go func() {
					defer channel.Close()
					for r := range reqs {
						switch r.Type {
						case "pty-req", "window-change", "env":
							r.Reply(true, nil)
						case "shell":
							r.Reply(true, nil)
							channel.Write([]byte("TabRemoto lab shell\r\n"))
							io.Copy(channel, channel)
							return
						case "exec":
							r.Reply(true, nil)
							channel.Write([]byte("TabRemoto lab key authentication OK\n"))
							channel.SendRequest("exit-status", false, ssh.Marshal(struct{ Status uint32 }{0}))
							return
						default:
							r.Reply(false, nil)
						}
					}
				}()
			}
		}(c)
	}
}

type traceTun struct{ tun.Device }

func tracePacket(direction string, p []byte) {
	if len(p) >= 40 && p[0]>>4 == 6 {
		fmt.Printf("IPv6 %s src=%s dst=%s next=%d wire=%d payload=%d\n", direction, netip.AddrFrom16([16]byte(p[8:24])), netip.AddrFrom16([16]byte(p[24:40])), p[6], len(p), int(p[4])*256+int(p[5]))
		if p[6] == 6 && len(p) >= 60 {
			plen := int(p[4])*256 + int(p[5])
			sum := uint32(plen) + 6
			for i := 8; i < 40; i += 2 {
				sum += uint32(binary.BigEndian.Uint16(p[i : i+2]))
			}
			if 40+plen <= len(p) {
				for i := 40; i < 40+plen; i += 2 {
					v := uint32(p[i]) << 8
					if i+1 < 40+plen {
						v += uint32(p[i+1])
					}
					sum += v
				}
			}
			for sum>>16 != 0 {
				sum = (sum & 65535) + (sum >> 16)
			}
			fmt.Printf("TCP checksum %s valid=%t field=%04x\n", direction, sum == 65535, binary.BigEndian.Uint16(p[56:58]))
			fmt.Printf("TCP %s sport=%d dport=%d flags=%02x seq=%d ack=%d\n", direction, binary.BigEndian.Uint16(p[40:42]), binary.BigEndian.Uint16(p[42:44]), p[53], binary.BigEndian.Uint32(p[44:48]), binary.BigEndian.Uint32(p[48:52]))
		}
	}
}
func (t *traceTun) Write(bufs [][]byte, offset int) (int, error) {
	for _, b := range bufs {
		tracePacket("from-Tab5", b[offset:])
	}
	return t.Device.Write(bufs, offset)
}
func (t *traceTun) Read(bufs [][]byte, sizes []int, offset int) (int, error) {
	n, e := t.Device.Read(bufs, sizes, offset)
	for i := 0; i < n; i++ {
		tracePacket("to-Tab5", bufs[i][offset:offset+sizes[i]])
	}
	return n, e
}
func serveDNS(c interface {
	Close() error
	ReadFrom([]byte) (int, net.Addr, error)
	WriteTo([]byte, net.Addr) (int, error)
}, answer4, answer6 string) {
	defer c.Close()
	buf := make([]byte, 1500)
	for {
		n, remote, e := c.ReadFrom(buf)
		if e != nil {
			return
		}
		var message dnsmessage.Message
		if message.Unpack(buf[:n]) != nil || len(message.Questions) != 1 {
			continue
		}
		q := message.Questions[0]
		reply := dnsmessage.Message{Header: dnsmessage.Header{ID: message.Header.ID, Response: true, RecursionAvailable: true}, Questions: message.Questions}
		if strings.EqualFold(q.Name.String(), "shared.test.") || strings.EqualFold(q.Name.String(), "v6only.test.") {
			h := dnsmessage.ResourceHeader{Name: q.Name, Class: dnsmessage.ClassINET, TTL: 30}
			if q.Type == dnsmessage.TypeA && !strings.EqualFold(q.Name.String(), "v6only.test.") {
				h.Type = dnsmessage.TypeA
				reply.Answers = append(reply.Answers, dnsmessage.Resource{Header: h, Body: &dnsmessage.AResource{A: netip.MustParseAddr(answer4).As4()}})
			}
			if q.Type == dnsmessage.TypeAAAA {
				h.Type = dnsmessage.TypeAAAA
				reply.Answers = append(reply.Answers, dnsmessage.Resource{Header: h, Body: &dnsmessage.AAAAResource{AAAA: netip.MustParseAddr(answer6).As16()}})
			}
		} else {
			reply.Header.RCode = dnsmessage.RCodeNameError
		}
		wire, e := reply.Pack()
		if e == nil {
			c.WriteTo(wire, remote)
		}
	}
}
func relayRDP(l net.Listener) {
	for {
		c, e := l.Accept()
		if e != nil {
			return
		}
		go func() {
			defer c.Close()
			target, e := net.Dial("tcp", "192.168.88.102:3389")
			if e != nil {
				return
			}
			defer target.Close()
			go io.Copy(target, c)
			io.Copy(c, target)
		}()
	}
}
func main() {
	if len(os.Args) < 3 || len(os.Args) > 5 {
		panic("output-private-json and physical-IP required")
	}
	index := 0
	if len(os.Args) > 3 {
		var err error
		index, err = strconv.Atoi(os.Args[3])
		if err != nil || index < 0 || index > 3 {
			panic("lab index must be 0..3")
		}
	}
	serverIP := fmt.Sprintf("10.%d.0.1", 87+index)
	clientIP := fmt.Sprintf("10.%d.0.2", 87+index)
	subnet := fmt.Sprintf("10.%d.0.0/24", 87+index)
	advanced := os.Getenv("TABREMOTO_ADVANCED_LAB") == "1"
	server6, client6 := "", ""
	if advanced {
		serverIP = "10.90.0.1"
		clientIP = fmt.Sprintf("10.90.0.%d", 2+index)
		subnet = "10.90.0.0/24"
		server6 = "fd79:90::1"
		client6 = fmt.Sprintf("fd79:90::%d", 2+index)
	}
	udpPort := 51821 + index
	sshPort := 22222 + index
	server, err := ecdh.X25519().GenerateKey(rand.Reader)
	if err != nil {
		panic(err)
	}
	client, err := ecdh.X25519().GenerateKey(rand.Reader)
	if err != nil {
		panic(err)
	}
	ips := []netip.Addr{netip.MustParseAddr(serverIP)}
	answer4, answer6 := "", ""
	if advanced {
		answer4 = fmt.Sprintf("10.90.0.%d", 11+index)
		answer6 = fmt.Sprintf("fd79:90::%d", 11+index)
		ips = append(ips, netip.MustParseAddr(server6), netip.MustParseAddr(answer4), netip.MustParseAddr(answer6))
	}
	tun, stack, err := netstack.CreateNetTUN(ips, nil, 1420)
	if err != nil {
		panic(err)
	}
	dev := device.NewDevice(&traceTun{tun}, conn.NewDefaultBind(), device.NewLogger(device.LogLevelError, "wg-lab: "))
	defer dev.Close()
	config := "private_key=" + hex.EncodeToString(server.Bytes()) + "\nlisten_port=" + strconv.Itoa(udpPort) + "\npublic_key=" + hex.EncodeToString(client.PublicKey().Bytes()) + "\nallowed_ip=" + clientIP + "/32\n"
	if advanced {
		config += "allowed_ip=" + client6 + "/128\n"
	}
	if err = dev.IpcSet(config); err != nil {
		panic(err)
	}
	if err = dev.Up(); err != nil {
		panic(err)
	}
	listener, err := stack.ListenTCP(&net.TCPAddr{IP: net.ParseIP(serverIP), Port: 7007})
	if err != nil {
		panic(err)
	}
	defer listener.Close()
	peer := "[Interface]\nAddress = 10.87.0.2/32\nPrivateKey = " + base64.StdEncoding.EncodeToString(client.Bytes()) + "\nMTU = 1420\n[Peer]\nPublicKey = " + base64.StdEncoding.EncodeToString(server.PublicKey().Bytes()) + "\nEndpoint = " + os.Args[2] + ":51821\nAllowedIPs = 10.87.0.0/24\nPersistentKeepalive = 5\n"
	peer = strings.ReplaceAll(peer, "10.87.0.2", clientIP)
	peer = strings.ReplaceAll(peer, "10.87.0.0/24", subnet)
	peer = strings.ReplaceAll(peer, ":51821", ":"+strconv.Itoa(udpPort))
	if advanced {
		peer = strings.Replace(peer, "Address = "+clientIP+"/32", "Address = "+clientIP+"/32, "+client6+"/128\nDNS = "+serverIP+", "+server6, 1)
		peer = strings.Replace(peer, "AllowedIPs = "+subnet, "AllowedIPs = "+subnet+", fd79:90::/64", 1)
		for _, ip := range []string{serverIP, server6} {
			c, e := stack.ListenUDP(&net.UDPAddr{IP: net.ParseIP(ip), Port: 53})
			if e != nil {
				panic(e)
			}
			go serveDNS(c, answer4, answer6)
		}
		for _, ip := range []string{answer4, answer6} {
			l, e := stack.ListenTCP(&net.TCPAddr{IP: net.ParseIP(ip), Port: 3389})
			if e != nil {
				panic(e)
			}
			go relayRDP(l)
		}
	}
	_, hostKey, _ := ed25519.GenerateKey(rand.Reader)
	signer, _ := ssh.NewSignerFromKey(hostKey)
	_, testKey, _ := ed25519.GenerateKey(rand.Reader)
	testSigner, _ := ssh.NewSignerFromKey(testKey)
	passRaw := make([]byte, 24)
	rand.Read(passRaw)
	password := base64.RawURLEncoding.EncodeToString(passRaw)
	phrase := "LabKey-" + password
	block, _ := ssh.MarshalPrivateKeyWithPassphrase(testKey, "TabRemoto temporary test", []byte(phrase))
	digest := sha256.Sum256(signer.PublicKey().Marshal())
	fingerprint := hex.EncodeToString(digest[:])
	allowed := []ssh.PublicKey{testSigner.PublicKey()}
	if len(os.Args) > 4 {
		data, err := os.ReadFile(os.Args[4])
		if err != nil {
			panic(err)
		}
		for len(data) > 0 {
			key, _, _, rest, err := ssh.ParseAuthorizedKey(data)
			if err != nil {
				panic(err)
			}
			allowed = append(allowed, key)
			data = rest
		}
	}
	auth := &ssh.ServerConfig{PasswordCallback: func(meta ssh.ConnMetadata, p []byte) (*ssh.Permissions, error) {
		if meta.User() == "tabremoto-test" && subtle.ConstantTimeCompare(p, []byte(password)) == 1 {
			return nil, nil
		}
		return nil, fmt.Errorf("denied")
	}, PublicKeyCallback: func(meta ssh.ConnMetadata, p ssh.PublicKey) (*ssh.Permissions, error) {
		if meta.User() == "tabremoto-test" {
			for _, key := range allowed {
				if bytes.Equal(p.Marshal(), key.Marshal()) {
					return nil, nil
				}
			}
		}
		return nil, fmt.Errorf("denied")
	}}
	auth.AddHostKey(signer)
	direct, e := net.Listen("tcp", net.JoinHostPort(os.Args[2], strconv.Itoa(sshPort)))
	if e != nil {
		panic(e)
	}
	defer direct.Close()
	go serveSSH(direct, auth)
	inside, e := stack.ListenTCP(&net.TCPAddr{IP: net.ParseIP(serverIP), Port: 2222})
	if e != nil {
		panic(e)
	}
	defer inside.Close()
	go serveSSH(inside, auth)
	if advanced {
		for _, ip := range []string{answer4, answer6} {
			l, e := stack.ListenTCP(&net.TCPAddr{IP: net.ParseIP(ip), Port: 2222})
			if e != nil {
				panic(e)
			}
			go serveSSH(l, auth)
		}
	}
	data, _ := json.Marshal(map[string]any{"vpn": map[string]string{"op": "vpn", "config": peer, "transport": "wifi"}, "ssh": map[string]any{"op": "ssh", "host": os.Args[2], "port": sshPort, "user": "tabremoto-test", "password": password, "key": string(pem.EncodeToMemory(block)), "passphrase": phrase, "fingerprint": fingerprint, "command": "probe"}})
	if err = os.MkdirAll(filepath.Dir(os.Args[1]), 0700); err != nil {
		panic(err)
	}
	if err = os.WriteFile(os.Args[1], data, 0600); err != nil {
		panic(err)
	}
	fmt.Println("WireGuard userspace lab ready; private configuration saved with mode 0600")
	for {
		conn, err := listener.Accept()
		if err != nil {
			panic(err)
		}
		go func(c net.Conn) { defer c.Close(); io.Copy(c, c) }(conn)
	}
}
