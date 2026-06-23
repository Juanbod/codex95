# Codex95 Link

Codex95 Link is a small Ethernet-to-Wi-Fi companion device for Windows 95
computers. It lets a Toshiba Libretto use the Codex95 bridge and optionally
reach the internet without adding Wi-Fi drivers to Windows 95.

```text
Toshiba Libretto
  PCMCIA Ethernet
    |
    | 10BASE-T Ethernet
    v
Codex95 Link device
  Ethernet DHCP/NAT + TCP proxy + Wi-Fi client
    |
    | Wi-Fi
    v
Modern computer running Codex95 bridge + internet
```

## Goals

- Present a simple wired Ethernet network to Windows 95.
- Connect to a modern Wi-Fi network as a client.
- Give the Libretto an IP address by DHCP, or support a static fallback.
- Let the Libretto use `http://192.168.8.1:8787` for Codex95 bridge traffic.
- Forward Codex95 TCP traffic to the real bridge on the modern computer.
- Optionally provide NAT internet access for old browsers, FTP clients, and
  installers.
- Avoid any OpenAI API key or secret storage on the Windows 95 computer.

## Recommended Network

Ethernet side, between Libretto and Codex95 Link:

```text
Codex95 Link Ethernet IP: 192.168.8.1
DHCP range:               192.168.8.20 - 192.168.8.80
Subnet mask:              255.255.255.0
Gateway/DNS for Libretto: 192.168.8.1
Codex95 bridge address:   192.168.8.1:8787
```

Wi-Fi side:

```text
Codex95 Link joins the normal home Wi-Fi.
Modern bridge PC stays on that Wi-Fi or wired LAN.
Codex95 Link forwards traffic to the bridge PC, for example 192.168.1.99:8787.
```

## Required Features

### 1. Ethernet DHCP Server

The device should give Windows 95 a working network configuration
automatically:

```text
IP:      192.168.8.x
Mask:    255.255.255.0
Gateway: 192.168.8.1
DNS:     192.168.8.1
```

Manual static configuration should also work:

```text
IP:      192.168.8.2
Mask:    255.255.255.0
Gateway: 192.168.8.1
DNS:     192.168.8.1
```

### 2. Codex95 TCP Proxy

The simplest reliable Codex95 path is a local proxy:

```text
Libretto -> 192.168.8.1:8787 -> Codex95 Link -> bridge PC:8787
```

The Libretto does not need to know the bridge PC address. In Codex95 Settings,
use:

```text
192.168.8.1:8787
```

### 3. Codex95 Discovery Helper

Codex95 normally discovers the bridge by UDP broadcast on port `8788`.
Broadcasts do not pass through NAT reliably, so Codex95 Link should answer
discovery itself on the Ethernet side:

```text
Request:  CODEX95_DISCOVER
Reply:    CODEX95_BRIDGE 8787
```

After that, the client connects to Codex95 Link, and the TCP proxy forwards the
connection to the real bridge PC.

### 4. NAT Internet Access

For general internet access, the device should route:

```text
Libretto Ethernet network -> Wi-Fi network -> internet
```

This is useful for:

- old FTP/HTTP downloads;
- local package mirrors;
- testing old browsers;
- fetching files from the modern computer.

Modern HTTPS sites usually will not work directly in Windows 95 browsers. For
that, use a modern bridge/proxy on the newer computer.

### 5. Web Configuration Page

The device should expose a tiny setup page on the Ethernet side:

```text
http://192.168.8.1/
```

Minimum settings:

- Wi-Fi SSID
- Wi-Fi password
- bridge PC address, such as `192.168.1.99`
- bridge port, usually `8787`
- DHCP on/off
- NAT on/off

Nice-to-have settings:

- scan Wi-Fi networks;
- bridge health check;
- save/restore config;
- firmware update;
- reset button behavior.

## Hardware Options

### Option A: Tiny OpenWrt Router

This is the most practical first version.

Examples:

- GL.iNet Mango / Shadow / Opal class devices
- tiny MT7628/MT7688 OpenWrt modules
- repackaged travel router board

Pros:

- real Linux networking;
- DHCP, DNS, NAT, firewall, and Wi-Fi client are already solved;
- TCP proxy and discovery helper are easy scripts/services;
- reliable enough for daily use.

Cons:

- bigger than a bare ESP32 board;
- needs more power;
- less romantic than a custom microcontroller build.

### Option B: ESP32 + W5500 Ethernet

This is the compact custom-board path.

Pros:

- very small;
- cheap;
- can be shaped into a compact dongle;
- enough speed for Codex95 chat and small file transfers.

Cons:

- true Ethernet-to-Wi-Fi NAT is more work;
- fewer mature routing tools than OpenWrt;
- custom firmware needs careful testing;
- HTTPS and large downloads should stay on the modern bridge PC.

Recommended ESP32 feature set:

- Wi-Fi station mode;
- W5500 SPI Ethernet facing the Libretto;
- DHCP server on Ethernet;
- TCP proxy for port `8787`;
- UDP discovery responder on `8788`;
- optional NAT for HTTP/FTP/basic internet.

## MVP Plan

1. Build an OpenWrt prototype first.
2. Configure Wi-Fi client mode.
3. Configure Ethernet as `192.168.8.1/24`.
4. Enable DHCP on Ethernet.
5. Enable NAT from Ethernet to Wi-Fi.
6. Add a TCP proxy:

   ```text
   0.0.0.0:8787 -> bridge-pc.local-or-ip:8787
   ```

7. Add a UDP discovery responder for `CODEX95_DISCOVER`.
8. Test from Libretto:

   ```bat
   ping 192.168.8.1
   ```

   ```text
   http://192.168.8.1:8787/health
   ```

9. In Codex95, set:

   ```text
   192.168.8.1:8787
   ```

10. After the OpenWrt version works, decide whether to shrink it into an ESP32
    custom board.

## Future Nice-To-Haves

- captive setup portal;
- physical button to reset Wi-Fi settings;
- small status LED patterns;
- bridge auto-discovery on the Wi-Fi side;
- bridge health LED;
- optional local file cache for drivers/tools;
- optional HTTP-to-modern-HTTPS fetch helper.

