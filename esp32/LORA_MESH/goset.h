// goset.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

// Grow-Only Set

#define GOSET_KEY_LEN FID_LEN // 32
#define GOSET_MAX_KEYS      MAX_FEEDS
#define GOSET_ROUND_LEN 10000 // in millis
#define MAX_PENDING        20 // log2(maxSetSize) + density (#neighbors)
#define NOVELTY_PER_ROUND   1
#define ASK_PER_ROUND       1
#define HELP_PER_ROUND      2
#define ZAP_ROUND_LEN    4500

/* packet format:

  n 32B 32B? 32B?  // 33 bytes, in the future up to two additional keys
  c 32B 32B 32B B  // 98 bytes
    lo  hi  xor cnt
  z 32B(nonce)     // 33 bytes
*/

// forward declaration
void set_want_dmx();

// -------------------------------------------------------------------------

unsigned char goset_dmx[DMX_LEN];
struct goset_s *theGOset;

struct claim_s {
  unsigned char typ;
  unsigned char lo[GOSET_KEY_LEN];
  unsigned char hi[GOSET_KEY_LEN];
  unsigned char xo[GOSET_KEY_LEN];
  unsigned char cnt;
};

struct novelty_s {
  unsigned char typ;
  unsigned char key[GOSET_KEY_LEN];
};

struct zap_s {
  unsigned char typ;
  unsigned char key[GOSET_KEY_LEN];
  int32_t ndx; // -1: zap everything, else: index of entry to zap
};

#define NOVELTY_LEN sizeof(struct novelty_s)
#define CLAIM_LEN   sizeof(struct claim_s)
#define ZAP_LEN     sizeof(struct zap_s)

struct goset_s {
  // unsigned short version; ??
  const char *fname;
  int goset_len; // number of set elements
  int largest_claim_span;
  unsigned char goset_state[GOSET_KEY_LEN];
  unsigned char goset_keys[GOSET_KEY_LEN * GOSET_MAX_KEYS];
  struct claim_s pending_claims[MAX_PENDING];
  struct novelty_s pending_novelty[MAX_PENDING];
  int pending_c_cnt, pending_n_cnt;
  int novelty_credit;
  unsigned long next_round; // FIXME: handle, or test, wraparound
  struct zap_s zap;
  unsigned long zap_state;
  unsigned long zap_next;
};

int _cmp_cnt(const void *a, const void *b)
{
  return ((struct claim_s*)a)->cnt - ((struct claim_s*)b)->cnt;
}

int _cmp_key32(const void *a, const void *b)
{
  return memcmp(a,b,GOSET_KEY_LEN);
}

unsigned char* _xor(struct goset_s *gp, int lo, int hi)
{
  static unsigned char sum[GOSET_KEY_LEN];
  cpu_set_fast();
  memset(sum, 0, GOSET_KEY_LEN);
  unsigned char *cp = gp->goset_keys + lo*GOSET_KEY_LEN;
  for (int i = lo; i <= hi; i++)
    for (int j = 0; j < GOSET_KEY_LEN; j++)
      sum[j] ^= *cp++;
  cpu_set_slow();
  return sum;  
}

int isZero(unsigned char *h, int len)
{
  static unsigned char zero[GOSET_KEY_LEN];
  return !memcmp(zero, h, len);
}

unsigned char* _mkNovelty(unsigned char *start, int cnt)
{
  static unsigned char pkt[NOVELTY_LEN];
  pkt[0] = 'n';
  memcpy(pkt+1, start, GOSET_KEY_LEN);
  return pkt;
}

unsigned char* _mkClaim(struct goset_s *gp, int lo, int hi)
{
  static struct claim_s claim;
  claim.typ = 'c';
  memcpy(claim.lo, gp->goset_keys + lo*GOSET_KEY_LEN, GOSET_KEY_LEN);
  memcpy(claim.hi, gp->goset_keys + hi*GOSET_KEY_LEN, GOSET_KEY_LEN);
  memcpy(claim.xo, _xor(gp, lo, hi), GOSET_KEY_LEN);
  claim.cnt = hi - lo + 1;
  return (unsigned char*) &claim;
}

