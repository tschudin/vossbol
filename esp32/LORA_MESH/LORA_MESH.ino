// vossbol_tbeam.ino

// tinySSB for ESP32
// 2022-08-09 <christian.tschudin@unibas.ch>


// #define NO_BLE   // disable Bluetooth Low Energy
// #define NO_BT    // disable Bluetooth
// #define NO_GPS   // disable GPS
// #define NO_LORA  // disable LoRa
// #define NO_OLED  // disable OLED
#define NO_WIFI  // disable WiFi

#define SLOW_CPU
#define SLOW_CPU_MHZ 80  // 40MHz is too low for handling the BLE protocol

#define LOG_FLUSH_INTERVAL         10000 // millis
#define LOG_BATTERY_INTERVAL  15*60*1000 // millis (15 minutes)

#define MGMT_SEND_STATUS_INTERVAL  5*60*1000 // millis (5 minutes)
#define MGMT_SEND_BEACON_INTERVAL     5*1000 // millis (5 seconds)

// ----------------------------------------------------------------------

// #define LORA_BAND    902E6 // USA
#define LORA_BAND  865.5E6 // Europe
#define LORA_BW     125000
#define LORA_SF          7
#define LORA_CR          5
#define LORA_TXPOWER    20 // highpowermode, otherwise choose 17 or lower
#define LORA_SYNC_WORD  0x58 // for "SB, Scuttlebutt". Discussion at https://blog.classycode.com/lora-sync-word-compatibility-between-sx127x-and-sx126x-460324d1787a

#define LORA_LOG // enable macro for logging received pkts
#define LORA_LOG_FILENAME  "/lora_log.txt"

#define FID_LEN         32
#define HASH_LEN        20
#define FID_HEX_LEN     (2*FID_LEN)
#define FID_B64_LEN     ((FID_LEN + 2) / 3 * 4)
#define FEED_DIR        "/feeds"
#define MAX_FEEDS      100 

#define DMX_LEN          7
#define GOSET_DMX_STR    "tinySSB-0.1 GOset 1"
#define MGMT_DMX_STR     "tinySSB-0.1 mgmt 1"

#define TINYSSB_PKT_LEN   120

#define tSSB_WIFI_SSID   "tinySSB"
#define tSSB_WIFI_PW     "dWeb2022"
#define tSSB_UDP_ADDR    "239.5.5.8"
#define tSSB_UDP_PORT    1558

#define PKTTYPE_plain48  '\x00' // single packet with 48B payload
#define PKTTYPE_chain20  '\x01' // start of hash sidechain (pkt contains BIPF-encoded content length)

/* Nordic UART
#define BLE_SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
*/

#define BLE_SERVICE_UUID           "6e400001-7646-4b5b-9a50-71becce51558"
#define BLE_CHARACTERISTIC_UUID_RX "6e400002-7646-4b5b-9a50-71becce51558"
#define BLE_CHARACTERISTIC_UUID_TX "6e400003-7646-4b5b-9a50-71becce51558"
#define BLE_CHARACTERISTIC_UUID_ST "6e400004-7646-4b5b-9a50-71becce51558"

#define BLE_RING_BUF_SIZE 3

// -----------------------------------------------------------------------------

char ssid[sizeof(tSSB_WIFI_SSID) + 6];
int wifi_clients = 0;
int ble_clients = 0;

/* FTP server would be neat:
// #define DEFAULT_STORAGE_TYPE_ESP32 STORAGE_LITTLEFS
#define DEFAULT_FTP_SERVER_NETWORK_TYPE_ESP32           NETWORK_ESP32
#define DEFAULT_STORAGE_TYPE_ESP32                      STORAGE_LITTLEFS
#include <FtpServer.h>
FtpServer ftpSrv;
*/

struct feed_s {
  unsigned char fid[FID_LEN];
  int next_seq;
  unsigned char prev[HASH_LEN]; // hash of previous log entry
  int max_prev_seq;
};
struct feed_s feeds[MAX_FEEDS];
int feed_cnt;

