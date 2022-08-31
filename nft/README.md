# vossbol/nft/README.md - this is not a NFT

Here is the first voice message that made it through
the VOSSBOL demo, it reads "August 2022, ..." It is not for sale.

The following files are included (the secret key that was used to
sign the tinySSB packet is ommitted):

- ```ca848735...``` empty file (the file name is the feed ID in hex)
- ```log``` the append-only log of feed ```0xca848...``` with two entries. The second entry contains the text+voice message
- ```mid``` the message IDs for the two log entries
- ```2-sidechain.bin``` sidechain of entry 2: the sequence of 10 chunks containing the text and voice data, including the hash pointers
- ```2-content.bipf``` the raw content of the sidechain, in binary
- ```2-content.txt``` a hex dump of above [BIPF](https://github.com/ssbc/bipf) data
- ```2-voice.c2``` the voice data in Codec2 format
- ```2-voice.wav``` above voice data, converted to WAV for convenience

See [https://github.com/tschudin/Codec2Recorder](https://github.com/tschudin/Codec2Recorder)
for an Android app that can play Codec2 audio and a reference to the codec.

---