unsigned char* _mkZap(struct goset_s *gp) // fill buffer with zap packet
{
  static unsigned char pkt[DMX_LEN + ZAP_LEN];
  memcpy(pkt, goset_dmx, DMX_LEN);
  memcpy(pkt + DMX_LEN, &gp->zap, ZAP_LEN);
  return pkt;
}

int _key_index(struct goset_s *gp, unsigned char *key)
{
  for (int i = 0; i < gp->goset_len; i++)
    if (!memcmp(gp->goset_keys + i*GOSET_KEY_LEN, key, GOSET_KEY_LEN))
      return i;
  Serial.println("** BUG: index negative for " + String(to_hex(key, GOSET_KEY_LEN)));
  return -1;
}

void _add_pending(struct goset_s *gp, unsigned char *claim)
{
  if (gp, gp->pending_c_cnt >= MAX_PENDING)
    return;
  for (int i = 0; i < gp->pending_c_cnt; i++)
    if (!memcmp(gp->pending_claims + i, claim, CLAIM_LEN))
      return;
  // Serial.println(String("  adding claim ") + to_hex(claim+65,32));
  memcpy(gp->pending_claims + gp->pending_c_cnt, claim, CLAIM_LEN);
  gp->pending_c_cnt++;
}

// ----------------------------------------------------------------------------

struct goset_s* goset_new()
{
  struct goset_s *gp = (struct goset_s*) calloc(1, sizeof(struct goset_s));
  gp->novelty_credit = NOVELTY_PER_ROUND;
  return gp;
}

void goset_dump(struct goset_s *gp)
{
  Serial.printf("GOset: %d keys\n", gp->goset_len);
  for (int i = 0; i < gp->goset_len; i++) {
    Serial.printf("  %2d %s\n", i,
                  to_hex(gp->goset_keys + i * GOSET_KEY_LEN, GOSET_KEY_LEN));
  }
}

void goset_add(struct goset_s *gp, unsigned char *key)
{
  if (isZero(key, GOSET_KEY_LEN))
    return;
  for (int i = 0; i < gp->goset_len; i++)
    if (!memcmp(gp->goset_keys+i*GOSET_KEY_LEN, key, GOSET_KEY_LEN)) {
      // Serial.println(String("key already in set: ") + to_hex(key, GOSET_KEY_LEN));
      return;
    }
  if (gp->goset_len >= GOSET_MAX_KEYS) {
    Serial.printf("  too many keys: %d\n", gp->goset_len);
    return;
  }
  repo_new_feed(key);

  memcpy(gp->goset_keys + gp->goset_len * GOSET_KEY_LEN, key, GOSET_KEY_LEN);
  gp->goset_len++;
  qsort(gp->goset_keys, gp->goset_len, GOSET_KEY_LEN, _cmp_key32);

  if (gp->goset_len >= gp->largest_claim_span) { // only rebroadcast if we are up to date
    if (gp->novelty_credit-- > 0)
      io_enqueue(_mkNovelty(key, 1), NOVELTY_LEN, goset_dmx, NULL);
    else {
      if (gp->pending_n_cnt < MAX_PENDING)
        memcpy(gp->pending_novelty + gp->pending_n_cnt++, _mkNovelty(key, 1), NOVELTY_LEN);
    }
  }

  Serial.printf("added key %s, len=%d\n", to_hex(key, GOSET_KEY_LEN), gp->goset_len);
}

