# Tremola for tinySSB with Voice-over-tinySSB support using Codec2

"Codec 2 is an open source speech codec designed for communications
quality speech between 700 and 3200 bit/s."
[Codec2 homepage](http://rowetel.com/codec2.html)

tinySSB is a new packet format for the append-only logs of SSB.
A log entry fits exactly in one packet of 120B. Payload of a
log entry can be arbitraily large using a side chain (chained
packets of 120B each, called chunks).

This Android app permits to create voice messages that can be
shipped over a tinySSB mesh network, and on the revceiving end
to play back such voice messages.

The Android code for the voice recorder and player view is based on this
[demo AudioRecorder app](https://github.com/exRivalis/AudioRecorder)

---
```<christian.tschudin@unibas.ch>```, Sep 7, 2022
