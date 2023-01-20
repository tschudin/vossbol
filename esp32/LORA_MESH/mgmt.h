// mgmt.h

unsigned char mgmt_dmx[DMX_LEN];

#define MGMT_ID_LEN  2
#define STATUST_SIZE 42

struct status_s {
  unsigned char typ;
  unsigned char id[MGMT_ID_LEN];
  long voltage;
  int feeds;
  int entries;
  int chunks;
  int free;
  unsigned long int received_on; // no need to send over network but there is enough space in the packet for now
  unsigned long int uptime;
};

#define STATUS_LEN   sizeof(struct status_s)


struct status_s statust[STATUST_SIZE];
int statust_cnt;


// forward declaration
void whoIsAlive();

//------------------------------------------------------------------------------

// fill buffer with status packet
unsigned char* _mkStatus(unsigned char typ)
{
  struct status_s *status = (struct status_s*) calloc(1, STATUS_LEN);
  status->typ = typ;
  status->id[0] = my_mac[4];
  status->id[1] = my_mac[5];
  status->voltage = axp.getBattVoltage()/1000;
  status->feeds = feed_cnt;
  status->entries = entry_cnt;
  status->chunks = chunk_cnt;
  int total = MyFS.totalBytes();
  int avail = total - MyFS.usedBytes();
  status->free = avail / (total/100);
  status->uptime = millis();
  
  static unsigned char pkt[DMX_LEN + STATUS_LEN];
  memcpy(pkt, mgmt_dmx, DMX_LEN);
  memcpy(pkt + DMX_LEN, status, STATUS_LEN);
  return pkt;
}

//------------------------------------------------------------------------------

// incoming packet with mgmt_dmx
void mgmt_rx(unsigned char *pkt, int len, unsigned char *aux)
{
  pkt += DMX_LEN;
  len -= DMX_LEN;

  // receive status request
  if (pkt[0] == 'q' && len == STATUS_LEN) {
    struct status_s *status = (struct status_s*) calloc(1, STATUS_LEN);
    memcpy(status, pkt, STATUS_LEN);
    Serial.println(String("mgmt_rx received status request from ") + to_hex(status->id, 2, 0));
    io_send(_mkStatus('s'), DMX_LEN + STATUS_LEN, NULL);
    return;
  }
  // receive status response
  if (pkt[0] == 's' && len == STATUS_LEN) {
    struct status_s *status = (struct status_s*) calloc(1, STATUS_LEN);
    memcpy(status, pkt, STATUS_LEN);
    status->received_on = millis();
    Serial.println(String("mgmt_rx received status response from ") + to_hex(status->id, 2, 0));
    int ndx = -1;
    for (int i = 0; i < statust_cnt; i++) {
      if (!memcmp(status->id, statust[i].id, MGMT_ID_LEN)) {
        ndx = i;
      }
    }
    if (ndx == -1) {
      ndx = statust_cnt++;
    }
    memcpy(statust[ndx].id, status->id, MGMT_ID_LEN);
    statust[ndx].voltage = status->voltage;
    statust[ndx].feeds = status->feeds;
    statust[ndx].entries = status->entries;
    statust[ndx].chunks = status->chunks;
    statust[ndx].free = status->free;
    statust[ndx].uptime = status->uptime;
    statust[ndx].received_on = status->received_on;
    return;
  }
  // unknown typ
  Serial.printf("mgmt_rx t=%c ??\n", pkt[0]);
}

// send status request to see what other nodes are out there
void mgmt_status_request()
{
  io_send(_mkStatus('q'), DMX_LEN + STATUS_LEN, NULL);
}

// print the status table
void mgmt_print_statust()
{
  Serial.println("  id   | battery | feeds | entries | chunks | free | lastSeen | uptime");
  Serial.printf("  ");
  for (int i = 0; i < 5; i++) { Serial.printf("-"); } // id
  for (int i = 0; i < 10; i++) { Serial.printf("-"); } // voltage
  for (int i = 0; i < 27; i++) { Serial.printf("-"); } // FEC
  for (int i = 0; i < 7; i++) { Serial.printf("-"); } // free
  for (int i = 0; i < 11; i++) { Serial.printf("-"); } // lastSeen
  for (int i = 0; i < 20; i++) { Serial.printf("-"); } // uptime
  Serial.printf("\n");
  for (int i = 0; i < statust_cnt; i++) {
    // id
    Serial.printf("  %s", to_hex(statust[i].id, MGMT_ID_LEN, 0));
    // voltage
    Serial.printf(" | %6dV", statust[i].voltage);
    // feeds, entries & chunks
    Serial.printf(" | %5d | %7d | %6d", statust[i].feeds, statust[i].entries, statust[i].chunks);
    // MyFS
    Serial.printf(" | %3d%%", statust[i].free);
    // lastSeen
    int l = millis() - statust[i].received_on;
    int ls = (l / 1000) % 60;
    int lm = (l / 1000 / 60) % 60;
    int lh = (l / 1000 / 60 / 60) % 24;
    Serial.printf(" | %02d:%02d:%02d", lh, lm ,ls);
    // uptime
    int u = statust[i].uptime;
    int us = (u / 1000) % 60;
    int um = (u / 1000 / 60) % 60;
    int uh = (u / 1000 / 60 / 60) % 24;
    int ud = u / 1000 / 60 / 60 / 24;
    Serial.printf(" | %4dd %2dh %2dm %2ds", ud, uh, um ,us);
    // newline
    Serial.printf("\n");
  }
}

// eof
