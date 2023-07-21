// node.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

#include <lwip/def.h> // htonl()

extern DmxClass   *dmx;
extern RepoClass  *repo;
extern GOsetClass *theGOset;


#define NODE_ROUND_LEN   7000  // millis, take turns between log entries and chunks
unsigned int node_next_vector; // time when one of the two next vectors should be sent

long packet_proc_time;
int packet_proc_cnt;
long chunk_proc_time;
int chunk_proc_cnt;

void incoming_want_request(unsigned char *buf, int len, unsigned char *aux, struct face_s *f)
{
  struct bipf_s *lptr = bipf_loads(buf + DMX_LEN, len - DMX_LEN);
  if (lptr == NULL || lptr->typ != BIPF_LIST || lptr->cnt < 1 ||
                                                lptr->u.list[0]->typ != BIPF_INT) {
      Serial.printf("   =W formatting error\r\n");
      return;
  }
  struct bipf_s **lst = lptr->u.list;
  int offs = lst[0]->u.i;
  int credit = 3;
  // we should send log entries from different feeds, if possible.
  // otherwise a lost tSSB packet will render the subsequent packets useless
  // wherefore we create a frontier copy
  short* seq_copy = (short*) calloc(lptr->cnt, sizeof(short));
  for (int i = 1; i < lptr->cnt; i++)
    if (lst[i]->typ == BIPF_INT)
      seq_copy[i] = lst[i]->u.i;
  char found_something = 1;
  while (credit > 0 && found_something) {
    found_something = 0;
    for (int i = 1; credit > 0 && i < lptr->cnt; i++) {
      if (seq_copy[i] == 0) continue;
      int fNDX = (offs + i-1) % theGOset->goset_len;
      unsigned char *fid = theGOset->get_key(fNDX);
      int seq = seq_copy[i];
      fishForNewLoRaPkt();
      unsigned char *pkt = repo->feed_read(fid, seq);
      if (pkt == NULL)
        continue;
      Serial.printf("  repo.read(%d.%d) -> %s..\r\n", fNDX, seq, to_hex(pkt, 8));
      io_enqueue(pkt, TINYSSB_PKT_LEN);
      found_something++;
      seq_copy[i]++;
      credit--;
    }
  }
  String v = "[";
  for (int i = 1; i < lptr->cnt; i++) {
    if (lst[i]->typ != BIPF_INT)
      continue;
    int fNDX = (offs + i-1) % theGOset->goset_len;
    int seq = lst[i]->u.i;
    v += " " + String(fNDX) + "." + String(seq);
    while (seq_copy[i]-- > seq)
      v += "*";
  }
  Serial.println("   =W " + v + " ]");
  bipf_free(lptr);
  free(seq_copy);
  return;
}

