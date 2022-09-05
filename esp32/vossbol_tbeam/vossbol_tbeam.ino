// vossbol_tbeam.ino

// tinySSB for ESP32
// 2022-08-09 <christian.tschudin@unibas.ch>


#define LORA_BAND    902E6 // USA
#define LORA_BW     125000
#define LORA_SF          7
#define LORA_CR          5
#define LORA_TXPOWER    17

#define FID_LEN         32
#define HASH_LEN        20
#define FID_HEX_LEN     (2*FID_LEN)
#define FID_B64_LEN     ((FID_LEN + 2) / 3 * 4)
#define FEED_DIR        "/feeds"
#define MAX_FEEDS      100 

#define DMX_LEN          7
#define GOSET_DMX_STR    "tinySSB-0.1 GOset 1"

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
void incoming_pkt(unsigned char* buf, int len, unsigned char *fid);
void incoming_chunk(unsigned char* buf, int len, int blbt_ndx);
void incoming_want_request(unsigned char* buf, int len, unsigned char* aux);
void incoming_chnk_request(unsigned char* buf, int len, unsigned char* aux);


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

char lora_line[80];
char gps_line[80];
char goset_line[80];
char refresh = 1;

int old_gps_sec, old_goset_c, old_goset_n, old_goset_len;
int old_repo_sum;
int lora_cnt = 0;
int lora_bad_crc = 0;

#include "cmd.h"

// ----------------------------------------------------------------------------
void setup()
{
  hw_setup();
  
  io_init();

  theGOset = goset_new();
  unsigned char h[32];
  crypto_hash_sha256(h, (unsigned char*) GOSET_DMX_STR, strlen(GOSET_DMX_STR));
  memcpy(goset_dmx, h, DMX_LEN);
  arm_dmx(goset_dmx, goset_rx, NULL);
  Serial.println(String("listening for GOset protocol on ") + to_hex(goset_dmx, 7));
  
  repo_load();

  theDisplay.setFont(ArialMT_Plain_16);
  theDisplay.drawString(0 , 0, "SSB.virt.lora.pub");
  theDisplay.setFont(ArialMT_Plain_10);
  theDisplay.drawString(0 , 18, __DATE__ " " __TIME__);
  theDisplay.display();
  delay(1000);
  strcpy(lora_line, "?");
  strcpy(goset_line, "?");

  Serial.println("\nFile system: " + String(MyFS.totalBytes(), DEC) + " total bytes, "
                                 + String(MyFS.usedBytes(), DEC) + " used");
  MyFS.mkdir(FEED_DIR);
  listDir(MyFS, FEED_DIR, 0);
  // ftpSrv.begin(".",".");
  
  delay(2000);
  Serial.println("init done, starting loop now. Type '?' for list of commands\n");
}

int incoming(struct face_s *f, unsigned char *pkt, int len)
{
  if (len <= (DMX_LEN + sizeof(uint32_t)) || crc_check(pkt, len) != 0) {
    Serial.println(String("Bad CRC for face ") + f->name + String(" pkt=") + to_hex(pkt, len));
    lora_bad_crc++;
    return -1;
  }
  // Serial.println("CRC OK");
  if (!on_rx(pkt, len - sizeof(uint32_t))) // remove 4 CRC bytes for CRC32
    return 0;
  Serial.println(String("DMX: unknown ") + to_hex(pkt, DMX_LEN));
  return -1;
}


void right_aligned(int cnt, char c, int y)
{
  char buf[20];
  sprintf(buf, "%d %c", cnt, c);
  int w = theDisplay.getStringWidth(buf);
  theDisplay.drawString(128-w, y, buf);
}

const char *wheel[] = {"\\", "|", "/", "-"};
int spin;

void loop()
{
  unsigned char pkt_buf[200], *cp;
  int packetSize, pkt_len;

  if (WiFi.softAPgetStationNum() != wifi_clients) {
    wifi_clients = WiFi.softAPgetStationNum();
    refresh = 1;
  }
  if (bleDeviceConnected != ble_clients) {
    ble_clients = bleDeviceConnected;
    refresh = 1;
  }
  
  io_dequeue();
  goset_tick(theGOset);
  node_tick();

  if (Serial.available())
    cmd_rx(Serial.readString());

  packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    pkt_len = 0;
    while (packetSize-- > 0) {
      unsigned char c = LoRa.read();
      if (pkt_len < sizeof(pkt_buf))
        pkt_buf[pkt_len++] = c;
    }
    lora_cnt++;
    incoming(&lora_face, pkt_buf, pkt_len);
    // sprintf(lora_line, "LoRa %d/%d: %dB, rssi=%d", lora_cnt, lora_bad_crc, pkt_len, LoRa.packetRssi());
    refresh = 1;
  }

  packetSize = udp.parsePacket();
  if (packetSize) {
    // Serial.print("UDP " + String(packetSize) + "B from "); 
    // Serial.print(udp.remoteIP());
    // Serial.println("/" + String(udp.remotePort()));
    pkt_len = udp.read(pkt_buf, sizeof(pkt_buf));
    incoming(&udp_face, pkt_buf, pkt_len);
  }

  packetSize = kiss_read(BT, &bt_kiss);
  if (packetSize > 0) {
    incoming(&bt_face, bt_kiss.buf, packetSize);
  }

  cp = ble_fetch_received();
  if (cp != NULL) {
    incoming(&ble_face, cp+1, *cp);
  }

#if !defined(ARDUINO_WIFI_LORA_32_V2)
  while (GPS.available())
    gps.encode(GPS.read());
  if (gps.time.second() != old_gps_sec) {
    old_gps_sec = gps.time.second();
    sprintf(gps_line, "%02d:%02d:%02d utc", gps.time.hour(), gps.time.minute(), old_gps_sec);
    refresh = 1;
  }
  if (old_goset_c != theGOset->pending_c_cnt || 
      old_goset_n != theGOset->pending_n_cnt ||
      old_goset_len != theGOset->goset_len) {
    old_goset_c = theGOset->pending_c_cnt;
    old_goset_n = theGOset->pending_n_cnt;
    old_goset_len = theGOset->goset_len;
    sprintf(goset_line, "GOS: len=%d, pn=%d, pc=%d", old_goset_len, old_goset_n, old_goset_c);
    refresh = 1;
  }
#endif

  int sum = feed_cnt + entry_cnt + chunk_cnt;
  if (sum != old_repo_sum) {
    old_repo_sum = sum;
    refresh = 1;
  }

  if (refresh) {
    theDisplay.clear();

    theDisplay.setFont(ArialMT_Plain_10);
    theDisplay.drawString(0, 3, tSSB_WIFI_SSID "-");
    theDisplay.setFont(ArialMT_Plain_16);
    theDisplay.drawString(42, 0, ssid+8);
    theDisplay.setFont(ArialMT_Plain_10);
    
    theDisplay.drawString(0, 18, gps_line);
    // theDisplay.drawString(0, 24, goset_line);
    theDisplay.drawString(0, 30, "w=" + String(wifi_clients) + \
                                 " e=" + String(ble_clients) + \
                                 " l=" + wheel[lora_cnt % 4]);

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

  delay(10);
  // ftpSrv.handleFTP();
}

// eof