void goset_rx(unsigned char *pkt, int len, unsigned char *aux)
{
  // struct face_s *f,
  struct goset_s *gp = theGOset;
  pkt += DMX_LEN;
  len -= DMX_LEN;

  if (pkt[0] == 'n' && len == NOVELTY_LEN) {
    Serial.printf("goset_rx t=n %s\n", to_hex(pkt+1, GOSET_KEY_LEN));
    goset_add(gp, pkt+1);
    return;
  }
  if (pkt[0] == 'z' && len == ZAP_LEN) {
    Serial.println("goset_rx t=z");
    unsigned long now = millis();
    if (gp->zap_state == 0) {
      Serial.println("  ZAP phase I starts");
      memcpy(&gp->zap, pkt, ZAP_LEN);
      gp->zap_state = now + ZAP_ROUND_LEN;
      gp->zap_next = now;
    }
    return;
  }
  if (pkt[0] != 'c' || len != CLAIM_LEN) {
    Serial.printf("goset_rx t=%c ??\n", pkt[0]);
    return;
  }
  struct claim_s *cp = (struct claim_s *) pkt;
  Serial.printf("goset_rx t=c, xo=%s, |span|=%d\n", to_hex(cp->xo, GOSET_KEY_LEN), cp->cnt);
  if (isZero(cp->lo, GOSET_KEY_LEN)) // remove this clause
    return;
  if (cp->cnt > gp->largest_claim_span)
    gp->largest_claim_span = cp->cnt;
  goset_add(gp, cp->lo);
  goset_add(gp, cp->hi);
  _add_pending(gp, pkt);
}

