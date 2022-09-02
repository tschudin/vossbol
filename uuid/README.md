# Assigned tinySSB UUIDs

as of Sep 2022



## Assigned UUIDs

| UUID | name | usage | comment |
+------+------+-------+---------+
| 00000000-7646-4b5b-9a50-71becce51558 | nz.scuttlebutt.tinyssb | base UUDI for tinySSB | |
| 6e400001-7646-4b5b-9a50-71becce51558 | nz.scuttlebutt.tinyssb.service.trs | tinySSB BLE replication service, v2022 | |
| 6e400002-7646-4b5b-9a50-71becce51558 | nz.scuttlebutt.tinyssb.characteristic.rx | ", RX characteristic | write towards periph, MTU=128B |
| 6e400003-7646-4b5b-9a50-71becce51558 | nz.scuttlebutt.tinyssb.characteristic.tx | ", TX characteristic | notif from periph, MTU=128B |


## raw output of generator, ending in "1558" (similar to "tSSB")

```
% ./pick_uuid_for_ble.py
Base UUID candidate for tinySSB
  0000000076464b5b9a5071becce51558
  00000000-7646-4b5b-9a50-71becce51558
  urn:uuid:00000000-7646-4b5b-9a50-71becce51558
  variant='specified in RFC 4122', version=4
```

---
