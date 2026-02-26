# RemoteWake

Remotely power on and shut down an Ubuntu desktop over the internet using an ESP32, Wake-on-LAN, and Cloudflare Tunnel — with zero recurring cost.

## Architecture

```
┌──────────┐   HTTPS    ┌────────────────────────┐  Cloudflare   ┌──────────────┐
│  You      │ ────────►  │  Cloudflare Tunnel     │  ──────────►  │ Command      │
│ (anywhere)│            │  (public internet)     │               │ Server       │
└──────────┘            └────────────────────────┘               │ (Laptop)     │
                                                                  │ :3000        │
                                                                  └──────┬───────┘
                                                                         │
                                                          ESP32 polls every 5s
                                                                         │
                                                                  ┌──────▼───────┐
                                                                  │   ESP32      │
                                                                  │  (always on) │
                                                                  └──┬───────┬───┘
                                                        WoL magic    │       │  HTTP POST
                                                        packet (UDP) │       │  /shutdown
                                                                  ┌──▼───────▼───┐
                                                                  │ Ubuntu       │
                                                                  │ Desktop      │
                                                                  │ :8080        │
                                                                  └──────────────┘
```

The system has **three components** running on three separate devices, all on the same local network:

| Component | Device | Directory | Role |
|-----------|--------|-----------|------|
| Command Server | Laptop (or any machine) | `Laptop/` | Queues wake/shutdown requests, exposed to the internet via Cloudflare Tunnel |
| ESP32 Firmware | ESP32 microcontroller | `Esp/` | Polls the server, sends WoL packets to boot the PC, sends HTTP shutdown commands |
| Shutdown Listener | Ubuntu Desktop | `Remote_Desktop/` | Receives shutdown commands from ESP32 and powers off the machine |

## How It Works

### Waking the PC

1. You send a POST request to the command server (reachable via Cloudflare Tunnel).
2. The ESP32 polls `GET /api/wake` every 5 seconds and picks up the queued request.
3. The ESP32 broadcasts a Wake-on-LAN magic packet (UDP port 9) to the desktop's MAC address.
4. The desktop's NIC (in standby) receives the magic packet and powers on the machine.
5. The ESP32 sends an acknowledgment back to the server.

### Shutting Down the PC

1. You send a POST request with `{"type": "shutdown"}` to the command server.
2. The ESP32 picks up the shutdown request and enters **shutdown mode**.
3. The ESP32 sends an HTTP POST to the desktop's shutdown listener (`http://<desktop-ip>:8080/shutdown`).
4. The listener runs `sudo shutdown -h now`.
5. The ESP32 retries the shutdown command every 10 seconds until it receives a new wake request (which exits shutdown mode).

### Handling Power Outages

The desktop's BIOS is configured with **"restore on AC power loss"**, so the machine boots automatically after a power cut. But if the last command was "shutdown," the PC shouldn't stay on.

The ESP32 handles this automatically:
- If the ESP32 is still in **shutdown mode** (i.e., the last command was a shutdown), it keeps retrying the shutdown HTTP request every 10 seconds.
- When power is restored and the desktop boots, the shutdown listener starts via systemd.
- The ESP32's retry hits the now-available endpoint, and the desktop shuts itself down again.

This loop continues until you explicitly send a **wake** request, which clears shutdown mode.

## Setup

### Prerequisites

- An ESP32 dev board
- Ubuntu desktop with Wake-on-LAN enabled in BIOS/UEFI
- A machine to run the command server (your laptop, a VPS, etc.)
- A Cloudflare account (free tier) with a domain for Cloudflare Tunnel
- Arduino IDE or PlatformIO for flashing the ESP32

### 1. Command Server (`Laptop/`)

```bash
cd Laptop
python -m venv venv
source venv/bin/activate
pip install -r requirements.txt
python server.py
```

This starts the FastAPI server on `127.0.0.1:3000`. Expose it to the internet using Cloudflare Tunnel:

```bash
cloudflared tunnel --url http://localhost:3000
```

Or configure a persistent named tunnel pointing to `http://localhost:3000` on your domain.

### 2. ESP32 Firmware (`Esp/`)

1. Copy `secrets.h` and fill in your actual values:

```cpp
#define WIFI_SSID "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"

#define SERVER_URL "https://your-tunnel-domain.com/api/wake"

#define TARGET_MAC "AA:BB:CC:DD:EE:FF"        // Ubuntu PC's MAC address
#define BROADCAST_IP 192, 168, 0, 255          // Your LAN's broadcast address
#define PC_SHUTDOWN_URL "http://192.168.0.109:8080/shutdown"  // Desktop's local IP
```

2. Install the required Arduino libraries:
   - **ArduinoJson** (by Benoit Blanchon)
   - Built-in ESP32 libraries: `WiFi`, `HTTPClient`, `WiFiUdp`

3. Flash `main.cpp` to the ESP32 using Arduino IDE or PlatformIO.

### 3. Shutdown Listener (`Remote_Desktop/`)

On the Ubuntu desktop:

```bash
pip install fastapi uvicorn
```

Allow passwordless shutdown for your user:

```bash
sudo visudo
# Add this line:
# yourusername ALL=(ALL) NOPASSWD: /sbin/shutdown
```

Run the listener (or set it up as a systemd service):

```bash
python3 Shutdown_listener.py
```

#### systemd Service (recommended)

Create `/etc/systemd/system/shutdown-listener.service`:

```ini
[Unit]
Description=PC Shutdown Listener
After=network.target

[Service]
ExecStart=/usr/bin/python3 /path/to/Remote_Desktop/Shutdown_listener.py
Restart=always
User=yourusername

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl enable shutdown-listener
sudo systemctl start shutdown-listener
```

### 4. Ubuntu Desktop BIOS

Enable **"Restore on AC Power Loss"** (or similar) in your BIOS/UEFI settings so the machine boots automatically after a power outage.

## API Reference

All endpoints are on the command server.

### `GET /api/wake`

Polled by the ESP32. Returns the next queued request, or `{"wake": false, "shutdown": false}` if the queue is empty.

### `POST /api/wake`

Queue a wake or shutdown request.

```bash
# Wake the PC
curl -X POST https://your-tunnel-domain.com/api/wake \
  -H "Content-Type: application/json" \
  -d '{"type": "wake"}'

# Shut down the PC
curl -X POST https://your-tunnel-domain.com/api/wake \
  -H "Content-Type: application/json" \
  -d '{"type": "shutdown"}'
```

### `POST /api/wake/ack`

Called by the ESP32 to acknowledge it processed a request. Body: `{"id": "<request_id>", "status": "sent"}`.

### `GET /api/status`

Returns server status and pending request queue.

### `GET /` 

Health check. Returns server info and pending request count.

## Notes

- The command server stores requests **in memory** — restarting it clears the queue. Stale requests older than 5 minutes are automatically cleaned up.
- The ESP32 deduplicates requests by ID, so it won't process the same request twice.
- Wake-on-LAN must be enabled in both the BIOS and the OS. On Ubuntu, you may need to enable it with `ethtool`:
  ```bash
  sudo ethtool -s <interface> wol g
  ```
- The command server has no authentication. If you expose it publicly, consider adding an API key or other auth mechanism.
