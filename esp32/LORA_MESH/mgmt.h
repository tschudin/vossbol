// mgmt.h

unsigned char mgmt_dmx[DMX_LEN];

#define MGMT_ID_LEN  2

struct mgmt_s {
  unsigned char typ;
  unsigned char id[MGMT_ID_LEN];
};

#define MGMT_LEN     sizeof(struct mgmt_s)


// forward declaration
void whoIsAlive();

//------------------------------------------------------------------------------

unsigned char* _mkMgmt(struct mgmt_s *mgmt) // fill buffer with mgmt packet
{
  static unsigned char pkt[DMX_LEN + MGMT_LEN];
  memcpy(pkt, mgmt_dmx, DMX_LEN);
  memcpy(pkt + DMX_LEN, mgmt, MGMT_LEN);
  return pkt;
}


void mgmt_rx(unsigned char *pkt, int len, unsigned char *aux)
{
  pkt += DMX_LEN;
  len -= DMX_LEN;

  if (pkt[0] == 'q' && len == MGMT_LEN) { // request ID
    unsigned char id[MGMT_ID_LEN];
    memcpy(id, pkt+1, 2*sizeof(unsigned char));
    Serial.println(String("mgmt_rx t=q id=") + to_hex(id, 2, 0));
    // add random time delay
    
    //lora_send();
    struct mgmt_s *mgmt = (struct mgmt_s*) calloc(1, MGMT_LEN);
    mgmt->typ = 's'; // response
    mgmt->id[0] = my_mac[4];
    mgmt->id[1] = my_mac[5];
    io_send(_mkMgmt(mgmt), DMX_LEN + MGMT_LEN, NULL);
    return;
  }
  if (pkt[0] == 's' && len == MGMT_LEN) { // respond ID
    unsigned char id[MGMT_ID_LEN];
    memcpy(id, pkt+1, 2*sizeof(unsigned char));
    Serial.println(String("mgmt_rx t=s id=") + to_hex(id, 2, 0));
    return;
  }

  Serial.printf("mgmt_rx t=%c ??\n", pkt[0]);
  Serial.printf("mgmt_rx t=%s ??\n", pkt);
}


void mgmt_whoIsAlive() {
  struct mgmt_s *mgmt = (struct mgmt_s*) calloc(1, MGMT_LEN);
  mgmt->typ = 'q'; // request
  mgmt->id[0] = my_mac[4];
  mgmt->id[1] = my_mac[5];
  io_send(_mkMgmt(mgmt), DMX_LEN + MGMT_LEN, NULL);
}

// eof
