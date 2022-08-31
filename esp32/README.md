# README.md for the "ESP32+LoRa tinySSB Relay" Demo

## What you get

Using T-Beam devices interconnected via LoRa, this software implements
a "virtual tinySSB pub". That is, all log content is fully replicated
and stored on the T-Beam devices' flash (1.5MB). One can also say that
"tinySSB _is_ the mesh", the "mesh is a pub", or similar.

The T-Beam devices act as WiFi Access Points so that external non-LoRa
devices can participate in the content replication (produce and
consume log entries). Logically, these external devices behave as full
peers to the T-Beam devices i.e., they use the exactly same
replication protocol as the T-Beam devices among themselves.

Unlike classic SSB, the T-Beam devices do not have an ed25519 ID: they
just accept new feeds and happily replicate these logs among
themselves. Together they look like one single log repository,
regardless from which WiFi point the T-Beam devices are accessed.  In
fact, they implement the idealized "global broadcast abstraction" on
which SSB relies.

```
Smartphone1                             Smartphone2          Smartphone3
     \                                       |                  /
    WiFi                                    WiFi             WiFi
       \                                     |                /
     T-Beam ...LoRa... T-Beam ...LoRa... T-Beam ...LoRa... T-Beam
        %                 %                %                %
    replicas          replicas          replicas         replicas (on Flash)
```

## What you do not get:

This is a demo. Currently it is not sustainable: when the flash is
full, game is over. No management functionality (e.g. remote
exploration of mesh state) is included. A limit of 100 feeds is
hardwired.

The exception to the above statemet is a "zap" functionality where one
can let all T-Beams (that reach each other) "collectively commit
reset" and erase all their content at once. Note, however, that if
just one T-Beam misses the zap signal, and joins again, it will replay
all its content into the mesh. Same goes for the Smartphones which
also could have all the content. That is: content is very sticky.

The demo assumes a trusted environment and does not include the
equivalent functionality of the "secure handshake" to a pub. The
remedy would be to deploy with all T-Beams a symmetric key and to
encrypt traffic, hence have a logically "closed circuit" that includes
the Smartphones.


## How to compile

- start the Arduino IDE
- in the "boards manager", select esp32 by Espressif Systems (we used v1.0.6)
- install the following libraries (tools -> manage libraries)
  ```
  Using library esp8266-oled-ssd1306-master at version 4.3.0
  Using library Wire at version 1.0.1
  Using library LoRa at version 0.8.0
  Using library SPI at version 1.0
  Using library TinyGPSPlus at version 1.0.3
  Using library AXP202X_Library-master at version 1.1.3
  Using library LittleFS_esp32 at version 1.0.6
  Using library FS at version 1.0
  Using library WiFi at version 1.0
  ```
- (FIXME: are special steps needed for libsodium?)
- in tools -> board -> ESP Arduino, select T-Beam
- in tools -> port, choose the appropriate serial device
- change the LoRa frequency to match your country's regulation (```LORA_BAND``` in ```vossbol_tbeam.ino```)
- compile and upload

### FLASH partitions

- one can/could resize the SPIFFS partition, but the default config seems to be fine
- afterwards (but also when using the existing partition for the first time), the partition will be formatted when tinySSB runs for the first time.

### File system layout

- each feed has a directory in the /feeds directory, the feed ID is encoded in hex
- inside the feed directory, the file named ```log``` is the append-only log
which contains the contatenation of the tinySSB packets
- the latest log entry's ID (aka "key", "ref", "message ID", "mid") is stored in a file that starts with ```+``` followed by that latest message's sequence number
- a message's sidechain is stored in a separate file (where all chunks are concatenated):
  - when the file name starts with ```!```, the sidechain is not fully replicated yet
  - when the file name start with a dot, the sidechain is complete
  - on both cases the above prefix is followed by the message's sequence number

Example of two feed directories:
```
21:44:52.613 -> Listing directory: /feeds/becf47c4b78aadf52269c5ddfb6cb83a158b1552857ad24adb2e875452f87628
21:44:52.623 ->   FILE: !1      SIZE: 120 
21:44:52.651 ->   FILE: !4	SIZE: 360
21:44:52.651 ->   FILE: +4	SIZE: 20
21:44:52.689 ->   FILE: log	SIZE: 480
21:44:52.689 -> Listing directory: /feeds/ca84873530ec0fd95f0fec92b50e344a5e0379dcd24cfcf333e287651627a82b
21:44:52.726 ->   FILE: +2	SIZE: 20
21:44:52.726 ->   FILE: .2	SIZE: 1200
21:44:52.770 ->   FILE: log	SIZE: 240

```

Explanation:
- the feed ```0xbecf4...``` has 4 log entries, the hash of the latest one is stored in ```+4```, entries 1 and 4 both have sidechains which are not fully replicated yet
- the feed ```0xca848...``` has two entries in its log where entry 2's sidechain has been fully received (file ```.2```)


## How to run

### T-Beam

The T-Bream only needs power and will then start meshing. If you can
run a serial cable to the device, there are a few commands that permit
limited management. Type the question mark to get the list of commands:

```
22:40:44.902 -> CMD ?
22:40:44.940 ->   ?  help
22:40:44.940 ->   a  add new random key
22:40:44.940 ->   d  dump DMXT and CHKT
22:40:44.940 ->   f  list file system
22:40:44.940 ->   r  reset this repo
22:40:44.940 ->   x  reboot
22:40:44.940 ->   z  zap (reset also remote nodes)
```

Note that command "a" is/was only useful to test the GrowOnlySet functionality
(it creates fake feed IDs which will never have content).

The T-Beam display will, beside the SSID and GPS time if available, show
three coarse metrics:
- F number of feeds
- E number of log entries
- C number of chunks

which permits to quickly assess whether two devices are synced with
high probability (identical F/E/C can occur although content state
diverges).


### You need an external (non-LoRa) device

- You need at least one client device that understands the tinySSB protocol over Wifi. Currently a client can be either a Python implementation of tinySSB on a laptop, or the tinySSB-enabled Android Tremola-App.
- the SSID is different for each T-Beam (to control access, for experiments) with a pattern "tinySSB-XXXX", the password is always "dWeb2022".
- we use UDP multicast at 239.5.5.8/1558

Android has the bad behavior to often _not_ reconnect to a T-Beam
access point after it has been reset. This is probably because Android
remembers that this SSID does not provide Internet access. That is,
Google punishes you for connecting to something that does not sing the
Internet gospel... Remedy: select the SSID by hand, again.


## Wish list of enhancements

- show number of WiFi clients on the T-Beam screen
- add zap command for single feeds
- add FTP to the T-Beam for faster content up/download than tinySSB replication
- add remote status collection command (get each T-Beam's GPS position, key metrics)
- check for memory leaks when using the BIPF functionality
- speed up the replication protocol: push novelty, pipelining, pull-with-credit
- ...

---