void goset_tick(struct goset_s *gp)
{
  unsigned long now = millis();
  if (gp->zap_state != 0) {
    if (now > gp->zap_state + ZAP_ROUND_LEN) { // two rounds after zap started
      int ndx = ntohl(gp->zap.ndx);
      Serial.println("ZAP phase II ended, resetting now, ndx=" + String(ndx));
      if (ndx == -1)
        repo_reset();
      else {
        char path[90];
        unsigned char *fid = gp->goset_keys + ndx * FID_LEN;
        sprintf(path, "%s/%s", FEED_DIR, to_hex(fid, FID_LEN));
        repo_reset(path);
      }
    }
    if (now < gp->zap_state && now > gp->zap_next) { // phase I
      Serial.printf("  sending zap message (%d bytes)\n", DMX_LEN + ZAP_LEN);
      io_send(_mkZap(gp), DMX_LEN + ZAP_LEN, NULL);
      gp->zap_next = now + 1000;
    }
  }
  if (now < gp->next_round && (gp->next_round-now) < 2*GOSET_ROUND_LEN) // FIXME: test whether wraparound works
    return;
  gp->next_round = now + GOSET_ROUND_LEN + esp_random() % 1000;

  if (gp->goset_len == 0)
    return;
  if (gp->goset_len == 1) { // can't send a claim, send the one key as novelty
    io_enqueue(_mkNovelty(gp->goset_keys, 1), NOVELTY_LEN, goset_dmx, NULL);
    return;
  }

  while (gp->novelty_credit-- > 0 && gp->pending_n_cnt > 0) {
    io_enqueue((unsigned char*) gp->pending_novelty, NOVELTY_LEN, goset_dmx, NULL);
    memmove(gp->pending_novelty, gp->pending_novelty+1, NOVELTY_LEN * --gp->pending_n_cnt);
  }
  gp->novelty_credit = NOVELTY_PER_ROUND;
  unsigned char *claim = _mkClaim(gp, 0, gp->goset_len-1);
  if (memcmp(gp->goset_state, claim+65, GOSET_KEY_LEN)) { // GOset changed
    memcpy(gp->goset_state, claim+65, GOSET_KEY_LEN);
    set_want_dmx();
  }
  io_enqueue(claim, CLAIM_LEN, goset_dmx, NULL);
  
  // sort pending entries, smallest first
  qsort(gp->pending_claims, gp->pending_c_cnt, CLAIM_LEN, _cmp_cnt);
  int max_ask = ASK_PER_ROUND;
  int max_help = HELP_PER_ROUND;

  struct claim_s *cp = gp->pending_claims;
  for (int i = 0; i < gp->pending_c_cnt; i++) {
    int lo = _key_index(gp, cp->lo);
    int hi = _key_index(gp, cp->hi);
    struct claim_s *partial = (struct claim_s*) _mkClaim(gp, lo, hi);

    if (cp->cnt == 0 || lo < 0 || hi < 0 || lo > hi // eliminate claims that match or are bogous
        // || !memcmp(partial->xo, gp->goset_state, GOSET_KEY_LEN)
        || !memcmp(partial->xo, cp->xo, GOSET_KEY_LEN) ) {
      memmove(cp, cp+1, (gp->pending_c_cnt - i - 1)*CLAIM_LEN);
      gp->pending_c_cnt--;
      i--;
      continue;
    } 
    // Serial.println("  not eliminated " + String(lo,DEC) + " " + String(hi,DEC) + " " + String(cp->cnt));
    if (partial->cnt <= cp->cnt) { // ask for help, but only for smallest entry, and only once in this round
      if (max_ask-- > 0) {
        Serial.print("  asking for help " + String(partial->cnt));
        Serial.print(String(" ") + to_hex(partial->lo,4) + String(".."));
        Serial.println(String(" ") + to_hex(partial->hi,4) + String(".."));
        io_enqueue((unsigned char*) partial, CLAIM_LEN, goset_dmx, NULL);
      }
      if (partial->cnt < cp->cnt) {
        cp++;
        // memmove(cp, cp+1, (gp->pending_c_cnt - i - 1)*CLAIM_LEN); // remove this claim we reacted on
        // gp->pending_c_cnt--;
        // i--;
        // // do not help if we have holes ...
        // max_help = 0;
        continue;
      }
    }
    if (max_help-- > 0) { // we have larger claim span, offer help (but limit # of claims)
      hi--, lo++;
      Serial.print("  offer help span=" + String(partial->cnt - 2));
      Serial.print(String(" ") + to_hex(gp->goset_keys+lo*GOSET_KEY_LEN,4) + String(".."));
      Serial.println(String(" ") + to_hex(gp->goset_keys+hi*GOSET_KEY_LEN,4) + String(".."));
      if (hi <= lo)
        io_enqueue(_mkNovelty(gp->goset_keys+lo*GOSET_KEY_LEN, 1), NOVELTY_LEN, goset_dmx, NULL);
      else if (hi - lo <= 2) // span of 2 or 3
        io_enqueue(_mkClaim(gp, lo, hi), CLAIM_LEN, goset_dmx, NULL);
      else { // split span in two intervals
        int sz = (hi+1 - lo) / 2;
        io_enqueue(_mkClaim(gp, lo, lo+sz-1), CLAIM_LEN, goset_dmx, NULL);
        io_enqueue(_mkClaim(gp, lo+sz, hi), CLAIM_LEN, goset_dmx, NULL);
      }
      memmove(cp, cp+1, (gp->pending_c_cnt - i - 1)*CLAIM_LEN);
      gp->pending_c_cnt--;
      i--;
      continue;
    }
    cp++;
    // option: trim pending claims?
    // gp->pending_c_cnt = i+1;
    // break;
  }
  // make room for new claims
  if (gp->pending_c_cnt > (MAX_PENDING-5))
    gp->pending_c_cnt = MAX_PENDING-5;
  // if (gp->pending_c_cnt > 2)
  //   gp->pending_c_cnt = 2; // better: log2(largest_claim_span) ?
  if (gp->pending_c_cnt > 0)
    Serial.printf("  |GOset|=%d, %d pending claims", gp->goset_len, gp->pending_c_cnt);
  // unsigned char *heap = reinterpret_cast<unsigned char*>(sbrk(0));
  // Serial.println(String(", heap sbrk=") + to_hex((unsigned char *)&heap, sizeof(heap)));
  for (int i = 0; i < gp->pending_c_cnt; i++)
    Serial.printf("  xor=%s, |span|=%d\n", to_hex(gp->pending_claims[i].xo,32), gp->pending_claims[i].cnt);
}

void goset_zap(struct goset_s *gp, int ndx)
{
  unsigned long now = millis();
  gp->zap.typ = 'z';
  gp->zap.ndx = htonl(ndx);
  for (int i=0; i < sizeof(gp->zap.key)/sizeof(uint32_t); i++) {
    uint32_t r = esp_random();
    memcpy(gp->zap.key + 4*i, (unsigned char*) &r, sizeof(uint32_t));
  }
  gp->zap_state = now + ZAP_ROUND_LEN;
  gp->zap_next = now + 1000;
  io_send(_mkZap(gp), DMX_LEN + ZAP_LEN, NULL);
}

// eof
