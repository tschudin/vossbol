// mgmt.h

#include <sodium/crypto_auth.h>

#if defined __has_include
#if __has_include ("credentials.h")
#include "credentials.h"
#else
unsigned char MGMT_KEY[crypto_auth_hmacsha512_KEYBYTES] = { 0 };
#endif
#endif


unsigned char mgmt_dmx[DMX_LEN];

#define MGMT_ID_LEN    2
#define STATUST_SIZE   23
#define STATUST_EOL    24 * 60 * 60 * 1000 // millis (24h)
#define MGMT_FCNT_LEN  4
#define MGMT_MIC_LEN   4

#define MGMT_FCNT_LOG_FILENAME        "/mgmt_fcnt_log.bin"
#define MGMT_FCNT_TABLE_LOG_FILENAME  "/mgmt_fcnt_table_log.bin"
#define MGMT_FCNT_TABLE_SIZE          42

struct request_s {
  unsigned char typ;
  unsigned char id[MGMT_ID_LEN];
  unsigned char cmd;
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
  float latitude;
  float longitude;
  int altitude:14;
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

struct fcnt_s {
  unsigned char typ;
  unsigned char id[MGMT_ID_LEN];
  unsigned int fcnt;
};

#define MGMT_REQUEST_LEN  sizeof(struct request_s)
#define MGMT_STATUS_LEN   sizeof(struct status_s)
#define MGMT_BEACON_LEN   sizeof(struct beacon_s)
#define MGMT_FCNT_LEN     sizeof(struct fcnt_s)



struct statust_entry_s statust[STATUST_SIZE];
int statust_cnt;
int statust_rrb;

struct fcnt_s mgmt_fcnt_table[MGMT_FCNT_TABLE_SIZE];
int mgmt_fcnt_table_cnt;

File mgmt_fcnt_log;
File mgmt_fcnt_table_log;

unsigned int mgmt_fcnt = 0;

unsigned char mgmt_id[MGMT_ID_LEN];

bool mgmt_beacon = false;
unsigned long int mgmt_next_send_status = MGMT_SEND_STATUS_INTERVAL;

// forward declaration
void mgmt_send_status();
void mgmt_send_beacon();



//------------------------------------------------------------------------------
// CREATE PAYLOADS

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
  status[0].voltage = 0;
#if defined(AXP_DEBUG)
  status[0].voltage = axp.getBattVoltage()/1000;
#endif
  status[0].feeds = feed_cnt;
  status[0].entries = entry_cnt;
  status[0].chunks = chunk_cnt;
  int total = MyFS.totalBytes();
  int avail = total - MyFS.usedBytes();
  status[0].free = avail / (total/100);
  status[0].uptime = millis();
#if defined(NO_GPS)
  status[0].latitude = 0;
  status[0].longitude = 0;
  status[0].altitude = 0;
#else
  status[0].latitude = gps.location.isValid() ? (float) gps.location.lat() : 0;
  status[0].longitude = gps.location.isValid() ? (float) gps.location.lng() : 0;
  status[0].altitude = gps.location.isValid() ? (float) gps.altitude.meters() : 0;
#endif

  // add neighbors
  int maxEntries = (int) (120 - 11) / MGMT_STATUS_LEN;
  int ndxNeighbor;
  for (int i = 1; i < maxEntries; i++) {
    if (i > statust_cnt) { break; }
    ndxNeighbor = statust_rrb;
    statust_rrb = ++statust_rrb % statust_cnt;
    memcpy(&status[i], &statust[ndxNeighbor].state, sizeof(struct status_s));
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



// -----------------------------------------------------------------------------
// MGMT RX

// receive request
void mgmt_rx_request(unsigned char *pkt, int len)
{
  // beacon
  if (pkt[3] == '+' || pkt[3] == '-') {
    mgmt_beacon = pkt[3] == '+' ? true : false;
    return;
  }
  
  // status
  if (pkt[3] == 's') {
    mgmt_next_send_status = millis() + random(5000);
    return;
  }
  
  // reboot
  if (pkt[3] == 'x') {
    esp_restart();
    return;
  }
  
  // unknown
  Serial.printf("mgmt_rx received request %s ??\r\n", pkt[3]);
}

// receive status
void mgmt_rx_status(unsigned char *pkt, int len) {
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
      Serial.printf("%8sstatus table is full, skipping...\r\n", "");
      free(other);
      return;
    }
  }
  statust[ndx].received_on = received_on;
  memcpy(&statust[ndx].state, other, sizeof(struct status_s));

  pkt += MGMT_STATUS_LEN;

