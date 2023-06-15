// io.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

/*
   tinySSB packet format:
   dmx(7B) + more_data(var) + CRC32(4B)
*/

#define IO_MAX_QUEUE_LEN          6
#define LORA_INTERPACKET_TIME  1200  // millis
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
#if defined(MAIN_BLEDevice_H_) && !defined(NO_BLE)
  struct face_s ble_face;
#endif

struct face_s *faces[] = {
  &lora_face,
  &udp_face,
  &bt_face,
#if defined(MAIN_BLEDevice_H_) && !defined(NO_BLE)
  &ble_face
#endif
};

// --------------------------------------------------------------------------------

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

#if defined(MAIN_BLEDevice_H_) && !defined(NO_BLE)

BLECharacteristic *RXChar = nullptr; // receive
BLECharacteristic *TXChar = nullptr; // transmit (notify)
BLECharacteristic *STChar = nullptr; // statistics
int bleDeviceConnected = 0;
char txString[128] = {0};

typedef unsigned char tssb_pkt_t[1+127];
tssb_pkt_t ble_ring_buf[BLE_RING_BUF_SIZE];
int ble_ring_buf_len = 0;
int ble_ring_buf_cur = 0;

unsigned char* ble_fetch_received() // first byte has length, up to 127B
{
  unsigned char *cp;
  if (ble_ring_buf_len == 0)
    return NULL;
  cp = (unsigned char*) (ble_ring_buf + ble_ring_buf_cur);
  noInterrupts();
  ble_ring_buf_cur = (ble_ring_buf_cur + 1) % BLE_RING_BUF_SIZE;
  ble_ring_buf_len--;
  interrupts();
  return cp;
}

class UARTServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      bleDeviceConnected += 1;
      Serial.println("** Device connected");
      // stop advertising when a peer is connected (we can only serve one client)
      if (bleDeviceConnected == 3) { pServer->getAdvertising()->stop(); }
      else { pServer->getAdvertising()->start(); }
    };
    void onDisconnect(BLEServer* pServer) {
      bleDeviceConnected -= 1;
      Serial.println("** Device disconnected");
      // resume advertising when peer disconnects
      pServer->getAdvertising()->start();
    }
};

class RXCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pChar) {
    uint16_t len = pChar->getValue().length();
    Serial.println("RXCallback " + String(len) + " bytes");
    if (len > 0 && len <= 127 && ble_ring_buf_len < BLE_RING_BUF_SIZE) {
      // no CRC check, as none is sent for BLE
      int ndx = (ble_ring_buf_cur + ble_ring_buf_len) % BLE_RING_BUF_SIZE;
      unsigned char *pos = (unsigned char*) (ble_ring_buf + ndx);
      *pos = len;
      memcpy(pos+1, pChar->getData(), len);
      noInterrupts();
      ble_ring_buf_len++;
      interrupts();
    }
  }
};

void ble_init()
{
  // Create the BLE Device
  BLEDevice::init("tinySSB virtual LoRa pub");
  BLEDevice::setMTU(128);
  // Create the BLE Server
  BLEServer *UARTServer = BLEDevice::createServer();
  // UARTServer->setMTU(128);
  UARTServer->setCallbacks(new UARTServerCallbacks());
  // Create the BLE Service
  BLEService *UARTService = UARTServer->createService(BLE_SERVICE_UUID);

  // Create our BLE Characteristics
  TXChar = UARTService->createCharacteristic(BLE_CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  TXChar->addDescriptor(new BLE2902());
  TXChar->setNotifyProperty(true);
  TXChar->setReadProperty(true);

  STChar = UARTService->createCharacteristic(BLE_CHARACTERISTIC_UUID_ST, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  STChar->addDescriptor(new BLE2902());
  STChar->setNotifyProperty(true);
  STChar->setReadProperty(true);

  RXChar = UARTService->createCharacteristic(BLE_CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  RXChar->setCallbacks(new RXCallbacks());

  // Start the service
  UARTService->start();
  // Start advertising
  UARTServer->getAdvertising()->start();
  esp_err_t local_mtu_ret = esp_ble_gatt_set_local_mtu(128); // 23);
  if (local_mtu_ret) {
    Serial.println("set local MTU failed, error code = " + String(local_mtu_ret));
  }

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(BLE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
}

#endif // BLE


// --------------------------------------------------------------------------------

void lora_send(unsigned char *buf, short len)
{
#if !defined(NO_LORA)
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
#endif
}

void udp_send(unsigned char *buf, short len)
{
#if !defined(NO_WIFI)
  if (udp.beginMulticastPacket()) {
    uint32_t crc = crc32_ieee(buf, len);
    udp.write(buf, len);
    udp.write((unsigned char*) &crc, sizeof(crc));
    udp.endPacket();
    Serial.println("UDP: sent " + String(len + sizeof(crc), DEC) + "B: "
                 + to_hex(buf,8) + ".." + to_hex(buf + len - 6, 6));
  } else
    Serial.println("udp send failed");
#endif
  /*
  if (udp_sock >= 0 && udp_addr_len > 0) {
    if (lwip_sendto(udp_sock, buf, len, 0,
                  (sockaddr*)&udp_addr, udp_addr_len) < 0)
        // err_cnt += 1;
    }
  */
}

#if defined(MAIN_BLEDevice_H_) && !defined(NO_BLE)

void ble_send(unsigned char *buf, short len) {
  if (bleDeviceConnected == 0) return;
  // no CRC added, we rely on BLE's CRC
  TXChar->setValue(buf, len);
  TXChar->notify();
  Serial.printf("BLE: sent %dB: %s..\r\n", len, to_hex(buf,8));
}

void ble_send_stats(unsigned char *str, short len) {
  if (bleDeviceConnected == 0) return;
  // no CRC added, we rely on BLE's CRC
  STChar->setValue(str, len);
  STChar->notify();
  Serial.printf("BLE: sent stat %dB: %s\r\n", len, str);
}

#endif // BLE

void bt_send(unsigned char *buf, short len)
{
#if !defined(NO_BT)
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
#endif
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
#if defined(MAIN_BLEDevice_H_) && !defined(NO_BLE)
  ble_face.name = (char*) "ble";
  ble_face.next_delta = UDP_INTERPACKET_TIME;
  ble_face.send = ble_send;
#endif
  bt_face.name = (char*) "bt";
  bt_face.next_delta = UDP_INTERPACKET_TIME;
  bt_face.send = bt_send;
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
