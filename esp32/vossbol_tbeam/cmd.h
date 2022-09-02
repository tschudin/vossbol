// cmd.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

void cmd_rx(String cmd) {
  cmd.toLowerCase();
  cmd.trim();
  Serial.println(String("CMD ") + cmd); 
  switch(cmd[0]) {
    case '?':
      Serial.println("  ?  help");
      Serial.println("  a  add new random key");
      Serial.println("  d  dump DMXT and CHKT");
      Serial.println("  f  list file system");
      Serial.println("  r  reset this repo");
      Serial.println("  x  reboot");
      Serial.println("  z  zap (reset also remote nodes)");
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
    case 'd': // dump
      // goset_dump(theGOset);
      Serial.println("Installed feeds:");
      for (int i = 0; i < feed_cnt; i++) {
        unsigned char *key = theGOset->goset_keys + i*FID_LEN;
        Serial.println(String("  ") + String(i) + " " + to_hex(key, 32)
                       + ", next_seq=" + String(fid2feed(key)->next_seq));
      }
      Serial.println("DMX table:");
      for (int i = 0; i < dmxt_cnt; i++)
        Serial.println(String("  ") + to_hex(dmxt[i].dmx, DMX_LEN));
      Serial.println("CHK table:");
      for (int i = 0; i < blbt_cnt; i++)
        Serial.println(String("  ") + to_hex(blbt[i].h, HASH_LEN) + " "
                       + String(_key_index(theGOset, blbt[i].fid))
                       + "." + String(blbt[i].seq) + "." + String(blbt[i].bnr));
      Serial.println();
      break;
    case 'f': // Directory dump
      Serial.println("\nFile system: " + String(MyFS.totalBytes(), DEC) + " total bytes, "
                                 + String(MyFS.usedBytes(), DEC) + " used");
      listDir(MyFS, FEED_DIR, 2);
      break;
    case 'r': // reset
      repo_reset();
      Serial.println("reset done");
      break;
    case 'x': // reboot
      Serial.println("rebooting ...\n");
      esp_restart();  
    case 'z': // zap
      Serial.println("zap protocol started ...\n");
      goset_zap(theGOset);
      break;
    default:
      Serial.println("unknown command");
      break;
  }
}
