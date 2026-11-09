#include <Wire.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <SPIFFS.h>
#include "esp_camera.h"
#include "driver/i2s.h"


const char* ssid = "xxxxxxxxxxxx"; // Hidden
const char* password = "xxxxxxxxxxxx"; // Hidden


WiFiServer server(80);


#define SIGNAL_PIN 7
#define PA_ENABLE_PIN 47


bool busy = false;


// ================= CAMERA CONFIG =================


#define CAM_D0 2
#define CAM_D1 17
#define CAM_D2 18
#define CAM_D3 39
#define CAM_D4 45
#define CAM_D5 46
#define CAM_D6 47
#define CAM_D7 48


#define CAM_XCLK 43
#define CAM_PCLK 44
#define CAM_VSYNC 21
#define CAM_HREF 1


#define CAM_SIOD 11
#define CAM_SIOC 10


// =================================================


// --- Pulse ---
void sendPulse() {
  digitalWrite(SIGNAL_PIN, LOW);
  delay(80);
  digitalWrite(SIGNAL_PIN, HIGH);
  delay(120);
}


void sendCommand(int count) {
  busy = true;


  for (int i = 0; i < count; i++) sendPulse();


  delay(300);


  for (int i = 0; i < 4; i++) sendPulse();


  delay(500);


  busy = false;
}


// --- Letter → pulse ---
int getCommandCount(char cmd) {
  switch (cmd) {
    case 'e': return 1;
    case 'f': return 2;
    case 'h': return 3;
    case 'g': return 4;
    case 'u': return 5;
    case 'j': return 6;
    case 's': return 7;
    case 'x': return 8;
    case 'r': return 9;
    case 'l': return 10;
    case 'm': return 11;
    case 'i': return 12;
    case 'o': return 13;
    default: return 0;
  }
}


// --- AUDIO ---
void playAudio(const char* path) {
  Serial.println("Audio received (not playing yet)");
}


// --- CAMERA INIT ---
void initCamera() {
  camera_config_t config;


  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;


  config.pin_d0 = CAM_D0;
  config.pin_d1 = CAM_D1;
  config.pin_d2 = CAM_D2;
  config.pin_d3 = CAM_D3;
  config.pin_d4 = CAM_D4;
  config.pin_d5 = CAM_D5;
  config.pin_d6 = CAM_D6;
  config.pin_d7 = CAM_D7;


  config.pin_xclk = CAM_XCLK;
  config.pin_pclk = CAM_PCLK;
  config.pin_vsync = CAM_VSYNC;
  config.pin_href = CAM_HREF;


  config.pin_sscb_sda = CAM_SIOD;
  config.pin_sscb_scl = CAM_SIOC;


  config.pin_pwdn = -1;
  config.pin_reset = -1;


  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;


  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_DRAM;


  esp_err_t err = esp_camera_init(&config);


  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return;
  }


  Serial.println("Camera init OK");
}


void initI2S() {
  Serial.println("Using default I2S (no manual config)");
}


void initES8311() {
  Wire.begin(11, 10); // SDA, SCL


  Serial.println("Initializing ES8311...");


  auto writeReg = [](uint8_t reg, uint8_t val) {
    Wire.beginTransmission(0x18);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
  };


  writeReg(0x00, 0x1F); // reset
  delay(50);


  writeReg(0x01, 0x30);
  writeReg(0x02, 0x00);
  writeReg(0x03, 0x10);


  writeReg(0x04, 0x10);
  writeReg(0x05, 0x00);


  writeReg(0x06, 0x00);
  writeReg(0x07, 0x00);


  writeReg(0x0A, 0x00);
  writeReg(0x0B, 0x02); // DAC on
  writeReg(0x0C, 0x1F); // volume (safe)


  writeReg(0x10, 0x1F); // enable output
  writeReg(0x11, 0x7F);


  Serial.println("ES8311 ready");
}


void setup() {
  Serial.begin(115200);


  pinMode(SIGNAL_PIN, OUTPUT);
  digitalWrite(SIGNAL_PIN, HIGH);


  SPIFFS.begin(true);


  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);


  Serial.println(WiFi.localIP());


  pinMode(PA_ENABLE_PIN, OUTPUT);
  digitalWrite(PA_ENABLE_PIN, HIGH);  // 🔥 TURN ON SPEAKER AMP


  initCamera();
  initI2S();
  //initES8311();


  // 🔥 AUDIO TEST ON BOOT
  delay(2000);
  playAudio("/speech.wav");


  server.begin();
}


void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    delay(100);
    return;
  }
 
  WiFiClient client = server.available();
  if (!client) return;


  String request = client.readStringUntil('\n');


  bool isCapture = request.indexOf("GET /capture") >= 0;
  bool isCommand = request.indexOf("GET /cmd") >= 0;
  bool isAudio   = request.indexOf("POST /audio") >= 0;


  char cmd = 0;


  if (isCommand) {
    int index = request.indexOf("c=");
    if (index != -1) cmd = request.charAt(index + 2);
  }


  if (isCapture) {
    camera_fb_t * fb = esp_camera_fb_get();


    if (!fb) {
      Serial.println("Camera capture failed");
      client.stop();
      return;
    }


    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: image/jpeg");
    client.println();


    client.write(fb->buf, fb->len);
    esp_camera_fb_return(fb);


    delay(50);


    client.stop();
    return;
  }


  client.println("HTTP/1.1 200 OK");
  client.println();
  client.println("OK");


  if (isAudio) {
    File file = SPIFFS.open("/speech.wav", FILE_WRITE);
    while (client.available()) file.write(client.read());
    file.close();
    playAudio("/speech.wav");
  }


  client.stop();


  if (cmd && !busy) {
    int count = getCommandCount(cmd);
    if (count > 0) sendCommand(count);
  }
}