void incoming_chnk_request(unsigned char *buf, int len, unsigned char *aux, struct face_s *f)
{
  struct bipf_s *lptr = bipf_loads(buf + DMX_LEN, len - DMX_LEN);
  if (lptr == NULL || lptr->typ != BIPF_LIST) {
    Serial.printf("   =C formatting error\r\n");
    return;
  }
  int err_cnt = 0;
  struct bipf_s **slpptr = lptr->u.list;
  // FIXME: should assemble sequence of chunks from different sidechains, if possible.
  // otherwise a lost tSSB packet will render the subsequent chained packets useless
  short* cnr_copy = (short*) malloc(lptr->cnt * sizeof(short));
  for (int i = 0; i < lptr->cnt; i++, slpptr++) {
    cnr_copy[i] = -1;
    if ((*slpptr)->typ == BIPF_LIST && (*slpptr)->cnt >= 3 &&
        (*slpptr)->u.list[2]->typ == BIPF_INT)
      cnr_copy[i] = (*slpptr)->u.list[2]->u.i;
  }
  int credit = 3;
  char found_something = 1;
  while (credit > 0 && found_something) {
    found_something = 0;
    slpptr = lptr->u.list;
    for (int i = 0; i < lptr->cnt; i++, slpptr++) {
      if ((*slpptr)->typ != BIPF_LIST || (*slpptr)->cnt < 3) {
        err_cnt++;
        continue;
      }
      struct bipf_s **lst = (*slpptr)->u.list;
      if (lst[0]->typ != BIPF_INT || lst[1]->typ != BIPF_INT || lst[2]->typ != BIPF_INT) {
        err_cnt++;
        continue;
      }
        // ||
        //         (*slpptr)->u.list[0]->typ != BIPF_INT || (*slpptr)->u.list[1]->typ != BIPF_INT ||
        //         (*slpptr)->u.list[2]->typ != BIPF_INT)
        // ||
        //         (*slpptr)->u.list[0]->typ != BIPF_INT || (*slpptr)->u.list[1]->typ != BIPF_INT ||
        //         (*slpptr)->u.list[2]->typ != BIPF_INT)
    // int fNDX = (*slpptr)->u.list[0]->u.i;
    // int seq = (*slpptr)->u.list[1]->u.i;
    // int cnr = (*slpptr)->u.list[2]->u.i; // chunk nr
      int fNDX = lst[0]->u.i;
      int seq = lst[1]->u.i;
      int cnr = cnr_copy[i]; // chunk nr
      // v += (v.length() == 0 ? "[ " : " ") + String(fNDX) + "." + String(seq) + "." + String(cnr);
    // Serial.printf(" %d.%d.%d", fNDX, seq, cnr);
      unsigned char *fid = theGOset->get_key(fNDX);
      unsigned char *pkt = repo->feed_read(fid, seq);
      if (pkt == NULL || pkt[DMX_LEN] != PKTTYPE_chain20) continue;
      int szlen = 4;
      int sz = bipf_varint_decode(pkt, DMX_LEN + 1, &szlen);
      if (sz <= 48-szlen-HASH_LEN) {
        Serial.printf("   -- no side chain for %d.%d? sz=%d\r\n", fNDX, seq, sz);
        Serial.printf("      content: %s\r\n", to_hex(pkt+DMX_LEN+1+szlen, sz));
        continue;
      }
      int max_chunks = (sz - (48 - szlen) + 99) / 100;
      if (cnr > max_chunks) {
        // Serial.printf("   -- chunk nr > maxchunks (%d.%d.%d > %d)\r\n", fNDX, seq, cnr, max_chunks);
        continue;
      }
      fishForNewLoRaPkt();
      unsigned char *chunk = repo->feed_read_chunk(fid, seq, cnr);

      if (chunk == NULL) { // missing content
        // Serial.printf("   -- cannot load chunk %d.%d.%d\r\n", fNDX, seq, cnr);
        continue;
      }
      //Serial.printf("   have chunk %d.%d.%d\r\n", fNDX, seq, cnr);
      io_enqueue(chunk, TINYSSB_PKT_LEN, NULL, f);
      found_something++;
      cnr_copy[i]++; // chunk nr
      credit--;
    }
  }
  String v = "[";
  slpptr = lptr->u.list;
  for (int i = 0; i < lptr->cnt; i++, slpptr++) {
    if ((*slpptr)->typ != BIPF_LIST || (*slpptr)->cnt < 3)
      continue;
    struct bipf_s **lst = (*slpptr)->u.list;
    if (lst[0]->typ != BIPF_INT || lst[1]->typ != BIPF_INT || lst[2]->typ != BIPF_INT)
      continue;
    v += " " + String(lst[0]->u.i) + "." + lst[1]->u.i + "." + lst[2]->u.i;
    while (cnr_copy[i]-- > lst[2]->u.i)
      v += "*";
  }
  Serial.println("   =C " + v + " ]");

  bipf_free(lptr);
  free(cnr_copy);
  if (err_cnt != 0)
    Serial.printf("   =C contains formatting errors\r\n");
  return;
}

void incoming_pkt(unsigned char *buf, int len, unsigned char *fid, struct face_s *f)
{
  Serial.println("   incoming log entry");
  if (len != TINYSSB_PKT_LEN) return;
  unsigned long now = millis();
  repo->feed_append(fid, buf);
  packet_proc_time += millis() - now;
  packet_proc_cnt++;
}

void incoming_chunk(unsigned char *buf, int len, int blbt_ndx, struct face_s *f)
{
  Serial.println("   incoming chunk");
  if (len != TINYSSB_PKT_LEN) return;
  unsigned long now = millis();
  repo->sidechain_append(buf, blbt_ndx);
  chunk_proc_time += millis() - now;
  chunk_proc_cnt++;
}

