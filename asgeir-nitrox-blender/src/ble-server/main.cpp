#include <esp_now.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library

void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len);

#define CHANNEL 1

// Init ESP Now with fallback
void InitESPNow() {
  WiFi.disconnect();
  if (esp_now_init() == ESP_OK) {
    tft.println("ESPNow Init Success");
  }
  else {
    tft.println("ESPNow Init Failed");
    // Retry InitESPNow, add a counte and then restart?
    // InitESPNow();
    // or Simply Restart
    ESP.restart();
  }
}

// config AP SSID
void configDeviceAP() {
  const char *SSID = "Client_1";
  bool result = WiFi.softAP(SSID, "Client_1_Password", CHANNEL, 0);
  if (!result) {
    tft.println("AP Config failed.");
  } else {
    tft.println("AP Config Success. Broadcasting with AP: " + String(SSID));
  }
}

void setup() {
//  tft.begin(115200);

    tft.init();
    tft.setRotation(3);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);
  
  tft.println("ESPNow/Basic/Client Example");
  //Set device in AP mode to begin with
  WiFi.mode(WIFI_AP);
  // configure device AP mode
  configDeviceAP();
  // This is the mac address of the Client in AP Mode
  tft.print("AP MAC: "); tft.println(WiFi.softAPmacAddress());
  // Init ESPNow with a fallback logic
  InitESPNow();
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info.
  esp_now_register_recv_cb(OnDataRecv);
}

// callback when data is recv from Sender
void OnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
//    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 45);  
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  tft.println("Last Recv from: "); tft.println(macStr);
  tft.println("Last Recv Data: "); tft.print(*data); tft.println("   ");
}

void loop() {
  // Chill
}