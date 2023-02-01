// mgmt.h

unsigned char mgmt_dmx[DMX_LEN];

#define MGMT_ID_LEN  2
#define STATUST_SIZE 42

struct request_s {
  unsigned char typ;
  unsigned char cmd;
  unsigned char id[MGMT_ID_LEN];
  unsigned char dst[MGMT_ID_LEN];
  bool all = false;
};

struct status_s {
  unsigned char typ;
  unsigned char id[MGMT_ID_LEN];
  long voltage;
  int feeds;
  int entries;
  int chunks;
  int free;
  unsigned long int uptime;
};

struct statust_entry_s {
  status_s state;
  unsigned long int received_on;
};

struct beacon_s {
  unsigned char typ;
  unsigned char id[MGMT_ID_LEN];
};

#define MGMT_REQUEST_LEN  sizeof(struct request_s)
#define MGMT_STATUS_LEN   sizeof(struct status_s)
#define MGMT_BEACON_LEN   sizeof(struct beacon_s)


struct statust_entry_s statust[STATUST_SIZE];
int statust_cnt;


// forward declaration
void mgmt_send_status();
void mgmt_send_beacon();

//------------------------------------------------------------------------------

// fill buffer with request packet
unsigned char* _mkRequest(unsigned char cmd, unsigned char* id=NULL)
{
  static struct request_s request;
  request.typ = 'r';
  request.cmd = cmd;
  request.id[0] = my_mac[4];
  request.id[1] = my_mac[5];
  if (id == NULL) {
    request.all = true;
  } else {
    request.dst[0] = id[0];
    request.dst[1] = id[1];
  }
  return (unsigned char*) &request;
}

// fill buffer with status packet
unsigned char* _mkStatus()
{
  static struct status_s status;
  status.typ = 's';
  status.id[0] = my_mac[4];
  status.id[1] = my_mac[5];
  status.voltage = axp.getBattVoltage()/1000;
  status.feeds = feed_cnt;
  status.entries = entry_cnt;
  status.chunks = chunk_cnt;
  int total = MyFS.totalBytes();
  int avail = total - MyFS.usedBytes();
  status.free = avail / (total/100);
  status.uptime = millis();
  return (unsigned char*) &status;
}

// fill buffer with beacon packet
unsigned char* _mkBeacon()
{
  static struct beacon_s beacon;
  beacon.typ = 'b';
  beacon.id[0] = my_mac[4];
  beacon.id[1] = my_mac[5];
  return (unsigned char*) &beacon;
}

//------------------------------------------------------------------------------

// incoming packet with mgmt_dmx
void mgmt_rx(unsigned char *pkt, int len, unsigned char *aux)
{
  pkt += DMX_LEN;
  len -= DMX_LEN;

  // receive beacon
  if (pkt[0] == 'b' && len == MGMT_BEACON_LEN) {
    struct beacon_s *beacon = (struct beacon_s*) calloc(1, MGMT_BEACON_LEN);
    memcpy(beacon, pkt, MGMT_BEACON_LEN);
    Serial.println(String("mgmt_rx received beacon from ") + to_hex(beacon->id, MGMT_ID_LEN, 0));
    return;
  }
  // receive beacon request
  if (pkt[0] == 'r' && pkt[1] == 'b' && len == MGMT_REQUEST_LEN) {
    struct request_s *request = (struct request_s*) calloc(1, MGMT_REQUEST_LEN);
    memcpy(request, pkt, MGMT_REQUEST_LEN);
    Serial.println(String("mgmt_rx received beacon request from ") + to_hex(request->id, MGMT_ID_LEN, 0));
    if (request->all == true) {
      Serial.println("sending beacon ...");
      mgmt_send_beacon();
    } else {
      Serial.println(String("mgmt_rx received beacon request for ") + to_hex(request->dst, MGMT_ID_LEN, 0));
      for (int i = 0; i < MGMT_ID_LEN; i++) {
        if (request->dst[i] != my_mac[6 - MGMT_ID_LEN + i]) {
	  return;
	}
      }
      Serial.println("sending beacon ...");
      mgmt_send_beacon();
    }
    return;
  }
  // receive status request
  if (pkt[0] == 'r' && pkt[1] == 's' && len == MGMT_REQUEST_LEN) {
    struct request_s *request = (struct request_s*) calloc(1, MGMT_REQUEST_LEN);
    memcpy(request, pkt, MGMT_REQUEST_LEN);
    Serial.println(String("mgmt_rx received status request from ") + to_hex(request->id, MGMT_ID_LEN, 0));
    mgmt_send_status();
    return;
  }
  // receive status response
  if (pkt[0] == 's' && len == MGMT_STATUS_LEN) {
    struct status_s *status = (struct status_s*) calloc(1, MGMT_STATUS_LEN);
    memcpy(status, pkt, MGMT_STATUS_LEN);
    unsigned long int received_on = millis();
    Serial.println(String("mgmt_rx received status response from ") + to_hex(status->id, 2, 0));
    int ndx = -1;
    for (int i = 0; i < statust_cnt; i++) {
      if (!memcmp(status->id, statust[i].state.id, MGMT_ID_LEN)) {
        ndx = i;
      }
    }
    if (ndx == -1) {
      ndx = statust_cnt++;
    }
    memcpy(statust[ndx].state.id, status->id, MGMT_ID_LEN);
    statust[ndx].state.voltage = status->voltage;
    statust[ndx].state.feeds = status->feeds;
    statust[ndx].state.entries = status->entries;
    statust[ndx].state.chunks = status->chunks;
    statust[ndx].state.free = status->free;
    statust[ndx].state.uptime = status->uptime;
    statust[ndx].received_on = received_on;
    return;
  }
  // receive reboot request
  if (pkt[0] == 'r' && pkt[1] == 'x' && len == MGMT_REQUEST_LEN) {
    struct request_s *request = (struct request_s*) calloc(1, MGMT_REQUEST_LEN);
    memcpy(request, pkt, MGMT_REQUEST_LEN);
    Serial.println(String("mgmt_rx received reboot request from ") + to_hex(request->id, MGMT_ID_LEN, 0));
    if (request->all == true) {
      Serial.println("rebooting ...");
      esp_restart();
    } else {
      Serial.println(String("mgmt_rx received reboot request for ") + to_hex(request->dst, MGMT_ID_LEN, 0));
      for (int i = 0; i < MGMT_ID_LEN; i++) {
        if (request->dst[i] != my_mac[6 - MGMT_ID_LEN + i]) {
	  return;
	}
      }
      Serial.println("rebooting ...");
      esp_restart();
    }
    return;
  }
  // unknown typ
  Serial.printf("mgmt_rx t=%c ??\n", pkt[0]);
}

