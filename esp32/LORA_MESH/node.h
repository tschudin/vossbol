// node.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>


#define NODE_ROUND_LEN  (8000/2) // millis, take turns between log entries and chunks
unsigned int node_next_vector; // time when one of the two next vectors should be sent

void incoming_want_request(unsigned char *buf, int len, unsigned char *aux, struct face_s *f)
{
  struct bipf_s *lptr = bipf_loads(buf + DMX_LEN, len - DMX_LEN);
  if (lptr == NULL || lptr->typ != BIPF_LIST || lptr->cnt < 1) return;
  struct bipf_s **lst = lptr->u.list;
  if (lst[0]->typ != BIPF_INT) return;
  int offs = lst[0]->u.i;
  String v = "";
  int credit = 3;
  for (int i = 1; i < lptr->cnt; i++) {
    if (lst[i]->typ != BIPF_INT) continue;
    int fNDX = (offs + i-1) % theGOset->goset_len;
    int seq = lst[i]->u.i;
    v += (v.length() == 0 ? "[ " : ", ") + String(fNDX) + "." + String(seq);
    unsigned char *fid = theGOset->goset_keys + fNDX * GOSET_KEY_LEN;
    while (credit > 0) {
      unsigned char *pkt = repo_feed_read(fid, seq);
      if (pkt == NULL) break;
      // Serial.println(String("  have entry ") + to_hex(fid,10) + "." + String(seq));
      v += "*";
      io_enqueue(pkt, TINYSSB_PKT_LEN);
      seq++;
      credit--;
    }
  }
  Serial.println("received log entry request: " + v + " ]");
  bipf_free(lptr);
}

void incoming_chnk_request(unsigned char *buf, int len, unsigned char *aux, struct face_s *f)
{
  struct bipf_s *lptr = bipf_loads(buf + DMX_LEN, len - DMX_LEN);
  if (lptr == NULL || lptr->typ != BIPF_LIST) return;
  String v = "";
  struct bipf_s **slpptr = lptr->u.list;
  int credit = 3;
  for (int i = 0; i < lptr->cnt; i++, slpptr++) {
    if ((*slpptr)->typ != BIPF_LIST || (*slpptr)->cnt < 3 ||
        (*slpptr)->u.list[0]->typ != BIPF_INT || (*slpptr)->u.list[1]->typ != BIPF_INT)
        continue;
    int fNDX = (*slpptr)->u.list[0]->u.i;
    int seq = (*slpptr)->u.list[1]->u.i;
    int cnr = (*slpptr)->u.list[2]->u.i; // chunk nr
    v += (v.length() == 0 ? "[ " : ", ") + String(fNDX) + "." + String(seq) + "." + String(cnr);
    // Serial.printf(" %d.%d.%d", fNDX, seq, cnr);
    unsigned char *fid = theGOset->goset_keys + FID_LEN * fNDX;
    unsigned char *pkt = repo_feed_read(fid, seq);
    if (pkt == NULL || pkt[DMX_LEN] != PKTTYPE_chain20) continue;
    int szlen = 4;
    int sz = bipf_varint_decode(pkt, DMX_LEN + 1, &szlen);
    if (sz <= 48-szlen) continue;
    int max_chunks = (sz - (48 - szlen) + 99) / 100;
    if (cnr > max_chunks) continue;
    while (cnr <= max_chunks && credit > 0) {
      unsigned char *chunk = repo_feed_read_chunk(fid, seq, cnr);
      if (chunk == NULL)
        break;
      //Serial.println(String("  have chunk ") + to_hex(fid,20) + "." + String(seq) + "." + String(cnr));
      v += "*";
      io_enqueue(chunk, TINYSSB_PKT_LEN);
      credit--;
      cnr++;
    }
  }
  Serial.println("received chunk request: " + v + " ]");
  bipf_free(lptr);
}

void incoming_pkt(unsigned char *buf, int len, unsigned char *fid, struct face_s *f)
{
  // Serial.println("incoming pkt " + String(len));
  if (len != TINYSSB_PKT_LEN) return;
  repo_feed_append(fid, buf);
}

void incoming_chunk(unsigned char *buf, int len, int blbt_ndx, struct face_s *f)
{
  // Serial.println("received chunk");
  if (len != TINYSSB_PKT_LEN) return;
  repo_sidechain_append(buf, blbt_ndx);
}

