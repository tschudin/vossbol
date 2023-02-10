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
  bool beacon;
  long voltage;
  int feeds:10;
  int entries:10;
  int chunks:10;
  int free;
  unsigned long int uptime;
  unsigned long int lastSeen;
};

struct statust_entry_s {
  status_s state;
  unsigned long int received_on;
  status_s neighbors[STATUST_SIZE];
  int neighbor_cnt;
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
int statust_rrb;


// forward declaration
void mgmt_send_status();
void mgmt_send_beacon();

bool mgmt_beacon = false;
unsigned long int mgmt_next_send_status;

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
  static struct status_s status[(int) (120 - 11) / MGMT_STATUS_LEN];
  // add self
  status[0].typ = 's';
  status[0].id[0] = my_mac[4];
  status[0].id[1] = my_mac[5];
  status[0].beacon = mgmt_beacon;
  status[0].voltage = axp.getBattVoltage()/1000;
  status[0].feeds = feed_cnt;
  status[0].entries = entry_cnt;
  status[0].chunks = chunk_cnt;
  int total = MyFS.totalBytes();
  int avail = total - MyFS.usedBytes();
  status[0].free = avail / (total/100);
  status[0].uptime = millis();

  // add neighbors
  int maxEntries = (int) (120 - 11) / MGMT_STATUS_LEN;
  int ndxNeighbor;
  for (int i = 1; i < maxEntries; i++) {
    if (i > statust_cnt) { break; }
    ndxNeighbor = statust_rrb++ % statust_cnt;
    status[i].typ = 's';
    status[i].id[0] = statust[ndxNeighbor].state.id[0];
    status[i].id[1] = statust[ndxNeighbor].state.id[1];
    status[i].beacon = statust[ndxNeighbor].state.beacon;
    status[i].voltage = statust[ndxNeighbor].state.voltage;
    status[i].feeds = statust[ndxNeighbor].state.feeds;
    status[i].entries = statust[ndxNeighbor].state.entries;
    status[i].chunks = statust[ndxNeighbor].state.chunks;
    status[i].free = statust[ndxNeighbor].state.free;
    status[i].uptime = statust[ndxNeighbor].state.uptime;
    status[i].lastSeen = millis() - statust[ndxNeighbor].received_on;
  }

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

  Serial.printf("mgmt_rx received packet of len = %d\n", len);

