# Message Layout for TinySSB as implemented in TinyTremola - VoSSBoL
et.mettaz@unibas.ch - 05.07.2023

## Introduction

TinySSB is a protocol for append-only-logs to exchange data where a user can write (possibly 
encrypted) data in a personal log in which messages form a cryptographically secured log by adding a
signature and a hash of the previous message as part of the message. For more information on the
exact construction of the tinySSB packet, please refer to
[the TinySSB Repo](https://github.com/cn-uofbasel/tinyssb). This document describes the construction 
of the payload of packets after extraction (which means that the message can come from either a 
plaintext packet or a chain).

## Basic principles

The packet is encoded as a [BIPF](https://github.com/cn-uofbasel/bipf-python) object (namely a Bipf 
list for the top-level object). 

### Top level tags

The top level object can contain one or more pair of field interpreted as a tag and a value (in this
order) corresponding to that tag. The tags are:

- "TAM": Text and media
- "KAN": Used for Kanban events
- "BOX": Encrypted content. When decrypted, the content is again a top level tag with its content

### Text and Media tags

The "Text and Media" value is a dictionary containing up to 4 elements. Note that each can be 
omitted.

- "TIM": unix time in seconds since Jan 1, 1970
- "XRF": list cross referencing messages (potentially from other users)
- "RCP": list of recipients
- "BDY": body of the message containing attachments

### Message Body

The body contains a dictionary of fields. Each field is optional.

- "TXU8": utf-8 encoded text
- "AUC2": codec2 encoded audio file
- "IMJP": Image in JPG format
- "IMPG": Image in PNG format
- "LOGP": GPS location coordinates
- "LOMD": Maidenhead location coodinates