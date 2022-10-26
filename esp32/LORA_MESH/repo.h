// repo.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

// -----------------------------------------------------------------------

// for global stats: (see also feed_cnt in main file)
int entry_cnt;
int chunk_cnt;


char* _feed_path(unsigned char *fid, int seq = -1, int blob = -1)
{ // FIXME: ugly and error prone functionality
  // blob=-1: create filename for MID hash, blob=1: incomplete sidechain, blob=0: full sidechain
  static char p[sizeof(FEED_DIR) + 1 + 2*FID_LEN + 8];
  char *h = to_hex(fid, FID_LEN); // to_b64(fid, FID_LEN)
  strcpy(p, FEED_DIR);
  strcat(p, "/");
  strcat(p, h);
  MyFS.mkdir(p);
  strcat(p, "/");
  if (blob == -1) {
    if (seq == -1) // plain log
      strcat(p, "log");
    else // this is for the hash (mid), else a plain log
      sprintf(p+strlen(p), "+%d", seq);
  } else
    sprintf(p+strlen(p), blob ? "!%d" : "-%d", seq); // sidechain
  return p;
}

// ----------------------------------------------------------------------

void repo_clean(char *path)
{
  File fdir = MyFS.open(path);
  if (fdir) {
    File f = fdir.openNextFile();
    while (f) {
      char *fn = strdup(f.name());
      if (f.isDirectory()) {
        repo_clean(fn);
        f.close();
        MyFS.rmdir(fn);
      } else {
        f.close();
        MyFS.remove(fn);
      }
      free(fn);
      f = fdir.openNextFile();
    }
    fdir.close();
  }
}

void repo_reset(char *path)
{
  if (path != NULL)
    Serial.printf("repo reset of path %s\n", path);
  repo_clean(path);
  if (path != NULL)
    MyFS.rmdir(path);

  // listDir(MyFS, "/", 0);
  // listDir(MyFS, FEED_DIR, 1);
  esp_restart(); // FIXME?? is this still necessary? if not, then we have to erase the in-memory GOset ...
}

void repo_load()
{
  File fdir = MyFS.open(FEED_DIR);
  File f = fdir.openNextFile("r");
  while (f) {
    char *pos = strrchr(f.name(), '/');
    if (pos != NULL)  {
      pos++;
      unsigned char *fid = from_hex(pos, FID_LEN); // from_b64(pos, FID_LEN)
      if (fid != NULL) {
        int ndx = feed_index(fid);
        if (ndx < 0) {
          ndx = feed_cnt++;
          memcpy(feeds[ndx].fid, fid, FID_LEN);
          memcpy(feeds[ndx].prev, fid, HASH_LEN);
          // leave next_seq and max_prev_seq at 0
        }
        File ldir = MyFS.open(f.name());
        File g = ldir.openNextFile("r");
        while (g) {
          pos = strrchr(g.name(), '/');
          pos++;
          if (!strcmp(pos, "log")) {
            int cnt = g.size() / TINYSSB_PKT_LEN;
            feeds[ndx].next_seq = cnt + 1;
            entry_cnt += cnt;
          } else if (pos[0] == '+') { // contains hash (mid) of highest log entry
            int seq = atoi(pos+1);
            if (seq > feeds[ndx].max_prev_seq) {
              g.read(feeds[ndx].prev, HASH_LEN);
              feeds[ndx].max_prev_seq = seq;
            }
          } else if (pos[0] == '.' || pos[0] == '-' || pos[0] == '!') {
            // the dot is being phased out because of file transfer problems
            // on UNIX where it becomes a hidden file
            chunk_cnt += g.size() / TINYSSB_PKT_LEN;
          }
          if (pos[0] == '.') { // rename the '.' files to use '-'
            char path1[100], path2[100];
            strcpy(path1, g.name());
            strcpy(path2, g.name());
            g.close();
            pos = strrchr(path2, '/');
            pos[1] = '-';
            MyFS.rename(path1, path2);
          } else
            g.close();
          g = ldir.openNextFile("r");
        }
      }
    }
    f.close();
    f = fdir.openNextFile("r");
  }
  fdir.close();

  // FIXME: check next_seq for each feed, whether the log file (no suffix) has more log entries
  // and therefore we need to compute a new message id, create a new file with a seq nr suffix
  // ... while ((feeds[ndx].max_prev_seq+1) < feeds[ndx].next_seq) .. compute missing prev values, cycle files

  // init the GOset with the found keys
  struct goset_s *gp = theGOset;
  for (int i = 0; i < feed_cnt; i++)
    memcpy(gp->goset_keys + i * GOSET_KEY_LEN, feeds[i].fid, FID_LEN);
  gp->goset_len = feed_cnt;
  qsort(gp->goset_keys, gp->goset_len, GOSET_KEY_LEN, _cmp_key32);

  // init the WANT dmx handler for this GOset
  unsigned char *claim = _mkClaim(gp, 0, gp->goset_len-1);
  memcpy(gp->goset_state, claim+65, GOSET_KEY_LEN);
  set_want_dmx();
}

