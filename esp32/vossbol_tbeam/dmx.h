// dmx.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

// FIXME: in the code, rename blob to chunk (blb_s, blbt,  etc)

#define DMXT_SIZE   (GOSET_MAX_KEYS) // we need place for want (1 per feed)
#define BLBT_SIZE   100              // this size is a guess
#define DMX_PFX     "tinyssb-v0"


struct dmx_s {
  unsigned char dmx[DMX_LEN];
  void (*fct)(unsigned char*, int, unsigned char *aux);
  unsigned char *aux;
};

struct blb_s {
  unsigned char h[HASH_LEN];
  void (*fct)(unsigned char*, int, int);
  unsigned char *fid;
  int seq;
  int bnr;
};

struct dmx_s dmxt[DMXT_SIZE];
int dmxt_cnt;
struct blb_s blbt[BLBT_SIZE];
int blbt_cnt;

unsigned char want_dmx[DMX_LEN];
unsigned char chnk_dmx[DMX_LEN];

// -----------------------------------------------------------------------
int _dmxt_index(unsigned char *dmx)
{
  for (int i = 0; i < dmxt_cnt; i++) {
    if (!memcmp(dmx, dmxt[i].dmx, DMX_LEN))
      return i;
  }
  return -1;
}

int _blbt_index(unsigned char *h)
{
  for (int i = 0; i < blbt_cnt; i++) {
    if (!memcmp(h, blbt[i].h, HASH_LEN))
      return i;
  }
  return -1;
}

void arm_dmx(unsigned char *dmx,
             void (*fct)(unsigned char*, int, unsigned char*)=NULL,
             unsigned char *aux=NULL)
{
  int ndx = _dmxt_index(dmx);
  if (fct == NULL) { // del
    if (ndx != -1) {
      memmove(dmxt+ndx, dmxt+ndx+1, (dmxt_cnt - ndx - 1) * sizeof(struct dmx_s));
      dmxt_cnt--;
    }
    return;
  }
  if (ndx == -1) {
    if (dmxt_cnt >= DMXT_SIZE) {
      Serial.println("adm_dmx: dmxt is full");
      return; // full
    }
    ndx = dmxt_cnt++;
  }
  memcpy(dmxt[ndx].dmx, dmx, DMX_LEN);
  dmxt[ndx].fct = fct;
  dmxt[ndx].aux = aux;
}

void arm_blb(unsigned char *h,
             void (*fct)(unsigned char*, int, int)=NULL,
             unsigned char *fid=NULL, int seq=-1, int bnr=-1)
{
  int ndx = _blbt_index(h);
  if (ndx >= 0) // this entry will be either erased or newly written to
      free(blbt[ndx].fid);
  if (fct == NULL) { // del
    if (ndx != -1) {
      memmove(blbt+ndx, blbt+ndx+1, (blbt_cnt - ndx - 1) * sizeof(struct blb_s));
      blbt_cnt--;
    }
    return;
  }
  if (ndx == -1) {
    if (blbt_cnt >= BLBT_SIZE) {
      Serial.println("adm_dmx: blbt is full");
      return; // full
    }
    ndx = blbt_cnt++;
  }
  memcpy(blbt[ndx].h, h, HASH_LEN);
  blbt[ndx].fct = fct;
  blbt[ndx].fid = (unsigned char*) malloc(FID_LEN);
  memcpy(blbt[ndx].fid, fid, FID_LEN);
  blbt[ndx].seq = seq;
  blbt[ndx].bnr = bnr;
}

void compute_dmx(unsigned char *dst, unsigned char *buf, int len)
{
  struct crypto_hash_sha256_state h;
  unsigned char out[crypto_hash_sha256_BYTES];
  crypto_hash_sha256_init(&h);
  crypto_hash_sha256_update(&h, (unsigned char*) DMX_PFX, strlen(DMX_PFX));
  crypto_hash_sha256_update(&h, buf, len);
  crypto_hash_sha256_final(&h, out);
  // Serial.printf("comput_dmx %s", to_hex((unsigned char*)DMX_PFX, DMX_LEN));
  // Serial.printf("%s\n", to_hex(buf, len));
  // memcpy(dst, out, DMX_LEN);
  // Serial.printf(" --> dmx=%s\n", to_hex(dst, DMX_LEN));
}

int on_rx(unsigned char *buf, int len)
{
  unsigned char h[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(h, buf, len);
  Serial.print("<< buf " + String(len) + " " + to_hex(buf,16) + "... hash=");
  Serial.println(to_hex(h, HASH_LEN));

  int rc = -1;
  int ndx = _dmxt_index(buf);
  if (ndx >= 0) {
    // dmxt[ndx].fct(buf + DMX_LEN, len - DMX_LEN, dmxt[ndx].aux);
    dmxt[ndx].fct(buf, len, dmxt[ndx].aux);
    // return 0;  // try also the hash path (colliding DMX values so both handler must be served)
    rc = 0;
  }
  ndx = _blbt_index(h);
  if (ndx >= 0) {
    blbt[ndx].fct(buf, len, ndx);
    rc = 0;
  }
  return rc;
}

void set_want_dmx()
{
  arm_dmx(want_dmx, NULL); // undefine current handler
  arm_dmx(chnk_dmx, NULL); // undefine current handler
  unsigned char buf[4 + GOSET_KEY_LEN];

  memcpy(buf, "want", 4);
  memcpy(buf+4, theGOset->goset_state, GOSET_KEY_LEN);
  compute_dmx(want_dmx, buf, sizeof(buf));
  arm_dmx(want_dmx, incoming_want_request, NULL);
  Serial.println(String("listening for WANT (request) protocol on ") + to_hex(want_dmx, DMX_LEN));

  memcpy(buf, "blob", 4); // FIXME: value is historic -- should be the string "chunk" for a next tinySSB protocol version 
  memcpy(buf+4, theGOset->goset_state, GOSET_KEY_LEN);
  compute_dmx(chnk_dmx, buf, sizeof(buf));
  arm_dmx(chnk_dmx, incoming_chnk_request, NULL);
  Serial.println(String("listening for CHNK (request) protocol on ") + to_hex(chnk_dmx, DMX_LEN));
}

// eof