int feed_index(unsigned char* fid) {
  for (int i = 0; i < feed_cnt; i++)
    if (!memcmp(fid, feeds[i].fid, FID_LEN))
      return i;
  return -1;  
}

struct feed_s* fid2feed(unsigned char* fid) {
  int ndx = feed_index(fid);
  if (ndx < 0) return NULL;
  return feeds + ndx;
}

// forward definitions
char* to_hex(unsigned char* buf, int len, int add_colon=0);
void repo_new_feed(unsigned char* fid);
void repo_reset(char *path = (char*)FEED_DIR);
void incoming_pkt(unsigned char* buf, int len, unsigned char *fid, struct face_s *);
void incoming_chunk(unsigned char* buf, int len, int blbt_ndx, struct face_s *);
void incoming_want_request(unsigned char* buf, int len, unsigned char* aux, struct face_s *);
void incoming_chnk_request(unsigned char* buf, int len, unsigned char* aux, struct face_s *);
void ble_init();


// our own local code:
#include "bipf.h"
#include "kiss.h"
#include "hw_setup.h"
#include "io.h"
#include "goset.h"
#include "dmx.h"
#include "repo.h"
#include "node.h"
#include "ed25519.h"
#include "mgmt.h"

// char lora_line[80];
char time_line[80];
char loc_line[80];
char goset_line[80];
char refresh = 1;

int old_gps_sec, old_goset_c, old_goset_n, old_goset_len;
int old_repo_sum;
int lora_cnt = 0;
int lora_bad_crc = 0;

File lora_log;
unsigned long int next_log_flush;

unsigned long int next_mgmt_send_beacon;

#include "cmd.h"

