#include "esp_camera.h"
#include <WiFi.h>

const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASS";

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WiFiServer server(80);

uint8_t* previous_frame = NULL;
int frame_size = 160 * 120;

void startCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;

  config.frame_size = FRAMESIZE_QQVGA; // 160x120
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_camera_init(&config);
}

void setup() {
  Serial.begin(115200);

  startCamera();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  Serial.println(WiFi.localIP());
  server.begin();

  previous_frame = (uint8_t*)ps_malloc(frame_size);
}

void loop() {

  WiFiClient client = server.available();
  if (!client) return;

  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) return;

  int motion_pixels = 0;

  if (previous_frame != NULL) {
    for (int i = 0; i < frame_size; i++) {
      if (abs(fb->buf[i] - previous_frame[i]) > 25) {
        motion_pixels++;
      }
    }
  }

  bool fish_detected = motion_pixels > 1000;

  memcpy(previous_frame, fb->buf, frame_size);

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/plain");
  client.println();
  client.println(fish_detected ? "FISH DETECTED" : "NO FISH");

  esp_camera_fb_return(fb);
  client.stop();
}