void repo_new_feed(unsigned char *fid)
{
  char *path = _feed_path(fid);
  Serial.printf("new feed '%s'\n", path);
  MyFS.remove(path);
  File f = MyFS.open(path, "w");
  f.close();

  feeds[feed_cnt].next_seq = 1;
  memcpy(feeds[feed_cnt].fid, fid, FID_LEN);
  memcpy(feeds[feed_cnt].prev, fid, HASH_LEN);
  feed_cnt++;
}

unsigned char* repo_feed_read(unsigned char *fid, int seq)
{
  if (--seq < 0) return NULL;
  static unsigned char buf[TINYSSB_PKT_LEN];
  char *path = _feed_path(fid);
  File f = MyFS.open(path, "r");
  if (f.size()/TINYSSB_PKT_LEN <= seq)
    return NULL;
  f.seek(TINYSSB_PKT_LEN*seq, SeekSet);
  int sz = f.read(buf, sizeof(buf));
  f.close();
  return sz == sizeof(buf) ? buf : NULL;
}

unsigned char* repo_feed_read_chunk(unsigned char *fid, int seq, int cnr)
{
  static unsigned char buf[TINYSSB_PKT_LEN];
  File f = MyFS.open(_feed_path(fid, seq, 0), "r");
  if (!f)
    f = MyFS.open(_feed_path(fid, seq, 1), "r");
  if (!f)
    return NULL;
  f.seek(TINYSSB_PKT_LEN*cnr, SeekSet);
  int sz = f.read(buf, sizeof(buf));
  f.close();
  return sz == sizeof(buf) ? buf : NULL;
}

int repo_feed_len(unsigned char *fid)
{
  File f = MyFS.open(_feed_path(fid), "r");
  int cnt = f.size() / TINYSSB_PKT_LEN;
  f.close();
  return cnt;
}

