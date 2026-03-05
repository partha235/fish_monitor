#include <Arduino.h>
#include <WiFi.h>
#include "esp_camera.h"
#include <esp_log.h>
#include "esp_http_server.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ================= USER CONFIG =================
const char* ssid = "bps_cam";
const char* password = "12345678";
const int defaultFPS = 2;

// Logging tag
static const char *TAG = "CAMERA";


volatile uint32_t captureCount = 0;

// === ESP32-S3 Common Camera Pinout (matches many S3-CAM / S3-EYE boards) ===
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM     4   // SCCB SDA
#define SIOC_GPIO_NUM     5   // SCCB SCL
#define Y2_GPIO_NUM      11
#define Y3_GPIO_NUM       9
#define Y4_GPIO_NUM       8
#define Y5_GPIO_NUM      10
#define Y6_GPIO_NUM      12
#define Y7_GPIO_NUM      18
#define Y8_GPIO_NUM      17
#define Y9_GPIO_NUM      16
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13

#define LED_GPIO_NUM      2   // Built-in LED on many S3-CAM boards


// ================= STREAM CONSTANTS =================
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ================= HTML PAGE =================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<html>
    <head><title>Camera Monitor</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; text-align: center; margin-top: 30px; }
        button { font-size: 18px; padding: 10px 20px; margin: 10px; }
        img { width: 80%; max-width: 480px; height: auto; }
    </style>
    </head>
    <body>
        <h1>Camera Monitor</h1>
        <img src="/stream" alt="Live stream"><br>
    </body>
</html>
)rawliteral";

// ================= GLOBAL =================
httpd_handle_t server = NULL;

// ================= CAMERA INIT =================
bool initCamera() {
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
    config.pixel_format = PIXFORMAT_JPEG;
  
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_LATEST;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  
    if (psramFound()) {
        Serial.println("PSRAM FOUND");
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count = 2;
    } else {
        Serial.println("NO PSRAM");
        config.frame_size = FRAMESIZE_VGA;
        config.fb_location = CAMERA_FB_IN_DRAM;
    }
  
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return false;   // ✅ correct
    }
  
    // Test capture once
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Initial capture failed");
        return false;   // ✅ correct
    }
  
    esp_camera_fb_return(fb);
  
    return true;   // ✅ SUCCESS
  }

// ================= LED INIT =================
void initLED() {
  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);
}

// ================= HTTP HANDLERS =================
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
  return ESP_OK;
}

static esp_err_t control_handler(httpd_req_t *req) {

  char query[32];
  char value[16];

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    if (httpd_query_key_value(query, "state", value, sizeof(value)) == ESP_OK) {

      if (!strcmp(value, "on")) {
        digitalWrite(LED_GPIO_NUM, HIGH);
        httpd_resp_sendstr(req, "LED ON");
        return ESP_OK;
      }

      if (!strcmp(value, "off")) {
        digitalWrite(LED_GPIO_NUM, LOW);
        httpd_resp_sendstr(req, "LED OFF");
        return ESP_OK;
      }
    }
  }

  httpd_resp_set_status(req, "400 Bad Request");
  httpd_resp_sendstr(req, "Invalid");
  return ESP_FAIL;
}

static esp_err_t capture_handler(httpd_req_t *req) {

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Capture FAILED");
    return ESP_FAIL;
  }

  captureCount++;
  Serial.printf("📸 Capture #%lu | Size: %u bytes\n", captureCount, fb->len);

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_send(req, (const char *)fb->buf, fb->len);

  esp_camera_fb_return(fb);

  return ESP_OK;
}

static esp_err_t stream_handler(httpd_req_t *req) {

  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);

  camera_fb_t *fb = NULL;
  char part_buf[64];

  while (true) {

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Stream capture FAILED");
      break;
    }

captureCount++;
Serial.printf("🎥 Stream Frame #%lu | Size: %u bytes\n", captureCount, fb->len);
    size_t hlen = snprintf(part_buf, 64, STREAM_PART, fb->len);
    httpd_resp_send_chunk(req, part_buf, hlen);
    httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));

    esp_camera_fb_return(fb);

    delay(1000 / defaultFPS);
  }

  return ESP_OK;
}

// ================= SERVER START =================
void startServer() {

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  httpd_uri_t index_uri = { "/", HTTP_GET, index_handler, NULL };
  httpd_uri_t control_uri = { "/control", HTTP_GET, control_handler, NULL };
  httpd_uri_t stream_uri = { "/stream", HTTP_GET, stream_handler, NULL };
  httpd_uri_t capture_uri = { "/capture", HTTP_GET, capture_handler, NULL };

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &index_uri);
    httpd_register_uri_handler(server, &control_uri);
    httpd_register_uri_handler(server, &stream_uri);
    httpd_register_uri_handler(server, &capture_uri);
  }
}

// ================= SETUP =================
void setup() {

  // WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  initLED();

  if (!initCamera()) {
    Serial.println("camera not initiated");
    while (true) {
      digitalWrite(LED_GPIO_NUM, HIGH);
      delay(200);
      digitalWrite(LED_GPIO_NUM, LOW);
      delay(200);
    }
  }
  else{
      Serial.println("camera initiated");
  }
  delay(500);
  
  // ─── WiFi Access Point ─────────────────────────────────
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,23),
                    IPAddress(192,168,4,23),
                    IPAddress(255,255,255,0));
  WiFi.softAP(ssid, password);

  Serial.println("Access Point started");
  Serial.print("SSID     : "); Serial.println(ssid);
  Serial.print("Password : "); Serial.println(password);
  Serial.print("IP       : http://"); Serial.print(WiFi.softAPIP()); Serial.println("/\n");

  startServer();
}

// ================= LOOP =================
void loop() {
  delay(10000);
}
