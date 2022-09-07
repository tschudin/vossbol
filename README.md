# vossbol - a demo of voice-over-tinySSB-over-LoRa

August 2022

This repo contains a demo for compressed voice carried over a Secure
Scuttlebutt-like replication fabric (off-line first i.e, it tolerates
intermittent connectivity) using LoRa, or long range radio, as
physical layer.

The tinySSB layer provides a combination of self-configuration (new
users are automatically onboarded), reliable broadcast and a
persistency service in an integrated way. Packets are 120 Bytes long.


## Technology stack included in this repo

- Android app (Tremola) with Codec2 voice compression
- tinySSB packet format, append-only log and replication protocol
- LoRa using the "T-Beam" ESP32 device

```
Smartphone1                             Smartphone2          Smartphone3
     \                                       |                  /
    WiFi                                    WiFi             WiFi
       \                                     |                /
     T-Beam ...LoRa... T-Beam ...LoRa... T-Beam ...LoRa... T-Beam
        %                 %                %                %
    replicas          replicas          replicas         replicas (on Flash)
```

## Software

- directory esp32 (currently for T-Beam)
- directory android (tinyTremola Android app)
- directory py (command line scripts to interact with the mesh)

---