// send status response (sent periodically or after request)
void mgmt_send_status()
{
  unsigned long int now = millis();
  unsigned long int rand = random(1000);
  while (true) { if (millis() - now > rand) { break; } }
  io_enqueue(_mkStatus(), MGMT_STATUS_LEN, mgmt_dmx, NULL);
}

// print status table entry
void _print_status(status_s* status, unsigned long int received_on = NULL)
{
  // id
  Serial.printf("  %s", to_hex(status->id, MGMT_ID_LEN, 0));
  // voltage
  Serial.printf(" | %6dV", status->voltage);
  // feeds, entries & chunks
  int feeds = status->feeds;
  int entries = status->entries;
  int chunks = status->chunks;
  Serial.printf(" | %5d | %7d | %6d", feeds, entries, chunks);
  // free
  Serial.printf(" | %3d%%", status->free);
  // lastSeen
  int l = received_on == NULL ? 0 : millis() - received_on;
  int ls = (l / 1000) % 60;
  int lm = (l / 1000 / 60) % 60;
  int lh = (l / 1000 / 60 / 60) % 24;
  Serial.printf(" | %02d:%02d:%02d", lh, lm ,ls);
  // uptime
  int u = status->uptime;
  int us = (u / 1000) % 60;
  int um = (u / 1000 / 60) % 60;
  int uh = (u / 1000 / 60 / 60) % 24;
  int ud = u / 1000 / 60 / 60 / 24;
  Serial.printf(" | %4dd %2dh %2dm %2ds", ud, uh, um ,us);
  // self
  if (received_on == NULL) { Serial.printf(" (self)"); }
  // newline
  Serial.printf("\n");
}

// print the status table
void mgmt_print_statust()
{
  // header
  Serial.println("  id   | battery | feeds | entries | chunks | free | lastSeen | uptime");
  Serial.printf("  ");
  for (int i = 0; i < 4; i++) { Serial.printf("-"); } // id
  for (int i = 0; i < 10; i++) { Serial.printf("-"); } // voltage
  for (int i = 0; i < 27; i++) { Serial.printf("-"); } // FEC
  for (int i = 0; i < 7; i++) { Serial.printf("-"); } // free
  for (int i = 0; i < 11; i++) { Serial.printf("-"); } // lastSeen
  for (int i = 0; i < 20; i++) { Serial.printf("-"); } // uptime
  Serial.printf("\n");

  // self
  struct status_s *own = (struct status_s*) calloc(1, MGMT_STATUS_LEN);
  memcpy(own, _mkStatus(), MGMT_STATUS_LEN);
  _print_status(own);
  free(own);

  // table entries
  for (int i = 0; i < statust_cnt; i++) {
    _print_status(&statust[i].state, statust[i].received_on);
  }
}

// send request to specified node (all if none)
void mgmt_send_request(unsigned char cmd, unsigned char* id=NULL)
{
  io_enqueue(_mkRequest(cmd, id), MGMT_REQUEST_LEN, mgmt_dmx, NULL);
}

// send beacon
void mgmt_send_beacon()
{
  io_enqueue(_mkBeacon(), MGMT_BEACON_LEN, mgmt_dmx, NULL);
}

// eof
