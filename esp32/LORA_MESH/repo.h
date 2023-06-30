// repo.h

// tinySSB for ESP32
// (c) 2022-2023 <christian.tschudin@unibas.ch>

#if !defined(_INCLUDE_REPO_H)
#define _INCLUDE_REPO_H

struct feed_s {
  unsigned char fid[FID_LEN];
  int next_seq;
  unsigned char prev[HASH_LEN]; // hash of previous log entry
  int max_prev_seq;
};


class RepoClass {

public:
  void           clean(char *path); // recursively rm files IN this dir
  void           reset(char *path); // as above, remove the dir and reboot
  void           load();
  void           new_feed(unsigned char *fid);
  unsigned char* feed_read(unsigned char *fid, int seq);
  unsigned char* feed_read_chunk(unsigned char *fid, int seq, int cnr);
  int            feed_len(unsigned char *fid);
  void           feed_append(unsigned char *fid, unsigned char *pkt);
  int            feed_index(unsigned char* fid);
  void           sidechain_append(unsigned char *buf, int blbt_ndx);
  struct feed_s* fid2feed(unsigned char* fid);
  char*          _feed_chnk(unsigned char *fid, int seq, bool complete);
  char*          _feed_log(unsigned char *fid);

  struct feed_s feeds[MAX_FEEDS];
  int feed_cnt;
  int entry_cnt;
  int chunk_cnt;
  
private:
  char* _feed_path(unsigned char *fid);
  char* _feed_mid(unsigned char *fid, int seq);
};

#endif

// eof
