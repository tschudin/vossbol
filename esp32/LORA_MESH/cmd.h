// cmd.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

void cmd_rx(String cmd) {
  cmd.toLowerCase();
  cmd.trim();
  Serial.printf("CMD %s\n\n", cmd.c_str()); 
  switch(cmd[0]) {
    case '?':
      Serial.println("  ?        help");
      Serial.println("  a        add new random key");
      Serial.println("  b+[id|*] turn beacon on on id/all (1 hop)"); // TODO make all global
      Serial.println("  b-[id|*] turn beacon off on id/all (1 hop)"); // TODO same
      Serial.println("  d        dump DMXT and CHKT");
      Serial.println("  f        list file system");
#if defined(LORA_LOG)
      Serial.println("  l        list log file");
      Serial.println("  m        empty log file");
#endif
      Serial.println("  r        reset this repo to blank");
      Serial.println("  s[id|*]  status / request status from id/all (1 hop)");
      Serial.println("  x[id|*]  reboot / request reboot from id/all (1 hop)");
      Serial.println("  z[N]     zap (feed with index N) on all nodes (global)");
      break;
    case 'a': { // inject new key
      unsigned char key[GOSET_KEY_LEN];
      for (int i=0; i < sizeof(key)/4; i++) {
        unsigned int r = esp_random();
        memcpy(key + 4*i, (unsigned char*) &r, sizeof(4));
      }
      goset_add(theGOset, key);
      goset_dump(theGOset);
      break;
    }
    case 'b': // beacon
      if (!(cmd[1] == '+' or cmd[1] == '-')) { Serial.printf("invalid command: %s\n", cmd[1]); break; }
      if (cmd[2] == '*') {
        Serial.printf("sending request to turn %s beacon to all nodes\n", cmd[1] == '+' ? "on" : "off");
        mgmt_send_request(cmd[1]);
      } else if (cmd.length() == 2 * MGMT_ID_LEN + 2) {
	char idHex[2 * MGMT_ID_LEN];
	for (int i = 0; i < 2 * MGMT_ID_LEN; i++) { idHex[i] = cmd[i+2]; }
	unsigned char *id = from_hex(idHex, 4);
        Serial.printf("sending request to turn %s beacon to %s\n", cmd[1] == '+' ? "on" : "off", to_hex(id, MGMT_ID_LEN, 0));
        mgmt_send_request(cmd[1], id);
      }
      break;
    case 'd': // dump
      // goset_dump(theGOset);
      Serial.println("Installed feeds:");
      for (int i = 0; i < feed_cnt; i++) {
        unsigned char *key = theGOset->goset_keys + i*FID_LEN;
        Serial.printf("  %d %s, next_seq=%d\n", i, to_hex(key, 32), fid2feed(key)->next_seq);
      }
      Serial.println("DMX table:");
      for (int i = 0; i < dmxt_cnt; i++)
        Serial.printf("  %s\n", to_hex(dmxt[i].dmx, DMX_LEN));
      Serial.println("CHUNK table:");
      for (int i = 0; i < blbt_cnt; i++)
        Serial.printf("  %s %d.%d.%d\n", to_hex(blbt[i].h, HASH_LEN),
                      _key_index(theGOset, blbt[i].fid), blbt[i].seq, blbt[i].bnr);
      break;
    case 'f': // Directory dump
      Serial.printf("File system: %d total bytes, %d used\n",
                    MyFS.totalBytes(), MyFS.usedBytes());
      listDir(MyFS, FEED_DIR, 2);
      break;
#if defined(LORA_LOG)
  case 'l': // list Log file
      lora_log.close();
      lora_log = MyFS.open("/lora_log.txt", FILE_READ);
      while (lora_log.available()) {
        Serial.write(lora_log.read());
      }
      lora_log.close();
      lora_log = MyFS.open("/lora_log.txt", FILE_APPEND);
      break;
  case 'm': // empty Log file
      lora_log.close();
      lora_log = MyFS.open("/lora_log.txt", FILE_WRITE);
      break;
#endif
    case 'r': // reset
      repo_reset();
      Serial.println("reset done");
      break;
    case 's': // send status request
      if (cmd[1] == '*') {
        Serial.printf("sending status request to all reachable (1 hop) nodes\n");
        mgmt_send_request('s');
      } else if (cmd.length() == 2 * MGMT_ID_LEN + 1) {
	char idHex[2 * MGMT_ID_LEN];
	for (int i = 0; i < 2 * MGMT_ID_LEN; i++) { idHex[i] = cmd[i+1]; }
	unsigned char *id = from_hex(idHex, 4);
        Serial.printf("sending status request to %s\n", to_hex(id, MGMT_ID_LEN, 0));
        mgmt_send_request('s', id);
      } else {
        Serial.println("printing status ...\n");
        mgmt_print_statust();
      }
      break;
    case 'x': // reboot
      if (cmd[1] == '*') {
        Serial.printf("sending reboot request to all reachable (1 hop) nodes\n");
        mgmt_send_request('x');
      } else if (cmd.length() == 2 * MGMT_ID_LEN + 1) {
	char idHex[2 * MGMT_ID_LEN];
	for (int i = 0; i < 2 * MGMT_ID_LEN; i++) { idHex[i] = cmd[i+1]; }
	unsigned char *id = from_hex(idHex, 4);
        Serial.printf("sending reboot request to %s\n", to_hex(id, MGMT_ID_LEN, 0));
        mgmt_send_request('x', id);
      } else {
        Serial.println("rebooting ...\n");
        esp_restart();
      }
      break;
    case 'z': { // zap
      Serial.println("zap protocol started ...\n");
      int ndx = -1;
      if (cmd[1] != '\0')
        ndx = atoi(cmd.c_str()+1);
      goset_zap(theGOset, ndx);
      break;
    }
    default:
      Serial.println("unknown command");
      break;
  }
  Serial.println();
}