  // receive beacon
  if (pkt[0] == 'b' && len == MGMT_BEACON_LEN) {
    struct beacon_s *beacon = (struct beacon_s*) calloc(1, MGMT_BEACON_LEN);
    memcpy(beacon, pkt, MGMT_BEACON_LEN);
    Serial.println(String("mgmt_rx received beacon from ") + to_hex(beacon->id, MGMT_ID_LEN, 0));
    return;
  }
  // receive beacon request
  if (pkt[0] == 'r' && (pkt[1] == '+' || pkt[1] == '-') && len == MGMT_REQUEST_LEN) {
    struct request_s *request = (struct request_s*) calloc(1, MGMT_REQUEST_LEN);
    memcpy(request, pkt, MGMT_REQUEST_LEN);
    Serial.println(String("mgmt_rx received beacon request from ") + to_hex(request->id, MGMT_ID_LEN, 0));
    if (request->all == false) {
      Serial.println(String("mgmt_rx received beacon request for ") + to_hex(request->dst, MGMT_ID_LEN, 0));
      for (int i = 0; i < MGMT_ID_LEN; i++) {
        if (request->dst[i] != my_mac[6 - MGMT_ID_LEN + i]) {
	  return;
	}
      }
    }
    Serial.printf("turning %s beacon ...\n", pkt[1] == '+' ? "on" : "off");
    mgmt_beacon = pkt[1] == '+' ? true : false;
    //mgmt_send_beacon();
    return;
  }
  // receive status request
  if (pkt[0] == 'r' && pkt[1] == 's' && len == MGMT_REQUEST_LEN) {
    struct request_s *request = (struct request_s*) calloc(1, MGMT_REQUEST_LEN);
    memcpy(request, pkt, MGMT_REQUEST_LEN);
    Serial.println(String("mgmt_rx received status request from ") + to_hex(request->id, MGMT_ID_LEN, 0));
    mgmt_next_send_status = millis() + random(5000);
    return;
  }
  // receive status update
  if (pkt[0] == 's' && len % MGMT_STATUS_LEN == 0) {
    unsigned long int received_on = millis();
    int entries = len / MGMT_STATUS_LEN;
    struct status_s *other = (struct status_s*) calloc(1, MGMT_STATUS_LEN);
    memcpy(other, pkt, MGMT_STATUS_LEN);
    Serial.println(String("mgmt_rx received status update from ") + to_hex(other->id, 2, 0));
    int ndx = -1;
    // check if node is already in table
    for (int i = 0; i < statust_cnt; i++) {
      if (!memcmp(other->id, statust[i].state.id, MGMT_ID_LEN)) {
        ndx = i;
      }
    }
    // new node, check if table not full
    if (ndx == -1) {
      if (statust_cnt < STATUST_SIZE) {
        ndx = statust_cnt++;
      } else {
	Serial.printf("%8sstatus table is full, skipping...\n", "");
	free(other);
	return;
      }
    }
    memcpy(statust[ndx].state.id, other->id, MGMT_ID_LEN);
    statust[ndx].state.beacon = other->beacon;
    statust[ndx].state.voltage = other->voltage;
    statust[ndx].state.feeds = other->feeds;
    statust[ndx].state.entries = other->entries;
    statust[ndx].state.chunks = other->chunks;
    statust[ndx].state.free = other->free;
    statust[ndx].state.uptime = other->uptime;
    statust[ndx].received_on = received_on;

    pkt += MGMT_STATUS_LEN;

    int ndxNeighbor;
    for (int i = 1; i < entries; i++) {
      // no next status
      if (pkt[0] != 's') {
        break;
      }
      struct status_s *neighbor = (struct status_s*) calloc(1, MGMT_STATUS_LEN);
      memcpy(neighbor, pkt, MGMT_STATUS_LEN);
      Serial.printf("%8slearned about %s\n", "", to_hex(neighbor->id, 2, 0));
      pkt += MGMT_STATUS_LEN;
      ndxNeighbor = -1;
      // check if node is already in table
      for (int i = 0; i < statust[ndx].neighbor_cnt; i++) {
        if (!memcmp(neighbor->id, statust[ndx].neighbors[i].id, MGMT_ID_LEN)) {
          ndxNeighbor = i;
        }
      }
      // new node, check if table not full
      if (ndxNeighbor == -1) {
        if (statust[ndx].neighbor_cnt < STATUST_SIZE) {
          ndxNeighbor = statust[ndx].neighbor_cnt++;
	} else {
	  Serial.printf("%8sneighbor table of %s is full, skipping...\n", "", to_hex(other->id, 2, 0));
	  free(neighbor);
	  continue;
	}
      }
      memcpy(statust[ndx].neighbors[ndxNeighbor].id, neighbor->id, MGMT_ID_LEN);
      statust[ndx].neighbors[ndxNeighbor].beacon = neighbor->beacon;
      statust[ndx].neighbors[ndxNeighbor].voltage = neighbor->voltage;
      statust[ndx].neighbors[ndxNeighbor].feeds = neighbor->feeds;
      statust[ndx].neighbors[ndxNeighbor].entries = neighbor->entries;
      statust[ndx].neighbors[ndxNeighbor].chunks = neighbor->chunks;
      statust[ndx].neighbors[ndxNeighbor].free = neighbor->free;
      statust[ndx].neighbors[ndxNeighbor].uptime = neighbor->uptime;
      statust[ndx].neighbors[ndxNeighbor].lastSeen = neighbor->lastSeen;
      free(neighbor);
    }

    free(other);
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

// print status table entry
void _print_status(status_s* status, unsigned long int received_on = NULL, unsigned char* src = NULL)
{
  // id
  Serial.printf("  %s", to_hex(status->id, MGMT_ID_LEN, 0));
  // src
  Serial.printf(" | %s", src == NULL ? "self" : to_hex(src, MGMT_ID_LEN, 0));
  // when was this information received
  int r = received_on == NULL ? 0 : millis() - received_on;
  int rs = (r / 1000) % 60;
  int rm = (r / 1000 / 60) % 60;
  int rh = (r / 1000 / 60 / 60) % 24;
  Serial.printf(" | %02d:%02d:%02d", rh, rm ,rs);
  // when was the node seen for the last time
  int l = received_on == NULL ? 0 : millis() - received_on;
  l += status->lastSeen;
  int ls = (l / 1000) % 60;
  int lm = (l / 1000 / 60) % 60;
  int lh = (l / 1000 / 60 / 60) % 24;
  Serial.printf(" | %02d:%02d:%02d", lh, lm ,ls);
  // beacon
  Serial.printf(" | %6s", status->beacon == true ? "on" : "off");
  // voltage
  Serial.printf(" | %6dV", status->voltage);
  // feeds, entries & chunks
  int feeds = status->feeds;
  int entries = status->entries;
  int chunks = status->chunks;
  Serial.printf(" | %5d | %7d | %6d", feeds, entries, chunks);
  // free
  Serial.printf(" | %3d%%", status->free);
  // what uptime did it report when it was last seen
  int u = status->uptime;
  int us = (u / 1000) % 60;
  int um = (u / 1000 / 60) % 60;
  int uh = (u / 1000 / 60 / 60) % 24;
  int ud = u / 1000 / 60 / 60 / 24;
  Serial.printf(" | %4dd %2dh %2dm %2ds", ud, uh, um ,us);
  // newline
  Serial.printf("\n");
}

// print the status table
void mgmt_print_statust()
{
  // header
  Serial.println("  id   | src  | received | lastSeen | beacon | battery | feeds | entries | chunks | free | uptime");
  Serial.printf("  ");
  for (int i = 0; i < 4; i++) { Serial.printf("-"); } // id
  for (int i = 0; i < 7; i++) { Serial.printf("-"); } // src
  for (int i = 0; i < 11; i++) { Serial.printf("-"); } // received
  for (int i = 0; i < 11; i++) { Serial.printf("-"); } // lastSeen
  for (int i = 0; i < 9; i++) { Serial.printf("-"); } // beacon
  for (int i = 0; i < 10; i++) { Serial.printf("-"); } // voltage
  for (int i = 0; i < 27; i++) { Serial.printf("-"); } // FEC
  for (int i = 0; i < 7; i++) { Serial.printf("-"); } // free
  for (int i = 0; i < 20; i++) { Serial.printf("-"); } // uptime
  Serial.printf("\n");

  // self
  struct status_s *own = (struct status_s*) calloc(1, MGMT_STATUS_LEN);
  memcpy(own, _mkStatus(), MGMT_STATUS_LEN);
  _print_status(own);
  free(own);

  // table entries
  for (int i = 0; i < statust_cnt; i++) {
    _print_status(&statust[i].state, statust[i].received_on, statust[i].state.id);
    // neighbors
    for (int j = 0; j < statust[i].neighbor_cnt; j++) {
      _print_status(&statust[i].neighbors[j], statust[i].received_on, statust[i].state.id);
    }
  }

  // glossary
  Serial.printf("\n");
  Serial.printf(" src:      who said this\n");
  Serial.printf(" received: when was this information received\n");
  Serial.printf(" lastSeen: last time we heard from this node\n");
  Serial.printf(" uptime:   reported uptime when node was last seen\n");
}

// send request to specified node (all if none)
void mgmt_send_request(unsigned char cmd, unsigned char* id=NULL)
{
  io_enqueue(_mkRequest(cmd, id), MGMT_REQUEST_LEN, mgmt_dmx, NULL);
}

// send status response (sent periodically or after request)
void mgmt_send_status()
{
  io_enqueue(_mkStatus(), ((int) ((120 - 11) / MGMT_STATUS_LEN)) * MGMT_STATUS_LEN, mgmt_dmx, NULL);
  mgmt_next_send_status = millis() + MGMT_SEND_STATUS_INTERVAL + random(5000);
}

// send beacon
void mgmt_send_beacon()
{
  io_enqueue(_mkBeacon(), MGMT_BEACON_LEN, mgmt_dmx, NULL);
}

// eof
