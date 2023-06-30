// dmx.cpp

// tinySSB for ESP32
// (c) 2022-2023 <christian.tschudin@unibas.ch>

// FIXME: in the code, rename blob to chunk (blb_s, blbt,  etc)

#include <sodium/crypto_hash_sha256.h>
#include <stdio.h>
#include <string.h>

#include "LORA_MESH.h"
#include "dmx.h"

extern GOsetClass *theGOset;

extern void incoming_want_request(unsigned char* buf, int len, unsigned char* aux, struct face_s *);
extern void incoming_chnk_request(unsigned char* buf, int len, unsigned char* aux, struct face_s *);

// -----------------------------------------------------------------------

int DmxClass::_dmxt_index(unsigned char *dmx)
{
  for (int i = 0; i < this->dmxt_cnt; i++) {
    if (!memcmp(dmx, this->dmxt[i].dmx, DMX_LEN))
      return i;
  }
  return -1;
}

int DmxClass::_blbt_index(unsigned char *h)
{
  for (int i = 0; i < this->blbt_cnt; i++) {
    if (!memcmp(h, this->blbt[i].h, HASH_LEN))
      return i;
  }
  return -1;
}

void DmxClass::arm_dmx(unsigned char *dmx,
             void (*fct)(unsigned char*, int, unsigned char*, struct face_s*),
             unsigned char *aux)
{
  int ndx = this->_dmxt_index(dmx);
  if (fct == NULL) { // del
    if (ndx != -1) {
      memmove(this->dmxt+ndx, this->dmxt+ndx+1,
              (this->dmxt_cnt - ndx - 1) * sizeof(struct dmx_s));
      this->dmxt_cnt--;
    }
    return;
  }
  if (ndx == -1) {
    if (this->dmxt_cnt >= DMXT_SIZE) {
      Serial.println("adm_dmx: dmxt is full");
      return; // full
    }
    ndx = this->dmxt_cnt++;
  }
  memcpy(this->dmxt[ndx].dmx, dmx, DMX_LEN);
  this->dmxt[ndx].fct = fct;
  this->dmxt[ndx].aux = aux;
}

void DmxClass::arm_blb(unsigned char *h,
             void (*fct)(unsigned char*, int, int, struct face_s*),
                       unsigned char *fid, int seq, int bnr, int last)
{
  int ndx = this->_blbt_index(h);
  if (ndx >= 0) // this entry will be either erased or newly written to
      free(this->blbt[ndx].fid);
  if (fct == NULL) { // del
    if (ndx != -1) {
      memmove(this->blbt+ndx, this->blbt+ndx+1,
              (this->blbt_cnt - ndx - 1) * sizeof(struct blb_s));
      this->blbt_cnt--;
    }
    return;
  }
  if (ndx == -1) {
    if (this->blbt_cnt >= BLBT_SIZE) {
      Serial.println("adm_dmx: blbt is full");
      return; // full
    }
    ndx = this->blbt_cnt++;
  }
  memcpy(this->blbt[ndx].h, h, HASH_LEN);
  this->blbt[ndx].fct = fct;
  this->blbt[ndx].fid = (unsigned char*) malloc(FID_LEN);
  memcpy(this->blbt[ndx].fid, fid, FID_LEN);
  this->blbt[ndx].seq = seq;
  this->blbt[ndx].bnr = bnr;
  this->blbt[ndx].last_bnr = last;
}

void DmxClass::compute_dmx(unsigned char *dst, unsigned char *buf, int len)
{
  struct crypto_hash_sha256_state h;
  unsigned char out[crypto_hash_sha256_BYTES];
  crypto_hash_sha256_init(&h);
  crypto_hash_sha256_update(&h, (unsigned char*) DMX_PFX, strlen(DMX_PFX));
  crypto_hash_sha256_update(&h, buf, len);
  crypto_hash_sha256_final(&h, out);
  // Serial.printf("comput_dmx %s", to_hex((unsigned char*)DMX_PFX, DMX_LEN));
  // Serial.printf("%s\n", to_hex(buf, len));
  memcpy(dst, out, DMX_LEN);
  // Serial.printf(" --> dmx=%s\n", to_hex(dst, DMX_LEN));
}

int DmxClass::on_rx(unsigned char *buf, int len, struct face_s *f)
{
  unsigned char h[crypto_hash_sha256_BYTES];
  crypto_hash_sha256(h, buf, len);
  Serial.printf("<%c %dB %s..., hash=", f->name[0], len, to_hex(buf,16,0));
  Serial.println(to_hex(h, HASH_LEN,0));

  int rc = -1;
  int ndx = this->_dmxt_index(buf);
  if (ndx >= 0) {
    // dmxt[ndx].fct(buf + DMX_LEN, len - DMX_LEN, dmxt[ndx].aux);
    this->dmxt[ndx].fct(buf, len, this->dmxt[ndx].aux, f);
    // return 0;  // try also the hash path (colliding DMX values so both handler must be served)
    rc = 0;
  }
  ndx = this->_blbt_index(h);
  if (ndx >= 0) {
    this->blbt[ndx].fct(buf, len, ndx, f);
    rc = 0;
  }
  return rc;
}

void DmxClass::set_want_dmx()
{
  this->arm_dmx(this->want_dmx, NULL); // undefine current handler
  this->arm_dmx(this->chnk_dmx, NULL); // undefine current handler
  unsigned char buf[4 + GOSET_KEY_LEN];

  memcpy(buf, "want", 4);
  memcpy(buf+4, theGOset->goset_state, GOSET_KEY_LEN);
  compute_dmx(this->want_dmx, buf, sizeof(buf));
  arm_dmx(this->want_dmx, incoming_want_request, NULL);
  Serial.println(String("DMX for WANT is ") + to_hex(this->want_dmx, DMX_LEN, 0));

  memcpy(buf, "blob", 4); // FIXME: value is historic -- should be the string "chunk" for a next tinySSB protocol version 
  memcpy(buf+4, theGOset->goset_state, GOSET_KEY_LEN);
  compute_dmx(this->chnk_dmx, buf, sizeof(buf));
  arm_dmx(this->chnk_dmx, incoming_chnk_request, NULL);
  Serial.println(String("DMX for CHNK is ") + to_hex(this->chnk_dmx, DMX_LEN, 0));
}

// eof