// ----------------------------------------------------------------------------
void setup()
{
  hw_setup();

#if !defined(NO_OLED)
  theDisplay.setFont(ArialMT_Plain_16);
  theDisplay.drawString(0 , 0, "SSB.virt.lora.pub");
  theDisplay.setFont(ArialMT_Plain_10);
  theDisplay.drawString(0 , 18, __DATE__ " " __TIME__);
  int f = LORA_BAND / 10000;
  char fr[30];
  sprintf(fr, "%d.%02d MHz", f/100, f%100);
  theDisplay.setFont(ArialMT_Plain_24);
  theDisplay.drawString(0, 38, fr);

  theDisplay.display();
#endif
  
  io_init();

  Serial.printf("File system: %d total bytes, %d used\r\n",
                MyFS.totalBytes(), MyFS.usedBytes());
  MyFS.mkdir(FEED_DIR);
  listDir(MyFS, FEED_DIR, 0);
  // ftpSrv.begin(".",".");

  Serial.println();
  theGOset = goset_new();
  unsigned char h[32];
  crypto_hash_sha256(h, (unsigned char*) GOSET_DMX_STR, strlen(GOSET_DMX_STR));
  memcpy(goset_dmx, h, DMX_LEN);
  arm_dmx(goset_dmx, goset_rx, NULL);
  Serial.printf("listening for GOset protocol on %s\r\n", to_hex(goset_dmx, 7));

  // initialize mgmt_dmx
  crypto_hash_sha256(h, (unsigned char*) MGMT_DMX_STR, strlen(MGMT_DMX_STR));
  memcpy(mgmt_dmx, h, DMX_LEN);
  arm_dmx(mgmt_dmx, mgmt_rx, NULL);
  Serial.printf("listening for mgmt protocol on %s\r\n", to_hex(mgmt_dmx, DMX_LEN));

  repo_load();

  // strcpy(lora_line, "?");
  strcpy(time_line, "?");
  strcpy(loc_line, "?");
  strcpy(goset_line, "?");

  // load fcnt
  mgmt_fcnt_log = MyFS.open(MGMT_FCNT_LOG_FILENAME, "r");
  mgmt_fcnt_log.read((unsigned char*) &mgmt_fcnt, sizeof(mgmt_fcnt));
  mgmt_fcnt_log.close();
  // load fcnt-table
  mgmt_fcnt_table_log = MyFS.open(MGMT_FCNT_TABLE_LOG_FILENAME, "r");
  mgmt_fcnt_table_log.read((unsigned char*) &mgmt_fcnt_table_cnt, sizeof(mgmt_fcnt_table_cnt));
  mgmt_fcnt_table_log.read((unsigned char*) &mgmt_fcnt_table, MGMT_FCNT_LEN * MGMT_FCNT_TABLE_SIZE);
  mgmt_fcnt_table_log.close();

#if defined(LORA_LOG)
  lora_log = MyFS.open(LORA_LOG_FILENAME, FILE_APPEND);
  lora_log.printf("reboot\n");
#if defined(AXP_DEBUG)
  unsigned long int m = millis();
  lora_log.printf("t=%d.%03d battery=%.04gV\n", m/1000, m%1000,
                                                axp.getBattVoltage()/1000);
  next_log_battery = m + LOG_BATTERY_INTERVAL;
#endif
  lora_log.printf("millis,batt,utc,mac,lat,lon,ele,plen,prssi,psnr,pfe,rssi\n");
  next_log_flush = millis() + LOG_FLUSH_INTERVAL;
  Serial.printf("\r\nlength of %s: %d bytes\r\n", LORA_LOG_FILENAME, lora_log.size());
#endif // LORA_LOG

  Serial.printf("\r\nHeap: %d total, %d free, %d min, %d maxAlloc\r\n",
                 ESP.getHeapSize(), ESP.getFreeHeap(),
                 ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());

  Serial.println("\r\ninit done, starting loop now. Type '?' for list of commands\r\n");

  delay(1500); // keep the screen for some time so the display headline can be read ..
  OLED_toggle(); // default is OLED off, use button to switch on

  cpu_set_slow();

/*
  Serial.println("LIGHT SLEEP ENABLED FOR 5secs");
  delay(100);
 
  esp_sleep_enable_timer_wakeup(5 * 1000 * 1000);
  esp_light_sleep_start();
 
  Serial.println();
  Serial.println("LIGHT SLEEP WAKE UP");
*/
}

// ----------------------------------------------------------------------

int incoming(struct face_s *f, unsigned char *pkt, int len, int has_crc)
{
  if (len <= (DMX_LEN + sizeof(uint32_t)) || (has_crc && crc_check(pkt, len) != 0)) {
    Serial.println(String("Bad CRC for face ") + f->name + String(" pkt=") + to_hex(pkt, len));
    lora_bad_crc++;
    return -1;
  }
  if (has_crc) {
    // Serial.println("CRC OK");
    len -= sizeof(uint32_t);
  }
  if (!on_rx(pkt, len, f))
    return 0;
  Serial.println(String("DMX: unknown ") + to_hex(pkt, DMX_LEN));
  return -1;
}

void right_aligned(int cnt, char c, int y)
{
#if !defined(NO_OLED)
  char buf[20];
  sprintf(buf, "%d %c", cnt, c);
  int w = theDisplay.getStringWidth(buf);
  theDisplay.drawString(128-w, y, buf);
#endif
}

const char *wheel[] = {"\\", "|", "/", "-"};
int spin;

