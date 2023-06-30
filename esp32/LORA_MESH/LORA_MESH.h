// LORA_MESH.h


// #define NO_BLE   // disable Bluetooth Low Energy
#define NO_BT    // disable Bluetooth
// #define NO_GPS   // disable GPS
// #define NO_LORA  // disable LoRa
// #define NO_OLED  // disable OLED
#define NO_WIFI  // disable WiFi

#define SLOW_CPU
#define SLOW_CPU_MHZ 80  // 40MHz is too low for handling the BLE protocol

#define LOG_FLUSH_INTERVAL         10000 // millis
#define LOG_BATTERY_INTERVAL  15*60*1000 // millis (15 minutes)

// ----------------------------------------------------------------------

#define BAUD_RATE    115200 // or 38400 or 460800

#define LORA_BAND    902.5E6 // USA
// #define LORA_BAND  865.5E6 // Europe
#define LORA_BW     500000
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
#define MAX_FEEDS       100
#define FEED_PATH_SIZE  sizeof(FEED_DIR) + 1 + 2 * FID_LEN

#define DMX_LEN          7
#define GOSET_DMX_STR    "tinySSB-0.1 GOset 1"

#define TINYSSB_PKT_LEN  120
#define TINYSSB_SCC_LEN  (TINYSSB_PKT_LEN - HASH_LEN) // sidechain content per packet

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


extern unsigned int bipf_varint_decode(unsigned char *buf, int pos, int *lptr);

extern void io_enqueue(unsigned char *pkt, int len, unsigned char *dmx /*=NULL*/, struct face_s *f /*=NULL*/);
extern void io_send(unsigned char *buf, short len, struct face_s *f /*=NULL*/);

extern void incoming_pkt(unsigned char* buf, int len, unsigned char *fid, struct face_s *);
extern void incoming_chunk(unsigned char* buf, int len, int blbt_ndx, struct face_s *);
extern void incoming_want_request(unsigned char* buf, int len, unsigned char* aux, struct face_s *);
extern void incoming_chnk_request(unsigned char* buf, int len, unsigned char* aux, struct face_s *);
extern void ble_init();

#include <Arduino.h>

#include "device.h"
#include "util.h"
#include "io.h"
#include "goset.h"
extern GOsetClass *theGOset;
#include "dmx.h"
extern DmxClass   *dmx;
#include "repo.h"
extern RepoClass  *repo;

// eof
