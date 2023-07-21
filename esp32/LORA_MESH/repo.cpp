// repo.cpp

// tinySSB for ESP32
// (c) 2022-2023 <christian.tschudin@unibas.ch>

#include <lwip/def.h>
#include <LittleFS.h>
#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_sign_ed25519.h>
#include <stdio.h>
#include <string.h>

#include "LORA_MESH.h"

extern void incoming_pkt(unsigned char* buf, int len, unsigned char *fid, struct face_s *);
extern void incoming_chunk(unsigned char* buf, int len, int blbt_ndx, struct face_s *);
extern void incoming_want_request(unsigned char* buf, int len, unsigned char* aux, struct face_s *);
extern void incoming_chnk_request(unsigned char* buf, int len, unsigned char* aux, struct face_s *);


#define MyFS LittleFS

/*
  files for log and side chains use the following conventions, where N is a digit:

  ./-NNN  complete side chain for log entry with sequence number NNN
  ./!NNN  incomplete side chain for log entry with sequence number NNN
  ./+NN   message id (mID) for log entry NN, which typically is the last in ./log
  ./log   the append-only log
 */

char* RepoClass::_feed_path(unsigned char *fid)
{
  static char path[FEED_PATH_SIZE];
  sprintf(path, "%s/%s", FEED_DIR, to_hex(fid, FID_LEN, 0));
  return path;
}

char* RepoClass::_feed_log(unsigned char *fid)
{
  char *path = _feed_path(fid);
  static char file[FEED_PATH_SIZE + 1 + 3];
  sprintf(file, "%s/log", path);
  return file;
}

char* RepoClass::_feed_mid(unsigned char *fid, int seq)
{
  char *path = _feed_path(fid);
  static char file[FEED_PATH_SIZE + 1 + 3];
  sprintf(file, "%s/+%d", path, seq);
  return file;
}

char* RepoClass::_feed_chnk(unsigned char *fid, int seq, bool complete)
{
  char *path = _feed_path(fid);
  static char file[FEED_PATH_SIZE + 1 + 10];
  sprintf(file, complete ? "%s/!%d" : "%s/-%d", path, seq);
  return file;
}

// ----------------------------------------------------------------------