void repo_feed_append(unsigned char *fid, unsigned char *pkt)
{
  Serial.println(String("incoming entry for log ") + to_hex(fid, FID_LEN));
  int ndx = feed_index(fid);
  if (ndx < 0) {
    Serial.println("  no such feed");
    return;
  }
  // check dmx
  unsigned char buf[strlen(DMX_PFX) + FID_LEN + 4 + HASH_LEN + TINYSSB_PKT_LEN];
  memcpy(buf, DMX_PFX, strlen(DMX_PFX));
  memcpy(buf + strlen(DMX_PFX), fid, FID_LEN);
  int s = htonl(feeds[ndx].next_seq); // big endian
  memcpy(buf + strlen(DMX_PFX) + FID_LEN, (unsigned char*) &s, 4);
  memcpy(buf + strlen(DMX_PFX) + FID_LEN + 4, feeds[ndx].prev, HASH_LEN);
  unsigned char dmx[DMX_LEN];
  compute_dmx(dmx, buf + strlen(DMX_PFX), FID_LEN + 4 + HASH_LEN);
  if (memcmp(dmx, pkt, DMX_LEN)) { // wrong dmx field
    Serial.println("  DMX mismatch");
    return;
  }
  // check signature
  memcpy(buf + strlen(DMX_PFX) + FID_LEN + 4 + HASH_LEN, pkt, TINYSSB_PKT_LEN);
  cpu_set_fast();
  int b = crypto_sign_ed25519_verify_detached(pkt + 56, buf, strlen(DMX_PFX) + FID_LEN + 4 + HASH_LEN + 56, fid);
  cpu_set_slow();
  if (b) {
    Serial.println("  ed25519 signature verification failed");
    return;
  }

  File f = MyFS.open(_feed_path(fid), FILE_APPEND);
  Serial.printf("  writing log entry %d.%d\n", _key_index(theGOset,fid), f.size()/TINYSSB_PKT_LEN + 1);
  f.write(pkt, TINYSSB_PKT_LEN);
  f.close();
  entry_cnt++;

  unsigned char h[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(h, buf, sizeof(buf));
  memcpy(feeds[ndx].prev, h, HASH_LEN);
  if (feeds[ndx].next_seq >= 1) {
    File f = MyFS.open(_feed_path(fid, feeds[ndx].next_seq), "w");
    f.write(feeds[ndx].prev, HASH_LEN);
    f.close();
  }
  if (feeds[ndx].next_seq >= 2) // above new file replaces old MID file that can be deleted now
    MyFS.remove(_feed_path(fid, feeds[ndx].next_seq - 1));
  feeds[ndx].next_seq++;

  if (pkt[DMX_LEN] == PKTTYPE_chain20) {
    // read length of content
    int len = 4; // max lenght of content length field
    int sz = bipf_varint_decode(pkt, DMX_LEN+1, &len);
    Serial.printf("  sidechain will have %d bytes\n", sz);
    if (sz > (48 - HASH_LEN - len)) { // create sidechain file
      f = MyFS.open(_feed_path(fid, feeds[ndx].next_seq-1, 1), FILE_WRITE);
      f.close();
    }
  }

  arm_dmx(pkt); // remove old DMX handler for this packet

  // install handler for next pkt:
  s = htonl(feeds[ndx].next_seq); // big endian
  memcpy(buf + strlen(DMX_PFX) + FID_LEN, (unsigned char*) &s, 4);
  memcpy(buf + strlen(DMX_PFX) + FID_LEN + 4, feeds[ndx].prev, HASH_LEN);
  // unsigned char dmx[DMX_LEN];
  compute_dmx(dmx, buf + strlen(DMX_PFX), FID_LEN + 4 + HASH_LEN);
  arm_dmx(dmx, incoming_pkt, fid);
}

void repo_sidechain_append(unsigned char *buf, int blbt_ndx)
{
  struct blb_s *bp = blbt + blbt_ndx;
  File f = MyFS.open(_feed_path(bp->fid, bp->seq, 1), "a");
  if (f && (f.size() / TINYSSB_PKT_LEN) == bp->bnr) {
    Serial.printf("  persisting chunk %d.%d.%d\n", _key_index(theGOset,bp->fid), bp->seq, bp->bnr);
    f.write(buf, TINYSSB_PKT_LEN);
    f.close();
    chunk_cnt++;
    int i;
    for (i = TINYSSB_PKT_LEN - HASH_LEN; i < TINYSSB_PKT_LEN; i++)
      if (buf[i] != 0)
         break;
    if (i == TINYSSB_PKT_LEN) { // end of chain reached, rename file
      char *old = _feed_path(bp->fid, bp->seq, 1);
      char *fin = strdup(old);
      char *pos = strrchr(fin, '!');
      *pos = '-';
      MyFS.rename(old, fin);
      free(fin);
    } else { // chain extends, install next chunk handler
      arm_blb(buf + TINYSSB_PKT_LEN - HASH_LEN, incoming_chunk, bp->fid, bp->seq, bp->bnr+1);
    }
  } else
    Serial.printf("  invalid chunk %d.%d.%d or file problem?\n", _key_index(theGOset,bp->fid), bp->seq, bp->bnr);
  arm_blb(bp->h); // remove old CHUNK handler for this packet
}

// eof
