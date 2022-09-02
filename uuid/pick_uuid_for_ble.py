#!/usr/bin/env python3

import uuid

u = uuid.uuid4()
base_bytes = bytes(4) + u.bytes[4:]
base_uuid = uuid.UUID(bytes = base_bytes)

print("Base UUID candidate for tinySSB")
print(" ", base_uuid.hex)
print(" ", str(base_uuid))
print(" ", base_uuid.urn)
print(f"  variant='{base_uuid.variant}', version={base_uuid.version}")

'''
Base UUID candidate for tinySSB
  0000000076464b5b9a5071becce51558
  00000000-7646-4b5b-9a50-71becce51558
  urn:uuid:00000000-7646-4b5b-9a50-71becce51558
  variant='specified in RFC 4122', version=4
'''

