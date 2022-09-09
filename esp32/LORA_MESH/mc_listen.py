#!/usr/bin/env python3

# 2022-08-12 <christian.tschudin@unibas.ch>

import os
import sys
import _thread

import socket
def mk_addr(ip,port):
    return (ip,port)

print("  creating face for UDP multicast group")
snd_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
snd_sock.bind(mk_addr('0.0.0.0',0))
snd_sock.setsockopt(socket.IPPROTO_IP,
                                     socket.IP_MULTICAST_TTL, 2)
snd_sock.setsockopt(socket.SOL_IP,
                                     socket.IP_MULTICAST_IF,
                                     bytes(4))
snd_addr = snd_sock.getsockname()

mc_addr = ("239.5.5.8", 1558)
rcv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
rcv_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
rcv_sock.bind(mk_addr(*mc_addr))
mreq =  bytes([int(i) for i in mc_addr[0].split('.')]) + bytes(4)
rcv_sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
print("recv address is", mc_addr)

# repeat sending a packet to the mc group until we receive one
# that matches what we sent - for learning the outgoing interface
n = os.urandom(8)
# while True:
for i in range(100):
    snd_sock.sendto(n, mc_addr)
    pkt,src = rcv_sock.recvfrom(8)
    if pkt == n:
        snd_addr = src
        break
print("send address is", snd_addr)

while True:
    print("loop")
    r = rcv_sock.recvfrom(1000)
    if r == None or r[1] == snd_addr:
        continue
    print(len(r[0]), r[0].hex(), r[1])
    snd_sock.sendto(b'abcdefghijklmnopqrstuvwxyz', mc_addr)
# eof
