// hw_dsetup.h

// tinySSB for ESP32
// Aug 2022 <christian.tschudin@unibas.ch>

// collect all external libraries here


#if defined(WIFI_LoRa_32_V2) || defined(WIFI_LORA_32_V2)

# include <heltec.h>
# define theDisplay (*Heltec.display)

#else // ARDUINO_TBeam

# include <SSD1306.h> // display
# include <Wire.h> 
# include <LoRa.h>

SSD1306 theDisplay(0x3c, 21, 22); // lilygo t-beam

// GPS
# include <TinyGPS++.h>
# include <axp20x.h>

# define SCK     5    // GPIO5  -- SX1278's SCK
# define MISO    19   // GPIO19 -- SX1278's MISO
# define MOSI    27   // GPIO27 -- SX1278's MOSI
# define SS      18   // GPIO18 -- SX1278's CS
# define RST     14   // GPIO14 -- SX1278's RESET
# define DI0     26   // GPIO26 -- SX1278's IRQ(Interrupt Request)

#endif // device specific


// FS
#include <littlefs_api.h>
#include <littleFS.h>

// crypto
#include <sodium/crypto_hash_sha256.h>
#include <sodium/crypto_sign_ed25519.h>

// WiFi and BT
#include <WiFi.h>
#include <WiFiAP.h>
#include "BluetoothSerial.h"

// BLE
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include "esp_gatt_common_api.h"


// create instances

#define MyFS LITTLEFS
WiFiUDP udp;
IPAddress broadcastIP;
BluetoothSerial BT;

#if defined(AXP_DEBUG)
TinyGPSPlus gps;
HardwareSerial GPS(1);
AXP20X_Class axp;
#endif

unsigned char my_mac[6];

// -------------------------------------------------------------------

void hw_setup() // T-BEAM or Heltec LoRa32v2
{
#if defined(WIFI_LoRa_32_V2) || defined(WIFI_LORA_32_V2)
  Heltec.begin(true /*DisplayEnable Enable*/,
               true /*Heltec.Heltec.Heltec.LoRa Disable*/,
               true /*Serial Enable*/,
               true /*PABOOST Enable*/,
               LORA_BAND /*long*/);
  LoRa.setTxPower(LORA_TXPOWER, RF_PACONFIG_PASELECT_PABOOST);
  
#else // T-Beam
  Serial.begin(115200); while (!Serial);
  delay(100);

  theDisplay.init();
  theDisplay.flipScreenVertically();
  theDisplay.setFont(ArialMT_Plain_10);
  theDisplay.setTextAlignment(TEXT_ALIGN_LEFT);

  pinMode(16,OUTPUT);
  digitalWrite(16, LOW);    // set GPIO16 low to reset OLED
  delay(50); 
  digitalWrite(16, HIGH); // while OLED is running, must set GPIO16 in high、

  SPI.begin(SCK,MISO,MOSI,SS);
  LoRa.setPins(SS,RST,DI0);  
  if (!LoRa.begin(LORA_BAND)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  LoRa.setTxPower(LORA_TXPOWER);

  Wire.begin(21, 22);
  if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
    // Serial.println("AXP192 Begin PASS");
    axp.setPowerOutPut(AXP192_LDO2, AXP202_ON);
    axp.setPowerOutPut(AXP192_LDO3, AXP202_ON);
    axp.setPowerOutPut(AXP192_DCDC2, AXP202_ON);
    axp.setPowerOutPut(AXP192_EXTEN, AXP202_ON);
    axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON);
    GPS.begin(9600, SERIAL_8N1, 34, 12);   //17-TX 18-RX
  } else {
    Serial.println("AXP192 Begin FAIL");
  }

#endif

  LoRa.setSignalBandwidth(LORA_BW);
  LoRa.setSpreadingFactor(LORA_SF);
  LoRa.setCodingRate4(LORA_CR);
  LoRa.setPreambleLength(8);
  LoRa.setSyncWord(LORA_SYNC_WORD);
  LoRa.receive();

  Serial.println("\n** Starting Scuttlebutt vPub (LoRa, WiFi, BLE) with GOset **\n");
  theDisplay.clear();
  theDisplay.display();


  // -------------------------------------------------------------------
  if (!MyFS.begin(true)) { // FORMAT_SPIFFS_IF_FAILED)){
    Serial.println("LittleFS Mount Failed, partition was reformatted");
    // return;
  }

  // -------------------------------------------------------------------

  esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
  Serial.println(String("mac   ") + to_hex(my_mac, 6, 1));

  WiFi.disconnect(true);
  delay(500);
  WiFi.mode(WIFI_AP);
  sprintf(ssid, "%s-%s", tSSB_WIFI_SSID, to_hex(my_mac+4, 2));
  Serial.println(String("wifi  ") + ssid + " / " + tSSB_WIFI_PW);

  WiFi.softAP(ssid, tSSB_WIFI_PW, 7, 0, 4); // limit to four clients
  broadcastIP.fromString(tSSB_UDP_ADDR);
  if (!udp.beginMulticast(broadcastIP, tSSB_UDP_PORT)) {
    Serial.println("could not create multicast socket");
  } else {
    Serial.println("udp   " + broadcastIP.toString() + " / " + String(tSSB_UDP_PORT));
  }

#if defined(MAIN_BLEDevice_H_)
  ble_init();
#endif

  BT.begin(ssid);
  BT.setPin("0000");
  BT.write(KISS_FEND);

  Serial.println();
}

// ---------------------------------------------------------------------------------

static char hex_str[101];
static const char *hexdigits = "0123456789abcdef";

char* to_hex(unsigned char *buf, int bin_len, int add_colon)
{
  char *cp = hex_str;
  if (2*bin_len >= sizeof(hex_str))
    bin_len = sizeof(hex_str) / 2;
  for (int i = 0; i < bin_len; i++) {
    if (add_colon && i > 0) *cp++ = ':';
    *cp++ = hexdigits[buf[i] >> 4];
    *cp++ = hexdigits[buf[i] & 0x0f];
  }
  *cp = 0;
  return hex_str;
}

unsigned char* from_hex(char *hex, int len) // len = number of bytes to retrieve from hexstring
{
  static unsigned char buf[32];
  if (2*len > strlen(hex))
    return NULL;
  for (int cnt = 0; cnt < len; cnt++, hex += 2)
    sscanf(hex, "%2hhx", buf + cnt);
  return buf;
}

// --------------------------------------------------------------------------------

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
    Serial.printf("Listing directory: %s\r\n", dirname);

    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println(" - not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        char *fn = strrchr(file.name(), '/') + 1;
        if(file.isDirectory()){
            Serial.print("  DIR : ");
            Serial.println(fn);
            if(levels){
                listDir(fs, file.name(), levels -1);
            }
        } else {
            Serial.print("  FILE: ");
            Serial.print(fn);
            Serial.print("\tSIZE: ");
            Serial.println(file.size());
        }
        file = root.openNextFile();
    }
}

// eof
