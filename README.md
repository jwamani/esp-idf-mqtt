# Minimal ESP-IDF MQTT demo for Wokwi

One ESP32-S3 node that:

- joins the Wokwi virtual Wi-Fi (`Wokwi-GUEST`)
- connects to the free public broker `broker.emqx.io:1883`
- subscribes to `demo/chat`
- publishes a message every 5 seconds
- prints every message it receives

All settings live in `main/app_config.h`.

## Setup

1. Build:

   ```
   idf.py build
   ```

2. Open this folder in VS Code with the Wokwi extension.
3. Press `F1` and run `Wokwi: Start Simulator`.
4. Wokwi uses the free public gateway by default (no `[net]` section in `wokwi.toml`).

## Expected output

```
I (1234) demo: Wi-Fi connected, IP: 10.10.0.2
I (2345) demo: MQTT connected as node-000111
RX demo/chat: node-000111 #1
```

Every node sees its own message and the messages from the other nodes.

## Multiple nodes

Wokwi runs one board per workspace. This project has three simulation
workspaces, all using the same firmware:

| Workspace | Node | macAddress |
|---|---|---|
| `.` | node1 | `24:0a:c4:00:01:11` |
| `sim/node2` | node2 | `24:0a:c4:00:01:12` |
| `sim/master` | master | `24:0a:c4:00:01:13` |

After every build, copy the firmware into the simulation folders:

```
python sync_sims.py
```

Then open each folder as its own VS Code window and run
`Wokwi: Start Simulator` in each. All three connect to `demo/chat` and see each
other's messages.

The MAC address gives each node a unique client ID (`node-xxxxxx`), so the broker
does not disconnect them as duplicates.

## Watch all messages from your PC

```
mosquitto_sub -h broker.emqx.io -t "demo/#" -v
```

## Important

`broker.emqx.io` is a public test broker. Anyone can read the messages. Do not
send secrets. Keep the `demo/` topic prefix or use your own unique prefix.

Do not run this with `idf.py qemu`: QEMU does not emulate Wi-Fi.