void node_tick()
{
  static unsigned char turn; // alternate between requesting log entries an chunks
  static unsigned int log_offs;
  static unsigned int chunk_offs;

  unsigned long now = millis();
  if (now < node_next_vector && (node_next_vector-now) < 2*NODE_ROUND_LEN) // FIXME: test whether wraparound works
    return;
  node_next_vector = now + NODE_ROUND_LEN + esp_random() % 1000;

  Serial.printf("t=%d.%03d ", now/1000, now%1000);
#if defined(AXP_DEBUG)
  Serial.printf("battery=%.04gV ", axp.getBattVoltage()/1000);
#endif
  Serial.printf("|dmxt|=%d |blbt|=%d |feeds|=%d |entries|=%d |chunks|=%d |freeHeap|=%d\r\n",
                dmxt_cnt, blbt_cnt, feed_cnt, entry_cnt, chunk_cnt, ESP.getFreeHeap());

  if (theGOset->goset_len == 0)
    return;
  String v = "";
  struct bipf_s *lptr = bipf_mkList();
  turn = 1 - turn;
  if (turn) {
    log_offs = (log_offs+1) % theGOset->goset_len;
    bipf_list_append(lptr, bipf_mkInt(log_offs));
    int encoding_len = bipf_encodingLength(lptr);
    int i;
    for (i = 0; i < theGOset->goset_len; i++) {
      unsigned int ndx = (log_offs + i) % theGOset->goset_len;
      unsigned char *fid = theGOset->goset_keys + ndx*GOSET_KEY_LEN;
      struct feed_s *f = fid2feed(fid);

      // arm DMX
      unsigned char pktID[FID_LEN + 4 + HASH_LEN];
      memcpy(pktID, fid, FID_LEN);
      int s = htonl(f->next_seq); // big endian
      memcpy(pktID + FID_LEN, (unsigned char*) &s, 4);
      memcpy(pktID + FID_LEN + 4, f->prev, HASH_LEN);
      unsigned char dmx[DMX_LEN];
      compute_dmx(dmx, pktID, FID_LEN + 4 + HASH_LEN);
      arm_dmx(dmx, incoming_pkt, f->fid);

      // add to want vector
      struct bipf_s *bptr = bipf_mkInt(f->next_seq);
      encoding_len += bipf_encodingLength(bptr);
      bipf_list_append(lptr, bptr);
      v += (v.length() == 0 ? "[ " : ", ") + String(ndx) + "." + String(f->next_seq);
      if (encoding_len > 100) {
        i++;
        break;
      }
    }
    log_offs = (log_offs+i) % theGOset->goset_len;
    if (lptr->cnt > 1) {
      int sz = bipf_encodingLength(lptr);
      unsigned char buf[sz];
      bipf_encode(buf, lptr);
      io_enqueue(buf, sz, want_dmx);
      bipf_free(lptr);
      Serial.printf(">> %dB log entry request: %s ]\r\n", sz, v.c_str());
    }
    return;
  }
  
  // hunt for unfinished sidechains
  chunk_offs = (chunk_offs+1) % theGOset->goset_len;
  // Serial.printf("chunk_offs starting: %d\n", chunk_offs);
  int encoding_len = 0;
  int requested_first = -1; // in number of feeds requesting sthg
  for (int i = 0; i < theGOset->goset_len; i++) {
    unsigned int ndx = (chunk_offs + i) % theGOset->goset_len;
    unsigned char *fid = theGOset->goset_keys + ndx*GOSET_KEY_LEN;
    struct feed_s *f = fid2feed(fid);
    char dname[FEED_PATH_SIZE];
    sprintf(dname, "%s/%s", FEED_DIR, to_hex(fid, FID_LEN));
    File fdir = MyFS.open(dname);
    File g = fdir.openNextFile("r");
    while (g) {
      // Serial.println(String("looking at ") + g.name());
      char *pos = strchr(g.name(), '!');
      if (pos != NULL) {
        int seq = atoi(pos+1);
        unsigned char h[HASH_LEN];
        int sz = g.size();
        if (sz == 0) { // must fetch first ptr from log
          unsigned char *pkt = repo_feed_read(fid, seq);
          if (pkt != NULL)
            memcpy(h, pkt+DMX_LEN+1+28, HASH_LEN);
          else
            seq = -1;
        } else { // must fetch latest ptr from chain file
          g.seek(sz - HASH_LEN, SeekSet);
          if (g.read(h, HASH_LEN) != HASH_LEN) {
            Serial.println("could not read() after seek");
            seq = -1;
          } else {
            int j;
            for (j = 0; j < HASH_LEN; i++)
              if (h[j] != 0)
                break;
            if (j == HASH_LEN) // reached end of chain
              seq = -1;
          }
        }
        if (seq > 0) {
          int next_chunk = g.size() / TINYSSB_PKT_LEN;
          // FIXME: check if sidechain is already full, then swap '.' for '!' (e.g. after a crash)
          struct bipf_s *slptr = bipf_mkList();
          bipf_list_append(slptr, bipf_mkInt(ndx));
          bipf_list_append(slptr, bipf_mkInt(seq));
          bipf_list_append(slptr, bipf_mkInt(next_chunk));
          bipf_list_append(lptr, slptr);
          arm_blb(h, incoming_chunk, fid, seq, next_chunk);
          v += (v.length() == 0 ? "[ " : ", ") + String(ndx) + "." + String(seq) + "." + String(next_chunk);
          encoding_len += bipf_encodingLength(slptr);
          if (requested_first == -1)
            requested_first = i;
        }
      }
      g.close();
      if (encoding_len > 100)
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
    io_enqueue(buf, sz, chnk_dmx);
    bipf_free(lptr);
    Serial.printf(">> %dB chunk request: %s ]\r\n", sz, v.c_str());
  }

}

// eof
