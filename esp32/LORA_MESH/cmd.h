// cmd.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

void cmd_rx(String cmd) {
  cmd.toLowerCase();
  cmd.trim();
  Serial.printf("CMD %s\r\n\n", cmd.c_str());
  switch(cmd[0]) {
    case '?':
      Serial.println("  ?        help");
      Serial.println("  a        add new random key");
      Serial.println("  b+[id|*] turn beacon on on id/all");
      Serial.println("  b-[id|*] turn beacon off on id/all");
      Serial.println("  c        remove log files for fcnt & fcnt-table (local dev only!)");
      Serial.println("  d        dump DMXT and CHKT");
      Serial.println("  f        list file system");
      Serial.println("  g        dump GOset");
      Serial.println("  k+<key>  add new key (globally)");
      Serial.println("  k-<key>  remove key (globally)");
#if defined(LORA_LOG)
      Serial.println("  l        list log file");
      Serial.println("  m        empty log file");
#endif
      Serial.println("  q[0|1]   analyze (0) and fix (1) file system");
      Serial.println("  r[id|*]  reset this repo to blank / request reset from id/all");
      Serial.println("  s[id|*]  status / request status from id/all");
      Serial.println("  x[id|*]  reboot / request reboot from id/all");
      Serial.println("  z[N]     zap (feed with index N) on all nodes");
      break;
    case 'a': { // inject new key
      unsigned char key[GOSET_KEY_LEN];
      for (int i=0; i < sizeof(key)/4; i++) {
        unsigned int r = esp_random();
        memcpy(key + 4*i, (unsigned char*) &r, sizeof(4));
      }
      theGOset->add(key);
      theGOset->dump();
      break;
    }
    case 'b': // beacon
      if (!(cmd[1] == '+' or cmd[1] == '-')) { Serial.printf("invalid command: %s\n", cmd[1]); break; }
      if (cmd[2] == '*') {
        Serial.printf("sending request to turn %s beacon to all nodes\r\n", cmd[1] == '+' ? "on" : "off");
        mgmt_send_request(cmd[1]);
      } else if (cmd.length() == 2 * MGMT_ID_LEN + 2) {
	char idHex[2 * MGMT_ID_LEN];
	for (int i = 0; i < 2 * MGMT_ID_LEN; i++) { idHex[i] = cmd[i+2]; }
	unsigned char *id = from_hex(idHex, MGMT_ID_LEN);
        Serial.printf("sending request to turn %s beacon to %s\r\n", cmd[1] == '+' ? "on" : "off", to_hex(id, MGMT_ID_LEN, 0));
        mgmt_send_request(cmd[1], id);
      }
      break;
    case 'c': // fcnt
      Serial.println("Deleting logs of fcnt & fcnt-table...");
      MyFS.remove(MGMT_FCNT_LOG_FILENAME);
      MyFS.remove(MGMT_FCNT_TABLE_LOG_FILENAME);
      esp_restart();
      break;
    case 'd': // dump
      // goset_dump(theGOset);
      Serial.println("Installed feeds:");
      for (int i = 0; i < repo->feed_cnt; i++) {
        unsigned char *key = theGOset->get_key(i);
        Serial.printf("  %d %s, next_seq=%d\r\n", i, to_hex(key, 32, 0), repo->fid2feed(key)->next_seq);
      }
      Serial.println("DMX table:");
      for (int i = 0; i < dmx->dmxt_cnt; i++)
        Serial.printf("  %s\r\n", to_hex(dmx->dmxt[i].dmx, DMX_LEN, 0));
      Serial.println("CHUNK table:");
      for (int i = 0; i < dmx->blbt_cnt; i++)
        Serial.printf("  %s %d.%d.%d\r\n", to_hex(dmx->blbt[i].h, HASH_LEN, 0),
                      theGOset->_key_index(dmx->blbt[i].fid), dmx->blbt[i].seq, dmx->blbt[i].bnr);
      break;
    case 'f': // Directory dump
      Serial.printf("File system: %d total bytes, %d used\r\n",
                    MyFS.totalBytes(), MyFS.usedBytes());
      listDir(MyFS, FEED_DIR, 2);
      break;
    case 'g': // GOset dump
      Serial.printf("GOset: %d entries\r\n", theGOset->goset_len);
      for (int i = 0; i < theGOset->goset_len; i++)
        Serial.printf("%2d %s\r\n", i,
                      to_hex(theGOset->get_key(i), GOSET_KEY_LEN, 0));
      break;
    case 'k': { // allow/deny key
      if (cmd.length() != 2 * GOSET_KEY_LEN + 2) { Serial.printf("invalid key length\r\n"); break; }
      if (!(cmd[1] == '+' or cmd[1] == '-')) { Serial.printf("invalid command: %s\r\n", cmd[1]); break; }
      char keyHex[2 * GOSET_KEY_LEN];
      for (int i = 0; i < 2 * GOSET_KEY_LEN; i++) { keyHex[i] = cmd[i+2]; }
      unsigned char *key = from_hex(keyHex, GOSET_KEY_LEN);
      Serial.printf("sending request to %s key %s globally\r\n", cmd[1] == '+' ? "allow" : "deny", to_hex(key, GOSET_KEY_LEN, 0));
      mgmt_send_key(cmd[1] == '+' ? true : false, key);
      break;
    }
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
    case 'q':
      { // analyze file system
        char doit = cmd[1] == '1';
        File fdir = MyFS.open(FEED_DIR);
        File g = fdir.openNextFile("r");
        while (g) { // walk each feed
          unsigned char *fid = from_hex((char*)g.name(), FID_LEN);
          // this code should not be necessary to run anymore because we
          // handle now the case that a crash occurs between extending the log
          // and creating the sidechan file (which we now do first)
          Serial.printf(" check feed %s\r\n", g.name());
          unsigned char buf[TINYSSB_PKT_LEN];
          char *path = repo->_feed_log(fid);
          File f = MyFS.open(path, "r");
          int i = 0, sz;
          while(-1) { // walk each log entry
            int sz = f.read(buf, sizeof(buf));
            i++;
            if (sz != TINYSSB_PKT_LEN)
              break;
            if (buf[DMX_LEN] != PKTTYPE_chain20) {
              path = repo->_feed_chnk(fid, i, 1);
              if (MyFS.exists(path)) {
                Serial.printf(" !! sidechain file %s should NOT exist (1)\r\n", path);
                if (doit) {
                  Serial.printf("    removing sidechain file %s\r\n", path);
                  MyFS.remove(path);
                }
              }
              path = repo->_feed_chnk(fid, i, 0);
              if (MyFS.exists(path)) {
                Serial.printf(" !! sidechain file %s should NOT exist (2)\r\n", path);
                if (doit) {
                  Serial.printf("    removing sidechain file %s\r\n", path);
                  MyFS.remove(path);
                }
              }
              continue;
            }
            // read length of content
            int len = 4; // max lenght of content length field
            sz = bipf_varint_decode(buf, DMX_LEN+1, &len);
            // Serial.printf("   sidechain will have %d bytes\r\n", sz);
            if (sz > (48 - HASH_LEN - len)) { // entry HAS/SHOULD have a sidechain file
              path = repo->_feed_chnk(fid, i, 0);
              if (MyFS.exists(path)) {
                File h = MyFS.open(path, FILE_READ);
                int sz2 = h.size();
                h.close();
                if ((sz2 / TINYSSB_PKT_LEN) != ((sz - (48 - HASH_LEN - len) + TINYSSB_SCC_LEN-1)/ TINYSSB_SCC_LEN)) {
                  Serial.printf(" !! sidechain size mismatch %s: sz=%d, len=%d\r\n", path, sz, sz2);
                  if (doit) {
                    Serial.printf("    removing sidechain file %s\r\n", path);
                    MyFS.remove(path);
                    // esp_restart(); // because chunk count got changed
                  }
                }
                continue;
                // FIXME: should also check whether length is OK, and no !nn file exists
              }
              path = repo->_feed_chnk(fid, i, 1);
              if (MyFS.exists(path))
                continue;
                // FIXME: should also check whether length already exceeds sz
              if (doit) {
                Serial.printf(" !! creating the missing sidechain file %s\r\n", path);
                MyFS.open(path, FILE_WRITE).close();
              } else
                Serial.printf(" !! missing sidechain file %s\r\n", path);
            } else { // encoding error, no sidechain file should exist for this small content
              path = repo->_feed_chnk(fid, i, 1);
              if (MyFS.exists(path)) {
                Serial.printf(" !! sidechain file %s should NOT exist (3)\r\n", path);
                if (doit) {
                  Serial.printf("    removing sidechain file %s\r\n", path);
                  MyFS.remove(path);
                }
              }
              path = repo->_feed_chnk(fid, i, 0);
              if (MyFS.exists(path)) {
                Serial.printf(" !! sidechain file %s should NOT exist (4)\r\n", path);
                if (doit) {
                  Serial.printf("    removing sidechain file %s\r\n", path);
                  MyFS.remove(path);
                }
              }
            }
          }
          f.close();
          g = fdir.openNextFile();
        }
        fdir.close();
      }
      break;
    case 'r': // reset
      if (cmd[1] == '*') {
        Serial.printf("sending reset request to all nodes\r\n");
        mgmt_send_request('r');
      } else if (cmd.length() == 2 * MGMT_ID_LEN + 1) {
	char idHex[2 * MGMT_ID_LEN];
	for (int i = 0; i < 2 * MGMT_ID_LEN; i++) { idHex[i] = cmd[i+1]; }
	unsigned char *id = from_hex(idHex, MGMT_ID_LEN);
        Serial.printf("sending reset request to %s\r\n", to_hex(id, MGMT_ID_LEN, 0));
        mgmt_send_request('r', id);
      } else {
        repo->reset(NULL);
        Serial.println("reset done");
      }
      break;
    case 's': // send status request
      if (cmd[1] == '*') {
        Serial.printf("sending status request to all nodes\r\n");
        mgmt_send_request('s');
      } else if (cmd.length() == 2 * MGMT_ID_LEN + 1) {
	char idHex[2 * MGMT_ID_LEN];
	for (int i = 0; i < 2 * MGMT_ID_LEN; i++) { idHex[i] = cmd[i+1]; }
	unsigned char *id = from_hex(idHex, MGMT_ID_LEN);
        Serial.printf("sending status request to %s\r\n", to_hex(id, MGMT_ID_LEN, 0));
        mgmt_send_request('s', id);
      } else {
        Serial.println("printing status ...\n");
        mgmt_print_statust();
      }
      break;
    case 'x': // reboot
      if (cmd[1] == '*') {
        Serial.printf("sending reboot request to all nodes\r\n");
        mgmt_send_request('x');
      } else if (cmd.length() == 2 * MGMT_ID_LEN + 1) {
	char idHex[2 * MGMT_ID_LEN];
	for (int i = 0; i < 2 * MGMT_ID_LEN; i++) { idHex[i] = cmd[i+1]; }
	unsigned char *id = from_hex(idHex, MGMT_ID_LEN);
        Serial.printf("sending reboot request to %s\r\n", to_hex(id, MGMT_ID_LEN, 0));
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
      Serial.println("zap1");
      theGOset->do_zap(ndx);
      Serial.println("zap2");
      Serial.println();
      break;
    }
    default:
      Serial.println("unknown command");
      break;
  }
  Serial.println();
}
