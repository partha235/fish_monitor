// this program capture images

#include <Arduino.h>
#include "esp_camera.h"
#include <esp_log.h>

// Logging tag
static const char *TAG = "CAMERA";

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
    config.pixel_format = PIXFORMAT_JPEG;  // Required for streaming
    config.jpeg_quality = 12;              // Start reasonable
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_LATEST; // Better for streaming
    config.fb_location = CAMERA_FB_IN_PSRAM; // Prefer PSRAM if available

    // Try higher res if PSRAM available
    if (psramFound()) {
        Serial.println("PSRAM FOUND - enabling higher quality");
        config.frame_size = FRAMESIZE_SVGA;  // 800x600 — good start for OV3660
        // config.frame_size = FRAMESIZE_UXGA; // 1600x1200 — try this if stable
        config.jpeg_quality = 10;
        config.fb_count = 2;                 // Double buffering reduces lag
    } else {
        Serial.println("No PSRAM - limiting resolution");
        config.frame_size = FRAMESIZE_VGA;   // 640x480 fallback
        config.fb_location = CAMERA_FB_IN_DRAM;
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed with error 0x%x\n", err);
        return false;
    }

}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\nESP32-S3 Camera Test");

    pinMode(LED_GPIO_NUM, OUTPUT);
    digitalWrite(LED_GPIO_NUM, LOW);

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

    Serial.println("Camera ready. Capturing every 3s...");
}

void loop() {
    // digitalWrite(LED_GPIO_NUM, HIGH);  // LED on during capture

    // camera_fb_t *fb = esp_camera_fb_get();
    // if (!fb) {
    //     ESP_LOGE(TAG, "Capture failed");
    //     digitalWrite(LED_GPIO_NUM, LOW);
    //     delay(1000);
    //     return;
    // }

    // Serial.printf("Captured %dx%d JPEG → %u bytes\n", fb->width, fb->height, fb->len);

    // // Optional: show JPEG start bytes (FF D8 FF ...)
    // if (fb->len > 10) {
    //     Serial.print("Header: ");
    //     for (int i = 0; i < 10; i++) {
    //         Serial.printf("%02X ", fb->buf[i]);
    //     }
    //     Serial.println();
    // }

    // esp_camera_fb_return(fb);
    // digitalWrite(LED_GPIO_NUM, LOW);

    // delay(3000);
}