void RepoClass::clean(char *path)
{
  File fdir = MyFS.open(path);
  if (fdir) {
    File f = fdir.openNextFile();
    while (f) {
      char *fn = strdup(f.path());
      if (f.isDirectory()) {
        this->clean(fn);
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

void RepoClass::reset(char *path)
{
  if (path == NULL) { path = FEED_DIR; }
  Serial.printf("repo reset of path %s\r\n", path);
  this->clean(path);
  if (path != NULL)
    MyFS.rmdir(path);

  // listDir(MyFS, "/", 0);
  // listDir(MyFS, FEED_DIR, 1);
  esp_restart(); // FIXME?? is this still necessary? if not, then we have to erase the in-memory GOset ...
}

void RepoClass::load()
{
  File fdir = MyFS.open(FEED_DIR);
  File f = fdir.openNextFile("r");
  while (f) {
    char* pos = (char*) f.name();
    // Serial.printf("repo load %s\r\n", pos);
    unsigned char *fid = from_hex(pos, FID_LEN); // from_b64(pos, FID_LEN)
    if (fid != NULL) {
      // check if feed active
      //File k = MyFS.open(pathToCntFile, "r");
	//unsigned int cnt = 0;
      //k.read((unsigned char*) &cnt, sizeof(cnt));
	//k.close();
	//if (cnt % 2 == 1) {
	//  Serial.printf("repo_load: ignoring inactive feed %s with cnt = %d\r\n", fid, cnt);
      //  f.close();
      //  f = fdir.openNextFile("r");
	//  continue;
	//}
	// get index
      int ndx = this->feed_index(fid);
      // Serial.printf("  ndx1 is %d, cnt=%d %d %d\r\n",
      //               ndx, this->feed_cnt, FID_LEN, HASH_LEN);
      if (ndx < 0) {
        ndx = this->feed_cnt++;
        memcpy(this->feeds[ndx].fid, fid, FID_LEN);
        memcpy(this->feeds[ndx].prev, fid, HASH_LEN);
        // leave next_seq and max_prev_seq at 0
      }
      // Serial.printf("  ndx2 is %d\r\n", ndx);
      char *lpath = _feed_path(fid);
      File ldir = MyFS.open(lpath);
      File g = ldir.openNextFile("r");
      while (g) {
        pos = (char *) g.name();
        if (!strcmp(pos, "log")) {
          int cnt = g.size() / TINYSSB_PKT_LEN;
          this->feeds[ndx].next_seq = cnt + 1;
          this->entry_cnt += cnt;
        } else if (pos[0] == '+') { // contains hash (mid) of highest log entry
          int seq = atoi(pos+1);
          if (seq > this->feeds[ndx].max_prev_seq) {
            g.read(this->feeds[ndx].prev, HASH_LEN);
            this->feeds[ndx].max_prev_seq = seq;
          }
        } else if (pos[0] == '-' || pos[0] == '!') {
          this->chunk_cnt += g.size() / TINYSSB_PKT_LEN;
          // we could catch "open" ('!NNN') but finished side chains here, and rename
          // them to the '-NNN' format, but currently we do it in the node_tick() method
        }
        g.close();
        g = ldir.openNextFile("r");
      }
      if (this->feeds[ndx].next_seq == 0) { // no log file found, create it
        Serial.printf("HAVE TO CREATE LOG FILE FOR ndx=%d\r\n", ndx);
        MyFS.open(_feed_log(fid), "w").close();
        this->feeds[ndx].next_seq == 1;
      }
      // check next_seq for each feed, whether the log file (no
      // suffix) has more log entries and therefore we need to compute
      // a new message id MID, create a new file with a seq nr suffix
      // and remove the old MID file
      while ((this->feeds[ndx].max_prev_seq+1) < this->feeds[ndx].next_seq) {
        unsigned char buf[strlen(DMX_PFX) + FID_LEN + 4 + HASH_LEN + TINYSSB_PKT_LEN];
        memcpy(buf, DMX_PFX, strlen(DMX_PFX));
        memcpy(buf + strlen(DMX_PFX), fid, FID_LEN);
        int s = htonl(this->feeds[ndx].max_prev_seq); // big endian
        memcpy(buf + strlen(DMX_PFX) + FID_LEN, (unsigned char*) &s, 4);
        memcpy(buf + strlen(DMX_PFX) + FID_LEN + 4, this->feeds[ndx].prev, HASH_LEN);
        unsigned char dmx_val[DMX_LEN];
        dmx->compute_dmx(dmx_val, buf + strlen(DMX_PFX), FID_LEN + 4 + HASH_LEN);
        unsigned char *pkt = repo->feed_read(fid, this->feeds[ndx].max_prev_seq);
        memcpy(buf + strlen(DMX_PFX) + FID_LEN + 4 + HASH_LEN, pkt, TINYSSB_PKT_LEN);
        unsigned char h[crypto_hash_sha256_BYTES];
        crypto_hash_sha256(h, buf, sizeof(buf));
        memcpy(this->feeds[ndx].prev, h, HASH_LEN);
        this->feeds[ndx].max_prev_seq++;
        File f2 = MyFS.open(_feed_mid(fid, this->feeds[ndx].max_prev_seq), "w");
        f2.write(this->feeds[ndx].prev, HASH_LEN);
        f2.close();
        // remove old MID file
        if (this->feeds[ndx].next_seq >= 2)
          MyFS.remove(_feed_mid(fid, this->feeds[ndx].max_prev_seq - 1));
      }
    }
    f.close();
    f = fdir.openNextFile("r");
  }
  fdir.close();


  // init the GOset with the found keys
  for (int i = 0; i < this->feed_cnt; i++)
    theGOset->populate(this->feeds[i].fid);
  theGOset->populate(NULL); // triggers sorting, and setting the want_dmx
}

void RepoClass::new_feed(unsigned char *fid)
{
  char *path = _feed_path(fid);
  Serial.printf("   repo: new feed '%s'\r\n", path);
  MyFS.mkdir(path);
  char *log = _feed_log(fid);
  File f = MyFS.open(log, "w");
  f.close();

  this->feeds[this->feed_cnt].next_seq = 1;
  memcpy(this->feeds[this->feed_cnt].fid, fid, FID_LEN);
  memcpy(this->feeds[this->feed_cnt].prev, fid, HASH_LEN);
  this->feed_cnt++;
}

unsigned char* RepoClass::feed_read(unsigned char *fid, int seq)
{
  if (--seq < 0) return NULL;
  static unsigned char buf[TINYSSB_PKT_LEN];
  char *path = _feed_log(fid);
  File f = MyFS.open(path, "r");
  if (f.size()/TINYSSB_PKT_LEN <= seq)
    return NULL;
  f.seek(TINYSSB_PKT_LEN*seq, SeekSet);
  int sz = f.read(buf, sizeof(buf));
  f.close();
  return sz == sizeof(buf) ? buf : NULL;
}

unsigned char* RepoClass::feed_read_chunk(unsigned char *fid, int seq, int cnr)
{
  static unsigned char buf[TINYSSB_PKT_LEN];
  File f = MyFS.open(_feed_chnk(fid, seq, 0), "r");
  if (!f)
    f = MyFS.open(_feed_chnk(fid, seq, 1), "r");
  if (!f)
    return NULL;
  f.seek(TINYSSB_PKT_LEN*cnr, SeekSet);
  int sz = f.read(buf, sizeof(buf));
  f.close();
  return sz == sizeof(buf) ? buf : NULL;
}

int RepoClass::feed_len(unsigned char *fid)
{
  File f = MyFS.open(_feed_log(fid), "r");
  int cnt = f.size() / TINYSSB_PKT_LEN;
  f.close();
  return cnt;
}

void RepoClass::feed_append(unsigned char *fid, unsigned char *pkt)
{
  // Serial.println(String("incoming entry for log ") + to_hex(fid, FID_LEN, 0));

  long durations[10], t1, t2;
  t1 = millis();

  int ndx = feed_index(fid);
  if (ndx < 0) {
    Serial.println("  no such feed");
    return;
  }
  t2 = millis(); durations[0] = t2 - t1; t1 = t2;
  
  // check dmx
  unsigned char buf[strlen(DMX_PFX) + FID_LEN + 4 + HASH_LEN + TINYSSB_PKT_LEN];
  memcpy(buf, DMX_PFX, strlen(DMX_PFX));
  memcpy(buf + strlen(DMX_PFX), fid, FID_LEN);
  int s = htonl(this->feeds[ndx].next_seq); // big endian
  memcpy(buf + strlen(DMX_PFX) + FID_LEN, (unsigned char*) &s, 4);
  memcpy(buf + strlen(DMX_PFX) + FID_LEN + 4, this->feeds[ndx].prev, HASH_LEN);
  unsigned char dmx_val[DMX_LEN];
  dmx->compute_dmx(dmx_val, buf + strlen(DMX_PFX), FID_LEN + 4 + HASH_LEN);
  if (memcmp(dmx_val, pkt, DMX_LEN)) { // wrong dmx field
    Serial.println("   DMX mismatch");
    return;
  }
  t2 = millis(); durations[1] = t2 - t1; t1 = t2;
  fishForNewLoRaPkt();

  // check signature
  memcpy(buf + strlen(DMX_PFX) + FID_LEN + 4 + HASH_LEN, pkt, TINYSSB_PKT_LEN);
  // cpu_set_fast();
  int b = crypto_sign_ed25519_verify_detached(pkt + 56, buf, strlen(DMX_PFX) + FID_LEN + 4 + HASH_LEN + 56, fid);
  // cpu_set_slow();
  if (b) {
    Serial.println("   ed25519 signature verification failed");
    return;
  }
  t2 = millis(); durations[2] = t2 - t1; t1 = t2;
  fishForNewLoRaPkt();

  // create file for sidechain first, in case of crash before extending the log
  if (pkt[DMX_LEN] == PKTTYPE_chain20) {
    // read length of content
    int len = 4; // max lenght of content length field
    int sz = bipf_varint_decode(pkt, DMX_LEN+1, &len);
    Serial.printf("   sidechain for %d.%d will contain %d bytes\r\n",
                  theGOset->_key_index(fid), this->feeds[ndx].next_seq, sz);
    if (sz > (48 - HASH_LEN - len)) { // create sidechain file
      File f = MyFS.open(_feed_chnk(fid, this->feeds[ndx].next_seq, 1), FILE_WRITE);
      f.close();
    }
    fishForNewLoRaPkt();
  }
  t2 = millis(); durations[3] = t2 - t1; t1 = t2;

  File f = MyFS.open(_feed_log(fid), FILE_APPEND);
  Serial.printf("   appended %d.%d\r\n", theGOset->_key_index(fid), f.size()/TINYSSB_PKT_LEN + 1);
  f.write(pkt, TINYSSB_PKT_LEN);
  f.close();
  this->entry_cnt++;
  t2 = millis(); durations[4] = t2 - t1; t1 = t2;
  fishForNewLoRaPkt();

  // update the MID file. When this is not reached due to a creah, we re-scan at LOAD time
  unsigned char h[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(h, buf, sizeof(buf));
  memcpy(this->feeds[ndx].prev, h, HASH_LEN);
  if (this->feeds[ndx].next_seq >= 1) {
    File f = MyFS.open(_feed_mid(fid, this->feeds[ndx].next_seq), "w");
    f.write(this->feeds[ndx].prev, HASH_LEN);
    f.close();
    fishForNewLoRaPkt();
  }
  t2 = millis(); durations[5] = t2 - t1; t1 = t2;

  if (this->feeds[ndx].next_seq >= 2) // above new file replaces old MID file that can be deleted now
    MyFS.remove(_feed_mid(fid, this->feeds[ndx].next_seq - 1));
  this->feeds[ndx].next_seq++;
  t2 = millis(); durations[6] = t2 - t1; t1 = t2;

  dmx->arm_dmx(pkt); // remove old DMX handler for this packet
  t2 = millis(); durations[7] = t2 - t1; t1 = t2;

  // install handler for next pkt:
  s = htonl(this->feeds[ndx].next_seq); // big endian
  memcpy(buf + strlen(DMX_PFX) + FID_LEN, (unsigned char*) &s, 4);
  memcpy(buf + strlen(DMX_PFX) + FID_LEN + 4, this->feeds[ndx].prev, HASH_LEN);
  // unsigned char dmx[DMX_LEN];
  dmx->compute_dmx(dmx_val, buf + strlen(DMX_PFX), FID_LEN + 4 + HASH_LEN);
  t2 = millis(); durations[8] = t2 - t1; t1 = t2;
  fishForNewLoRaPkt();
  dmx->arm_dmx(dmx_val, incoming_pkt, fid, ndx, this->feeds[ndx].next_seq);
  // Serial.printf("   armed %s for %d.%d\r\n", to_hex(dmx_val, 7),
  //               ndx, this->feeds[ndx].next_seq);
  t2 = millis(); durations[9] = t2 - t1; t1 = t2;

  Serial.printf("   durations");
  for (int i = 0; i < sizeof(durations)/sizeof(long); i++)
    Serial.printf(" %ld", durations[i]);
  Serial.printf("\r\n");
}

int RepoClass::feed_index(unsigned char* fid) {
  for (int i = 0; i < this->feed_cnt; i++)
    if (!memcmp(fid, this->feeds[i].fid, FID_LEN))
      return i;
  return -1;  
}

struct feed_s* RepoClass::fid2feed(unsigned char* fid) {
  int ndx = this->feed_index(fid);
  if (ndx < 0) return NULL;
  return this->feeds + ndx;
}

void RepoClass::sidechain_append(unsigned char *buf, int blbt_ndx)
{
  struct blb_s *bp = dmx->blbt + blbt_ndx;
  File f = MyFS.open(_feed_chnk(bp->fid, bp->seq, 1), "a");
  int ndx = theGOset->_key_index(bp->fid);
  if (f && (f.size() / TINYSSB_PKT_LEN) == bp->bnr) {
    Serial.printf("   persisting chunk %d.%d.%d\r\n", ndx, bp->seq, bp->bnr);
    f.write(buf, TINYSSB_PKT_LEN);
    f.close();
    this->chunk_cnt++;
    if (bp->bnr >= bp->last_bnr) { // end of chain reached, rename file
      char *old = _feed_chnk(bp->fid, bp->seq, 1);
      char *fin = strdup(old);
      char *pos = strrchr(fin, '!');
      *pos = '-';
      MyFS.rename(old, fin);
      free(fin);
    } else { // chain extends, install next chunk handler
      unsigned char *hptr = buf + TINYSSB_PKT_LEN - HASH_LEN;
      dmx->arm_blb(hptr, incoming_chunk, bp->fid, bp->seq, bp->bnr+1, bp->last_bnr);
      // Serial.printf("   armed %s for %d.%d.%d\r\n", to_hex(hptr, HASH_LEN),
      //               ndx, bp->seq, bp->bnr+1);
    }
  } else
    Serial.printf("  invalid chunk %d.%d.%d/%d or file problem?\r\n",
                  ndx, bp->seq, bp->bnr, bp->last_bnr);
  dmx->arm_blb(bp->h); // remove old CHUNK handler for this packet
}


// eof