void loop()
{
  unsigned char pkt_buf[200], *cp;
  int packetSize, pkt_len;

#if !defined(NO_WIFI)
  if (WiFi.softAPgetStationNum() != wifi_clients) {
    wifi_clients = WiFi.softAPgetStationNum();
    refresh = 1;
  }
#endif
  
#if defined(MAIN_BLEDevice_H_)
  if (bleDeviceConnected != ble_clients) {
    ble_clients = bleDeviceConnected;
    refresh = 1;
  }
#endif

  userButton.loop();
  io_dequeue();
  goset_tick(theGOset);
  node_tick();

  if (Serial.available())
    cmd_rx(Serial.readString());

#if !defined(NO_LORA)
  packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    pkt_len = 0;
    while (packetSize-- > 0) {
      unsigned char c = LoRa.read();
      if (pkt_len < sizeof(pkt_buf))
        pkt_buf[pkt_len++] = c;
    }
#if defined (LORA_LOG)
    {
      unsigned long int m = millis();
      long pfe = 0;
      int rssi = 0;

#if defined(WIFI_LoRa_32_V2) || defined(WIFI_LORA_32_V2)
      lora_log.printf("%d.%03d,0000-00-00T00:00:00Z,%s,0,0,0",
                      m/1000, m%1000,
                      to_hex(my_mac,6,1));
#else
# if !defined(NO_GPS)
      lora_log.printf("%d.%03d,%04d-%02d-%02dT%02d:%02d:%02dZ,%s",
                      m/1000, m%1000,
                      gps.date.year(), gps.date.month(), gps.date.day(),
                      gps.time.hour(), gps.time.minute(), gps.time.second(),
                      // gps.time.centisecond(),
                      to_hex(my_mac,6,1));
      if (gps.location.isValid())
        lora_log.printf(",%.8g,%.8g,%g", gps.location.lat(),
                        gps.location.lng(), gps.altitude.meters());
      else
        lora_log.printf(",0,0,0");
#endif
      pfe   = LoRa.packetFrequencyError();
      rssi   = LoRa.rssi();
#endif

      int prssi  = LoRa.packetRssi();
      float psnr = LoRa.packetSnr();

      lora_log.printf(",%d,%d,%g,%ld,%d\n", pkt_len, prssi, psnr, pfe, rssi);
      if (millis() > next_log_flush) {
        lora_log.flush();
        next_log_flush = millis() + LOG_FLUSH_INTERVAL;
      }
    }
#endif
    lora_cnt++;
    incoming(&lora_face, pkt_buf, pkt_len, 1);
    // sprintf(lora_line, "LoRa %d/%d: %dB, rssi=%d", lora_cnt, lora_bad_crc, pkt_len, LoRa.packetRssi());
    refresh = 1;
  }
#endif // NO_LORA

#if !defined(NO_WIFI)
  packetSize = udp.parsePacket();
  if (packetSize) {
    // Serial.print("UDP " + String(packetSize) + "B from "); 
    // Serial.print(udp.remoteIP());
    // Serial.println("/" + String(udp.remotePort()));
    pkt_len = udp.read(pkt_buf, sizeof(pkt_buf));
    incoming(&udp_face, pkt_buf, pkt_len, 1);
  }
#endif

#if !defined(NO_BT)
  packetSize = kiss_read(BT, &bt_kiss);
  if (packetSize > 0) {
    incoming(&bt_face, bt_kiss.buf, packetSize, 1);
  }
#endif

#if defined(MAIN_BLEDevice_H_) && !defined(NO_BLE)
  cp = ble_fetch_received();
  if (cp != NULL) {
    incoming(&ble_face, cp+1, *cp, 0);
  }
#endif

#if defined(LORA_LOG)

  if (millis() > next_log_battery) {
    unsigned long int now = millis();
    lora_log.printf("t=%d.%03d ", now/1000, now%1000);
# if defined(AXP_DEBUG)
    lora_log.printf("battery=%.04gV ", axp.getBattVoltage()/1000);
# endif
    lora_log.printf("|dmxt|=%d |blbt|=%d |feeds|=%d |entries|=%d |chunks|=%d |freeHeap|=%dXX\r\n",
                dmxt_cnt, blbt_cnt, feed_cnt, entry_cnt, chunk_cnt, ESP.getFreeHeap());
    next_log_battery = millis() + LOG_BATTERY_INTERVAL;
  }