  int ndxNeighbor;
  for (int i = 1; i < entries; i++) {
    // no next status
    if (pkt[0] != 's') {
      break;
    }
    struct status_s *neighbor = (struct status_s*) calloc(1, MGMT_STATUS_LEN);
    memcpy(neighbor, pkt, MGMT_STATUS_LEN);
    Serial.printf("%8slearned about %s\r\n", "", to_hex(neighbor->id, 2, 0));
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
	Serial.printf("%8sneighbor table of %s is full, skipping...\r\n", "", to_hex(other->id, 2, 0));
	free(neighbor);
	continue;
      }
    }
    memcpy(&statust[ndx].neighbors[ndxNeighbor], neighbor, sizeof(struct status_s));
    free(neighbor);
  }

  free(other);
}

// incoming packet with mgmt_dmx
void mgmt_rx(unsigned char *pkt, int len, unsigned char *aux, struct face_s *f)
{
  // check if face == lora
  if (memcmp(f->name, (char *) "lora", 4)) { return; }

  // remove DMX
  pkt += DMX_LEN;
  len -= DMX_LEN;

  // get message integrity code
  len -= MGMT_MIC_LEN;
  unsigned char mic[MGMT_MIC_LEN];
  memcpy(mic, pkt+len, MGMT_MIC_LEN);
  // compute hmac
  unsigned char hash[crypto_auth_hmacsha512_BYTES];
  crypto_auth_hmacsha512(hash, pkt, len, MGMT_KEY);
  // copy mic to hmac
  memcpy(hash, mic, MGMT_MIC_LEN);
  // verify hmac
  if (crypto_auth_hmacsha512_verify(hash, pkt, len, MGMT_KEY) != 0) {
    Serial.printf("mgmt_rx verification of hmac failed\r\n");
    return;
  }
  // get fcnt
  len -= MGMT_FCNT_LEN;
  unsigned int fcnt = (uint32_t)pkt[len+3] << 24 |
                       (uint32_t)pkt[len+2] << 16 |
                       (uint32_t)pkt[len+1] << 8  |
                       (uint32_t)pkt[len];
  // get src id
  unsigned char src[MGMT_ID_LEN];
  memcpy(src, pkt+1, MGMT_ID_LEN);
  Serial.printf("mgmt_rx got packet from %s\r\n", to_hex(src, MGMT_ID_LEN, 0));
  // check if we sent this
  if (!memcmp(src, mgmt_id, MGMT_ID_LEN)) { return; }
  // check if node is already in fcnt table
  int ndx = -1;
  for (int i = 0; i < mgmt_fcnt_table_cnt; i++) {
    if (!memcmp(src, mgmt_fcnt_table[i].id, MGMT_ID_LEN)) {
      ndx = i;
    }
  }
  // new node, check if table not full
  if (ndx == -1) {
    if (mgmt_fcnt_table_cnt < MGMT_FCNT_TABLE_SIZE) {
      ndx = mgmt_fcnt_table_cnt++;
      memcpy(&mgmt_fcnt_table[ndx].id, src, MGMT_ID_LEN);
    } else {
      Serial.printf("mgmt_rx fcnt table is full, skipping...\r\n");
      return;
    }
  }
  // existing node -> validate fcnt
  if (ndx != -1 && fcnt <= mgmt_fcnt_table[ndx].fcnt) { return; }
  // update fcnt
  mgmt_fcnt_table[ndx].fcnt = fcnt;
  // write fcnt-table to file
  mgmt_fcnt_table_log = MyFS.open(MGMT_FCNT_TABLE_LOG_FILENAME, "w");
  mgmt_fcnt_table_log.write((unsigned char*) &mgmt_fcnt_table_cnt, sizeof(mgmt_fcnt_table_cnt));
  mgmt_fcnt_table_log.write((unsigned char*) &mgmt_fcnt_table, MGMT_FCNT_LEN * MGMT_FCNT_TABLE_SIZE);
  mgmt_fcnt_table_log.close();



  // receive beacon
  if (pkt[0] == 'b' && len == MGMT_BEACON_LEN) {
    Serial.println(String("mgmt_rx received beacon from ") + to_hex(src, MGMT_ID_LEN, 0));
    return;
  }

  // receive status
  if (pkt[0] == 's' && len % MGMT_STATUS_LEN == 0) {
    mgmt_rx_status(pkt, len);
    return;
  }

  // receive request
  if (pkt[0] == 'r' && len == MGMT_REQUEST_LEN) {
    struct request_s *request = (struct request_s*) calloc(1, MGMT_REQUEST_LEN);
    memcpy(request, pkt, MGMT_REQUEST_LEN);
    if (memcmp(request->dst, mgmt_id, MGMT_ID_LEN)) {
      io_send(pkt - DMX_LEN, len + DMX_LEN + MGMT_MIC_LEN + MGMT_FCNT_LEN, NULL);
    }
    if (!memcmp(request->dst, mgmt_id, MGMT_ID_LEN) || request->all == true) {
      mgmt_rx_request(pkt, len);
    }
    free(request);
    return;
  }

  // unknown typ
  Serial.printf("mgmt_rx t=%c ??\r\n", pkt[0]);
}



