// node.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>


#define NODE_ROUND_LEN  5000 // millis
unsigned int node_next_vector; // time when next vector should be sent

void incoming_want_request(unsigned char *buf, int len, unsigned char *aux)
{
  struct bipf_s *lptr = bipf_loads(buf + DMX_LEN, len - DMX_LEN);
  if (lptr == NULL || lptr->typ != BIPF_LIST) return;
  Serial.print("want handler: received vector is [");
  struct bipf_s **slpptr = lptr->u.list;
  int credit = 3;
  for (int i = 0; i < lptr->cnt; i++, slpptr++) {
    if ((*slpptr)->typ != BIPF_LIST || (*slpptr)->cnt < 2 ||
        (*slpptr)->u.list[0]->typ != BIPF_INT || (*slpptr)->u.list[1]->typ != BIPF_INT)
        continue;
    int fNDX = (*slpptr)->u.list[0]->u.i;
    int seq = (*slpptr)->u.list[1]->u.i;
    Serial.print(" " + String(fNDX) + "." + String(seq));
    unsigned char *fid = theGOset->goset_keys + fNDX * GOSET_KEY_LEN;
    while (credit > 0) {
      unsigned char *pkt = repo_feed_read(fid, seq);
      if (pkt == NULL) break;
      Serial.println(String("  have entry ") + to_hex(fid,10) + "." + String(seq));
      io_enqueue(pkt, TINYSSB_PKT_LEN);
      seq++;
      credit--;
    }
  }
  Serial.println(" ]");
  bipf_free(lptr);
}

void incoming_chnk_request(unsigned char *buf, int len, unsigned char *aux)
{
  struct bipf_s *lptr = bipf_loads(buf + DMX_LEN, len - DMX_LEN);
  if (lptr == NULL || lptr->typ != BIPF_LIST) return;
  Serial.print("chunk handler: received vector is [");
  struct bipf_s **slpptr = lptr->u.list;
  int credit = 3;
  for (int i = 0; i < lptr->cnt; i++, slpptr++) {
    if ((*slpptr)->typ != BIPF_LIST || (*slpptr)->cnt < 3 ||
        (*slpptr)->u.list[0]->typ != BIPF_INT || (*slpptr)->u.list[1]->typ != BIPF_INT)
        continue;
    int fNDX = (*slpptr)->u.list[0]->u.i;
    int seq = (*slpptr)->u.list[1]->u.i;
    int cnr = (*slpptr)->u.list[2]->u.i; // chunk nr
    Serial.print(" " + String(fNDX) + "." + String(seq) + "." + String(cnr));
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
      Serial.println(String("  have chunk ") + to_hex(fid,20) + "." + String(seq) + "." + String(cnr));
      io_enqueue(chunk, TINYSSB_PKT_LEN);
      credit--;
      cnr++;
    }
  }
  Serial.println(" ]");
  bipf_free(lptr);
}

void incoming_pkt(unsigned char *buf, int len, unsigned char *fid)
{
  Serial.println("incoming pkt " + String(len));
  if (len != TINYSSB_PKT_LEN) return;
  // Serial.println(to_hex(fid, FID_LEN));
  repo_feed_append(fid, buf);
}

void incoming_chunk(unsigned char *buf, int len, int blbt_ndx)
{
  Serial.println("received chunk");
  if (len != TINYSSB_PKT_LEN) return;
  repo_sidechain_append(buf, blbt_ndx);
}

void node_tick()
{
  unsigned long now = millis();
  if (now < node_next_vector && (node_next_vector-now) < 2*NODE_ROUND_LEN) // FIXME: test whether wraparound works
    return;

  node_next_vector = now + NODE_ROUND_LEN + esp_random() % 1000;
  if (theGOset->goset_len == 0)
    return;

  Serial.print("|dmxt|=" + String(dmxt_cnt) + ", |blbt|=" + String(blbt_cnt));
  Serial.println(", stats: |feeds|=" + String(feed_cnt) + ", |entries|=" + String(entry_cnt) + ", |chunks|=" + String(chunk_cnt));

  // FIXME: limit vector to 100B, rotate through set
  struct bipf_s *lptr = bipf_mkList();
  for (int i = 0; i < theGOset->goset_len; i++) {
    unsigned char *fid = theGOset->goset_keys + i*GOSET_KEY_LEN;
    struct feed_s *f = fid2feed(fid);

    unsigned char pktID[FID_LEN + 4 + HASH_LEN];
    memcpy(pktID, fid, FID_LEN);
    int s = htonl(f->next_seq); // big endian
    memcpy(pktID + FID_LEN, (unsigned char*) &s, 4);
    memcpy(pktID + FID_LEN + 4, f->prev, HASH_LEN);
    unsigned char dmx[DMX_LEN];
    compute_dmx(dmx, pktID, FID_LEN + 4 + HASH_LEN);
    arm_dmx(dmx, incoming_pkt, f->fid);

    // add to want vector
    struct bipf_s *slptr = bipf_mkList();
    bipf_list_append(slptr, bipf_mkInt(i));
    bipf_list_append(slptr, bipf_mkInt(f->next_seq));
    bipf_list_append(lptr, slptr);
  }

  {
    int sz = bipf_encodingLength(lptr);
    unsigned char buf[sz];
    bipf_encode(buf, lptr);
    io_enqueue(buf, sz, want_dmx);
    bipf_free(lptr);
  }

  // hunt for unfinished sidechains
  lptr = bipf_mkList();
  File fdir = MyFS.open(FEED_DIR);
  File f = fdir.openNextFile("r");
  while (f) {
    if (f.isDirectory()) {
      unsigned char *fid = from_hex(strrchr(f.name(), '/')+1, FID_LEN); // from_b64(pos, FID_LEN)
      if (fid != NULL) {
        File ldir = MyFS.open(f.name());
        File g = ldir.openNextFile("r");
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
                int i;
                for (i = 0; i < HASH_LEN; i++)
                  if (h[i] != 0)
                    break;
                if (i == HASH_LEN) // reached end of chain
                  seq = -1;
              }
            }
            if (seq > 0) {
              int next_chunk = g.size() / TINYSSB_PKT_LEN;
              // FIXME: check if sidechain is already full, then swap '.' for '!' (e.g. after a crash)
              struct bipf_s *slptr = bipf_mkList();
              bipf_list_append(slptr, bipf_mkInt(_key_index(theGOset,fid)));
              bipf_list_append(slptr, bipf_mkInt(seq));
              bipf_list_append(slptr, bipf_mkInt(next_chunk));
              bipf_list_append(lptr, slptr);
              arm_blb(h, incoming_chunk, fid, seq, next_chunk);
            }
          }
          g.close();
          g = ldir.openNextFile("r");
        } // while (g)
      }
    }
    f.close();
    f = fdir.openNextFile("r");
  }
  fdir.close();
  if (lptr->cnt > 0) {
    int sz = bipf_encodingLength(lptr);
    unsigned char buf[sz];
    bipf_encode(buf, lptr);
    io_enqueue(buf, sz, chnk_dmx);
    bipf_free(lptr);
  }

}

// eof