# if !defined(NO_GPS)
  while (GPS.available())
    gps.encode(GPS.read());
  if (gps.time.second() != old_gps_sec) {
    old_gps_sec = gps.time.second();
    sprintf(time_line, "%02d:%02d:%02d utc", gps.time.hour(), gps.time.minute(),
                                      old_gps_sec);
    /*
    if (gps.location.isValid())
      sprintf(loc_line, "%.8g@%.8g@%g/%d", gps.location.lat(), gps.location.lng(),
                                      gps.altitude.meters(), gps.satellites.value());
    else
      strcpy(loc_line, "-@-@-/-");
    */
    refresh = 1;
  }
# endif
  if (old_goset_c != theGOset->pending_c_cnt || 
      old_goset_n != theGOset->pending_n_cnt ||
      old_goset_len != theGOset->goset_len) {
    old_goset_c = theGOset->pending_c_cnt;
    old_goset_n = theGOset->pending_n_cnt;
    old_goset_len = theGOset->goset_len;
    sprintf(goset_line, "GOS: len=%d, pn=%d, pc=%d", old_goset_len, old_goset_n, old_goset_c);
    refresh = 1;
  }
#endif // LORA_LOG

  int sum = feed_cnt + entry_cnt + chunk_cnt;
  if (sum != old_repo_sum) {
    old_repo_sum = sum;
    refresh = 1;
  }

#if !defined(NO_OLED)
  if (refresh) {
    theDisplay.clear();

    theDisplay.setFont(ArialMT_Plain_10);
    theDisplay.drawString(0, 3, tSSB_WIFI_SSID "-");
    theDisplay.setFont(ArialMT_Plain_16);
    theDisplay.drawString(42, 0, ssid+8);
    theDisplay.setFont(ArialMT_Plain_10);
    
    theDisplay.drawString(0, 18, time_line);
    // theDisplay.drawString(0, 24, goset_line);
    char stat_line[30];
    char gps_synced = 0;
# if !defined(NO_GPS)
    gps_synced = gps.location.isValid() ? 1 : 0;
#endif
    sprintf(stat_line, "W:%d E:%d G:%d L:%s",
            wifi_clients, ble_clients, gps_synced, wheel[lora_cnt % 4]);
    theDisplay.drawString(0, 30, stat_line);

#if defined(MAIN_BLEDevice_H_)
#if defined(WIFI_LoRa_32_V2) || defined(WIFI_LORA_32_V2)
    sprintf(stat_line + strlen(stat_line)-1, "%d", lora_cnt);
#else
    sprintf(stat_line + strlen(stat_line)-1, "%d, batt:%.04g",
            lora_cnt, axp.getBattVoltage()/1000);
#endif
    ble_send_stats((unsigned char*) stat_line, strlen(stat_line));
#endif

    theDisplay.setFont(ArialMT_Plain_16);
    right_aligned(feed_cnt,  'F', 0); 
    right_aligned(entry_cnt, 'E', 22); 
    right_aligned(chunk_cnt, 'C', 44); 

    int total = MyFS.totalBytes();
    int avail = total - MyFS.usedBytes();
    char buf[10];
    sprintf(buf, "%2d%% free", avail / (total/100));
    theDisplay.drawString(0, 44, buf);

    // theDisplay.drawString(0, 12, wheel[spin++ % 4]);     // lora_line
    theDisplay.display();
    refresh = 0;
  }
#endif // NO_OLED

  // periodically send status
  if (millis() > mgmt_next_send_status) {
    mgmt_send_status();
  }

  // check if beacon is active
  if (mgmt_beacon && millis() > next_mgmt_send_beacon) {
    mgmt_send_beacon();
    next_mgmt_send_beacon = millis() + MGMT_SEND_BEACON_INTERVAL + random(2000);
  }

  delay(10);

  // ftpSrv.handleFTP();
}

// eof