// -----------------------------------------------------------------------------
// STATUS TABLE

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
  // latitude
  Serial.printf(" | %10f", status->latitude);
  // longitude
  Serial.printf(" | %10f", status->longitude);
  // altitude
  Serial.printf(" | %7dm", status->altitude);
  // newline
  Serial.printf("\r\n");
}

// print the status table
void mgmt_print_statust()
{
  // header
  Serial.println("  id   | src  | received | lastSeen | beacon | battery | feeds | entries | chunks | free | uptime            | latitude   | longitude  | altitude");
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
  for (int i = 0; i < 13; i++) { Serial.printf("-"); } // latitude
  for (int i = 0; i < 13; i++) { Serial.printf("-"); } // longitude
  for (int i = 0; i < 11; i++) { Serial.printf("-"); } // altitude
  Serial.printf("\r\n");

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
  Serial.printf("\r\n");
  Serial.printf(" src:      who said this\r\n");
  Serial.printf(" received: when was this information received\r\n");
  Serial.printf(" lastSeen: last time we heard from this node\r\n");
  Serial.printf(" uptime:   reported uptime when node was last seen\r\n");
}

// remove stale entries from status table
void mgmt_statust_housekeeping()
{
  while (true) {
    int old_cnt = statust_cnt;
    for (int i = 0; i < old_cnt; i++) {
      if (millis() - statust[i].received_on > STATUST_EOL) {
        if (i < old_cnt - 1) {
          memcpy(&statust[i], &statust[old_cnt - 1], sizeof(struct statust_entry_s));
	}
	statust[old_cnt - 1] = (const struct statust_entry_s) { 0 };
	statust_cnt--;
	break;
      } else {
        // same for its neighbors
        while (true) {
          int old_neighbor_cnt = statust[i].neighbor_cnt;
          for (int j = 0; j < old_neighbor_cnt; j++) {
            if (millis() - statust[i].received_on + statust[i].neighbors[j].lastSeen > STATUST_EOL) {
              if (i < old_neighbor_cnt - 1) {
                memcpy(&statust[i].neighbors[j], &statust[i].neighbors[old_neighbor_cnt - 1], sizeof(struct status_s));
	      }
	      statust[i].neighbors[old_neighbor_cnt - 1] = (const struct status_s) { 0 };
	      statust[i].neighbor_cnt--;
	      break;
            }
          }
          if (old_neighbor_cnt == statust[i].neighbor_cnt) { break; }
        }
      }
    }
    if (old_cnt == statust_cnt) { break; }
  }
  
}



// -----------------------------------------------------------------------------
// FRAME COUNTER



// -----------------------------------------------------------------------------
// MGMT TX
 
// send mgmt packet
void mgmt_tx(unsigned char* payload, int payload_len)
{
  int len = payload_len + MGMT_FCNT_LEN + MGMT_MIC_LEN;
  // add payload
  unsigned char message[len] = { 0 };
  memcpy(message, payload, payload_len);
  // add fcnt
  memcpy(message + payload_len, (unsigned char*) &++mgmt_fcnt, MGMT_FCNT_LEN);
  // write fcnt to flash
  mgmt_fcnt_log = MyFS.open(MGMT_FCNT_LOG_FILENAME, "w");
  mgmt_fcnt_log.write((unsigned char*) &mgmt_fcnt, sizeof(mgmt_fcnt));
  mgmt_fcnt_log.close();
  // add hmac
  unsigned char hash[crypto_auth_hmacsha512_BYTES];
  crypto_auth_hmacsha512(hash, message, payload_len + MGMT_FCNT_LEN, MGMT_KEY);
  memcpy(message + payload_len + MGMT_FCNT_LEN, hash, MGMT_MIC_LEN);
  // send
  io_enqueue(message, len, mgmt_dmx, NULL);
}

// send beacon
void mgmt_send_beacon()
{
  mgmt_tx(_mkBeacon(), MGMT_BEACON_LEN);
}

// send request to specified node (all if none)
void mgmt_send_request(unsigned char cmd, unsigned char* id=NULL)
{
  mgmt_tx(_mkRequest(cmd,id), MGMT_REQUEST_LEN);
}

// send status response (sent periodically or after request)
void mgmt_send_status()
{
  // housekeeping
  mgmt_statust_housekeeping();
  // send status
  mgmt_tx(_mkStatus(), ((int) ((120 - 11) / MGMT_STATUS_LEN)) * MGMT_STATUS_LEN);
  // set next event
  mgmt_next_send_status = millis() + MGMT_SEND_STATUS_INTERVAL + random(5000);
}

// eof