void node_tick()
{
  static unsigned char turn; // alternate between requesting log entries an chunks
  static unsigned int log_offs;
  static unsigned int chunk_offs;

  unsigned long now = millis();
  if (now < node_next_vector) // && (node_next_vector-now) < 2*NODE_ROUND_LEN) // FIXME: test whether wraparound works
    return;
  node_next_vector = now + NODE_ROUND_LEN + esp_random() % 500;

  lora_pps = 0.75 * lora_pps + 0.25 * 1000.0 * lora_pkt_cnt / NODE_ROUND_LEN;
  // lora_pps = 1000.0 * lora_pkt_cnt / NODE_ROUND_LEN;
  lora_pkt_cnt = 0;
 
  Serial.printf("-- t=%d.%03d ", now/1000, now%1000);
#if defined(AXP_DEBUG)
  Serial.printf("%1.04gV ", axp.getBattVoltage()/1000);
#endif
  //  Serial.printf("|dmxt|=%d |blbt|=%d |feeds|=%d |entries|=%d |chunks|=%d |freeHeap|=%d pps=%1.2f\r\n",
  //                dmx->dmxt_cnt, dmx->blbt_cnt, repo->feed_cnt, repo->entry_cnt, repo->chunk_cnt, ESP.getFreeHeap(), lora_pps);
  Serial.printf("%dF%dE%dC |dmxt|=%d |blbt|=%d |freeHeap|=%d lora_sent=%d lora_rcvd=%d pps=%1.2f\r\n",
                repo->feed_cnt, repo->entry_cnt, repo->chunk_cnt,
                dmx->dmxt_cnt, dmx->blbt_cnt, ESP.getFreeHeap(),
                lora_sent_pkts, lora_rcvd_pkts, lora_pps);
  if (packet_proc_time != 0 && chunk_proc_time != 0) {
    Serial.printf("   t/packet=%6.2g t/chunk=%6.2g (msec)\r\n",
                  (double)packet_proc_time / packet_proc_cnt,
                  (double)chunk_proc_time / chunk_proc_cnt);
  }

  if (theGOset->goset_len == 0)
    return;
  String v = "";
  struct bipf_s *lptr = bipf_mkList();
  // Serial.printf("turn=%d t=%d.%0d\r\n", turn, now/1000, now%1000);
  turn = 1 - turn;

  if (turn) {
    log_offs = (log_offs+1) % theGOset->goset_len;
    bipf_list_append(lptr, bipf_mkInt(log_offs));
    int encoding_len = bipf_encodingLength(lptr);
    int i;
    for (i = 0; i < theGOset->goset_len; i++) {
      unsigned int ndx = (log_offs + i) % theGOset->goset_len;
      unsigned char *fid = theGOset->get_key(ndx);
      struct feed_s *f = repo->fid2feed(fid);

      // arm DMX
      unsigned char pktID[FID_LEN + 4 + HASH_LEN];
      memcpy(pktID, fid, FID_LEN);
      int s = htonl(f->next_seq); // big endian
      memcpy(pktID + FID_LEN, (unsigned char*) &s, 4);
      memcpy(pktID + FID_LEN + 4, f->prev, HASH_LEN);
      unsigned char dmx_val[DMX_LEN];
      dmx->compute_dmx(dmx_val, pktID, FID_LEN + 4 + HASH_LEN);
      dmx->arm_dmx(dmx_val, incoming_pkt, f->fid, ndx, f->next_seq);
      // Serial.printf("   armed %s for %d.%d\r\n", to_hex(dmx_val, 7),
      //               ndx, f->next_seq);

      // add to want vector
      struct bipf_s *bptr = bipf_mkInt(f->next_seq);
      encoding_len += bipf_encodingLength(bptr);
      bipf_list_append(lptr, bptr);
      v += (v.length() == 0 ? "[ " : " ") + String(ndx) + "." + String(f->next_seq);
      if (encoding_len > 100) {
        i++;
        break;
      }
      fishForNewLoRaPkt();
      io_dequeue();
    }
    // Serial.printf(".. after loop\r\n");
    log_offs = (log_offs+i) % theGOset->goset_len;
    if (lptr->cnt > 1) {
      int sz = bipf_encodingLength(lptr);
      unsigned char buf[sz];
      bipf_encode(buf, lptr);
      io_enqueue(buf, sz, dmx->want_dmx);
      bipf_free(lptr);
      Serial.printf(">> W %s ] %dB\r\n", v.c_str(), sz);
    }
    return;
  }

  // hunt for unfinished sidechains
  // FIXME: must sort for sequence numbers
  chunk_offs = (chunk_offs+1) % theGOset->goset_len;
  int encoding_len = 0;
  int requested_first = -1; // in number of feeds requesting sthg
  for (int i = 0; i < theGOset->goset_len; i++) {
    unsigned int ndx = (chunk_offs + i) % theGOset->goset_len;
    unsigned char *fid = theGOset->get_key(ndx);
    struct feed_s *f = repo->fid2feed(fid);
    char dname[FEED_PATH_SIZE];
    sprintf(dname, "%s/%s", FEED_DIR, to_hex(fid, FID_LEN, 0));
    File fdir = MyFS.open(dname);
    File g = fdir.openNextFile("r");
    short max_chunk_req_per_feed = 2;
    while (g) {
      char *pos = strchr(g.name(), '!');
      if (pos != NULL) { // unfinished chain
        int chunk_cnt;
        unsigned char h[HASH_LEN];
        int seq = atoi(pos+1);
        unsigned char *pkt = repo->feed_read(fid, seq);
        fishForNewLoRaPkt();
        if (pkt != NULL && pkt[DMX_LEN] == PKTTYPE_chain20) {
          // read length of content
          int len = 4; // max length of content length field
          int content_len = bipf_varint_decode(pkt, DMX_LEN+1, &len);
          content_len -= 48 - HASH_LEN - len; // content in the sidechain proper
          chunk_cnt = (content_len + TINYSSB_SCC_LEN - 1) / TINYSSB_SCC_LEN;
          int chainfile_len = g.size();
          if (chunk_cnt <= chainfile_len / TINYSSB_PKT_LEN) { // mv file to a '-NNN' name
            char *fold = repo->_feed_chnk(fid, seq, 1);
            char *fnew = strdup(fold);
            char *pos = strrchr(fnew, '!');
            *pos = '-';
            Serial.printf(" _ rename %s to %s\r\n", fold, fnew);
            g.close();
            MyFS.rename(fold, fnew);
            free(fnew);
            seq = -1;
          } else {
            if (chainfile_len == 0) // fetch first ptr from log entry
              memcpy(h, pkt+DMX_LEN+1+28, HASH_LEN);
            else {
              // Serial.printf("    must fetch latest ptr from chain file\r\n");
              g.seek(chainfile_len - HASH_LEN, SeekSet);
              if (g.read(h, HASH_LEN) != HASH_LEN) {
                // Serial.println("could not read() after seek");
                seq = -1;
              }
            }
          }
        } else
          seq = -1;
        if (seq > 0) {
          int next_chunk = g.size() / TINYSSB_PKT_LEN;
          struct bipf_s *slptr = bipf_mkList();
          bipf_list_append(slptr, bipf_mkInt(ndx));
          bipf_list_append(slptr, bipf_mkInt(seq));
          bipf_list_append(slptr, bipf_mkInt(next_chunk));
          bipf_list_append(lptr, slptr);
          dmx->arm_blb(h, incoming_chunk, fid, seq, next_chunk, chunk_cnt-1);
          // Serial.printf("   armed %s for %d.%d.%d/%d\r\n", to_hex(h, HASH_LEN),
          //               ndx, seq, next_chunk, chunk_cnt-1);
          v += (v.length() == 0 ? "[ " : " ") + String(ndx) + "." + String(seq) + "." + String(next_chunk);
          encoding_len += bipf_encodingLength(slptr);
          if (requested_first == -1)
            requested_first = i;
          max_chunk_req_per_feed--;
        }
      }
      g.close();
      fishForNewLoRaPkt();
      io_dequeue();
      if (encoding_len > 100 || max_chunk_req_per_feed <= 0)
        break;
      g = fdir.openNextFile("r");
    } // while (g)
    fdir.close();
    if (encoding_len > 100)
      break;
  }
  if (requested_first != -1)
    chunk_offs = (chunk_offs+requested_first) % theGOset->goset_len;
  // Serial.printf("chunk_offs ending: %d, requested_first: %d\n",
  //               chunk_offs, requested_first);
  
  if (lptr->cnt > 0) {
    int sz = bipf_encodingLength(lptr);
    unsigned char buf[sz];
    bipf_encode(buf, lptr);
    Serial.printf(">> C %s ] %dB\r\n", v.c_str(), sz);
    io_enqueue(buf, sz, dmx->chnk_dmx);
    bipf_free(lptr);
  }
}

// eof
