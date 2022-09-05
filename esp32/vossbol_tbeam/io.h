// io.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

/*
   tinySSB packet format:
   dmx(7B) + more_data(var) + CRC32(4B)
*/

#define IO_MAX_QUEUE_LEN         10
#define LORA_INTERPACKET_TIME  1000  // millis
#define UDP_INTERPACKET_TIME    100  // millis
#define NR_OF_FACES               (sizeof(faces) / sizeof(void*))

// unsigned char *io_queue[IO_MAX_QUEUE_LEN]; // ring buffer of |len|...pkt...|
// int io_queue_len = 0, io_offs = 0;
// unsigned long io_next_send = 0; // FIXME: handle wraparound

struct face_s {
  char *name;
  unsigned long next_send;
  unsigned int next_delta;
  unsigned char *queue[IO_MAX_QUEUE_LEN]; // ring buffer of |len|...pkt...|
  int queue_len;
  int offs = 0;
  void (*send)(unsigned char *buf, short len);
};

struct face_s lora_face;
struct face_s udp_face;
struct face_s bt_face;
struct face_s ble_face;
struct face_s *faces[] = { &lora_face, &udp_face, &bt_face, &ble_face };

uint32_t crc32_ieee(unsigned char *pkt, int len) { // Ethernet/ZIP polynomial
  uint32_t crc = 0xffffffffu;
  while (len-- > 0) {
    crc ^= *pkt++;
    for (int i = 0; i < 8; i++)
      crc = crc & 1 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
  }
  return htonl(crc ^ 0xffffffffu);
}

// --------------------------------------------------------------------------------

void lora_send(unsigned char *buf, short len)
{
  if (LoRa.beginPacket()) {
    uint32_t crc = crc32_ieee(buf, len);
    LoRa.write(buf, len);
    LoRa.write((unsigned char*) &crc, sizeof(crc));
    LoRa.endPacket();
    // delicate to invoke to_hex() twice (static buffer!), but it seems to work:
    Serial.println("LoRa: sent " + String(len + sizeof(crc), DEC) + "B: "
                   + to_hex(buf,8) + ".." + to_hex(buf + len - 6, 6));
  } else
    Serial.println("LoRa send failed");
}

void udp_send(unsigned char *buf, short len)
{
  if (udp.beginMulticastPacket()) {
    uint32_t crc = crc32_ieee(buf, len);
    udp.write(buf, len);
    udp.write((unsigned char*) &crc, sizeof(crc));
    udp.endPacket();
    Serial.println("UDP: sent " + String(len + sizeof(crc), DEC) + "B: "
                 + to_hex(buf,8) + ".." + to_hex(buf + len - 6, 6));
  } else
    Serial.println("udp send failed");
  /*
  if (udp_sock >= 0 && udp_addr_len > 0) {
    if (lwip_sendto(udp_sock, buf, len, 0,
                  (sockaddr*)&udp_addr, udp_addr_len) < 0)
        // err_cnt += 1;
    }
  */
}

void bt_send(unsigned char *buf, short len)
{
  if (BT.connected()) {
    uint32_t crc = crc32_ieee(buf, len);
    unsigned char *buf2 = (unsigned char*) malloc(len + sizeof(crc));
    memcpy(buf2, buf, len);
    memcpy(buf2+len, &crc, sizeof(crc));
    kiss_write(BT, buf2, len+sizeof(crc));
    Serial.println("BT: sent " + String(len + sizeof(crc)) + "B: "
            + to_hex(buf2,8) + ".." + to_hex(buf2 + len + sizeof(crc) - 6, 6));

  } // else
    // Serial.println("BT not connected");
}

// --------------------------------------------------------------------------------

void io_init()
{
  lora_face.name = (char*) "lora";
  lora_face.next_delta = LORA_INTERPACKET_TIME;
  lora_face.send = lora_send;  
  udp_face.name = (char*) "udp";
  udp_face.next_delta = UDP_INTERPACKET_TIME;
  udp_face.send = udp_send;
  bt_face.name = (char*) "bt";
  bt_face.next_delta = UDP_INTERPACKET_TIME;
  bt_face.send = bt_send;
  ble_face.name = (char*) "ble";
  ble_face.next_delta = UDP_INTERPACKET_TIME;
  ble_face.send = ble_send;
}

void io_send(unsigned char *buf, short len, struct face_s *f=NULL)
{
  for (int i = 0; i < NR_OF_FACES; i++)
    if (f == NULL || f == faces[i])
      faces[i]->send(buf, len);
}

void io_enqueue(unsigned char *pkt, int len, unsigned char *dmx=NULL, struct face_s *f=NULL)
{
  for (int i = 0; i < NR_OF_FACES; i++)
    if (f == NULL || f == faces[i]) {
      if (faces[i]->queue_len >= IO_MAX_QUEUE_LEN) {
        Serial.println("IO: outgoing queue full " + String(i,DEC));
        continue;
      }
      int sz = len + (dmx ? DMX_LEN : 0);
      unsigned char *buf = (unsigned char*) malloc(1+sz);
      buf[0] = sz;
      if (dmx) {
        memcpy(buf+1, dmx, DMX_LEN);
        memcpy(buf+1+DMX_LEN, pkt, len);
      } else
        memcpy(buf+1, pkt, len);
      // only insert if packet content is not already enqueued:
      int k;
      for (k = 0; k < faces[i]->queue_len; k++)
        if (!memcmp(faces[i]->queue[(faces[i]->offs + k)%IO_MAX_QUEUE_LEN], buf, 1+sz))
          break;
      if (-1 || k == faces[i]->queue_len) {
        faces[i]->queue[(faces[i]->offs + faces[i]->queue_len) % IO_MAX_QUEUE_LEN] = buf;
        faces[i]->queue_len++;
      } else
        free(buf);
    }
}

void io_dequeue() // enforces interpacket time
{
  unsigned long now = millis();
  for (int i = 0; i < NR_OF_FACES; i++) {
    struct face_s *f = faces[i];
    if (f->queue_len <= 0 || now < f->next_send) // FIXME: handle wraparound
      continue;
    f->next_send = now + f->next_delta + esp_random() % (f->next_delta/10);
    unsigned char *buf = f->queue[f->offs];
    io_send(buf+1, *buf, f);
    free(buf);
    f->offs = (f->offs + 1) % IO_MAX_QUEUE_LEN;
    f->queue_len--;
  }
}

int crc_check(unsigned char *pkt, int len) // returns 0 if OK
{
  uint32_t crc = crc32_ieee(pkt, len-sizeof(crc));
  return memcmp(pkt+len-sizeof(crc), (void*)&crc, sizeof(crc));
}

// eof
