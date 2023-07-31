// config.h

#if !defined(_INCLUDE_CONFIG_H)
#define _INCLUDE_CONFIG_H

#include "bipf.h"

struct lora_config_s {
  char plan[8];        // name of frequency plan, ASCIIZ
  unsigned long  fr;   // in Hz
  unsigned int   bw;   // bandwidth, in Hz
  unsigned short sf;   // spreading factor (7-12)
  unsigned short cr;   // coding rate (5-8)
  unsigned char  sw;   // sync word
  unsigned char  tx;   // tx power
};

extern struct lora_config_s lora_configs[];
extern short lora_configs_size;

// ---------------------------------------------------------------------------

// the "config.bipf: file retains persisted config choices as a dict
// currently used:

// {
//   "lora_plan": "US915", // i.e., the lora config is specified by plan name
// }

#define CONFIG_FILENAME "config.bipf"

struct bipf_s* config_load(); // returns a BIPF dict with the persisted config dict
void           config_save(struct bipf_s *dict); // persist the BIPF dict

#endif // _INCLUDE_CONFIG_H
