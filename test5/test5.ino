/* Includes ---------------------------------------------------------------- */
#include <fishdetetions_inferencing.h>
#include "edge-impulse-sdk/dsp/image/image.hpp"

#include "esp_camera.h"

// Select camera model - find more camera models in camera_pins.h file here
// https://github.com/espressif/arduino-esp32/blob/master/libraries/ESP32/examples/Camera/CameraWebServer/camera_pins.h
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




/* Private variables ------------------------------------------------------- */
static bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
static bool is_initialised = false;

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
    config.pixel_format = PIXFORMAT_RGB565;  // Required for streaming
    config.jpeg_quality = 12;              // Start reasonable
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST; // Better for streaming
    config.fb_location = CAMERA_FB_IN_PSRAM; // Prefer PSRAM if available
  
    // Try higher res if PSRAM available
    if (psramFound()) {
        Serial.println("PSRAM FOUND - enabling higher quality");
        config.frame_size = FRAMESIZE_QVGA;  // 320x240  
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

            is_initialised = true;   // ✅ ADD THIS
            return true;
  
  }

/* Constant defines -------------------------------------------------------- */
#define EI_CAMERA_RAW_FRAME_BUFFER_COLS 320
#define EI_CAMERA_RAW_FRAME_BUFFER_ROWS 240
#define EI_CAMERA_FRAME_BYTE_SIZE 3

  uint8_t snapshot_buf[EI_CAMERA_RAW_FRAME_BUFFER_COLS *
    EI_CAMERA_RAW_FRAME_BUFFER_ROWS *
    EI_CAMERA_FRAME_BYTE_SIZE];

/**
* @brief      Arduino setup function
*/
void setup()
{
    Serial.begin(115200);
    delay(1000);

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


    //comment out the below line to start inference immediately after upload
    Serial.println("Edge Impulse Inferencing Demo");

    ei_printf("\nStarting continious inference in 2 seconds...\n");
    ei_sleep(2000);
}

/**
* @brief      Get data and run inferencing
*
* @param[in]  debug  Get debug info if true
*/
void loop()
{

    // instead of wait_ms, we'll wait on the signal, this allows threads to cancel us...
    if (ei_sleep(5) != EI_IMPULSE_OK) {
        return;
    }

    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = &ei_camera_get_data;

    if (ei_camera_capture(
        EI_CLASSIFIER_INPUT_WIDTH,
        EI_CLASSIFIER_INPUT_HEIGHT,
        snapshot_buf) == false) {
    ei_printf("Failed to capture image\r\n");
    return;
    }

    // Run the classifier
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, debug_nn);
    if (err != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", err);
        return;
    }

    // print the predictions
    ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \n",
                result.timing.dsp, result.timing.classification, result.timing.anomaly);

#if EI_CLASSIFIER_OBJECT_DETECTION == 1
    ei_printf("Object detection bounding boxes:\r\n");
    for (uint32_t i = 0; i < result.bounding_boxes_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.bounding_boxes[i];
        if (bb.value == 0) {
            continue;
        }
        ei_printf("  %s (%f) [ x: %u, y: %u, width: %u, height: %u ]\r\n",
                bb.label,
                bb.value,
                bb.x,
                bb.y,
                bb.width,
                bb.height);
    }

    // Print the prediction results (classification)
#else
    ei_printf("Predictions:\r\n");
    for (uint16_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        ei_printf("  %s: ", ei_classifier_inferencing_categories[i]);
        ei_printf("%.5f\r\n", result.classification[i].value);
    }
#endif

    // Print anomaly result (if it exists)
#if EI_CLASSIFIER_HAS_ANOMALY
    ei_printf("Anomaly prediction: %.3f\r\n", result.anomaly);
#endif

#if EI_CLASSIFIER_HAS_VISUAL_ANOMALY
    ei_printf("Visual anomalies:\r\n");
    for (uint32_t i = 0; i < result.visual_ad_count; i++) {
        ei_impulse_result_bounding_box_t bb = result.visual_ad_grid_cells[i];
        if (bb.value == 0) {
            continue;
        }
        ei_printf("  %s (%f) [ x: %u, y: %u, width: %u, height: %u ]\r\n",
                bb.label,
                bb.value,
                bb.x,
                bb.y,
                bb.width,
                bb.height);
    }
#endif



}

/**
 * @brief   Setup image sensor & start streaming
 *
 * @retval  false if initialisation failed
 */

/**
 * @brief      Stop streaming of sensor data
 */
void ei_camera_deinit(void) {

    //deinitialize the camera
    esp_err_t err = esp_camera_deinit();

    if (err != ESP_OK)
    {
        ei_printf("Camera deinit failed\n");
        return;
    }

    is_initialised = false;
    return;
}


/**
 * @brief      Capture, rescale and crop image
 *
 * @param[in]  img_width     width of output image
 * @param[in]  img_height    height of output image
 * @param[in]  out_buf       pointer to store output image, NULL may be used
 *                           if ei_camera_frame_buffer is to be used for capture and resize/cropping.
 *
 * @retval     false if not initialised, image captured, rescaled or cropped failed
 *
 */
 
 
bool ei_camera_capture(uint32_t img_width, uint32_t img_height, uint8_t *out_buf)
 {
     if (!is_initialised) {
         ei_printf("ERR: Camera not initialized\r\n");
         return false;
     }
 
     camera_fb_t *fb = esp_camera_fb_get();
     if (!fb) {
         ei_printf("Camera capture failed\n");
         return false;
     }
 
     // Convert RGB565 → RGB888 directly into snapshot_buf
     for (size_t i = 0, j = 0; i < fb->len; i += 2, j += 3) {
         uint16_t pixel = (fb->buf[i] << 8) | fb->buf[i + 1];
 
         snapshot_buf[j]     = ((pixel >> 11) & 0x1F) << 3;  // R
         snapshot_buf[j + 1] = ((pixel >> 5) & 0x3F) << 2;   // G
         snapshot_buf[j + 2] = (pixel & 0x1F) << 3;          // B
     }
 
     esp_camera_fb_return(fb);
 
     // Resize properly
     if (img_width != EI_CAMERA_RAW_FRAME_BUFFER_COLS ||
         img_height != EI_CAMERA_RAW_FRAME_BUFFER_ROWS) {
 
         ei::image::processing::crop_and_interpolate_rgb888(
             snapshot_buf,
             EI_CAMERA_RAW_FRAME_BUFFER_COLS,
             EI_CAMERA_RAW_FRAME_BUFFER_ROWS,
             out_buf,
             img_width,
             img_height
         );
     }
 
     return true;
 }


static int ei_camera_get_data(size_t offset, size_t length, float *out_ptr)
{
    size_t pixel_ix = offset * 3;

    for (size_t i = 0; i < length; i++) {

        uint8_t r = snapshot_buf[pixel_ix];
        uint8_t g = snapshot_buf[pixel_ix + 1];
        uint8_t b = snapshot_buf[pixel_ix + 2];

        out_ptr[i] = (r << 16) | (g << 8) | b;

        pixel_ix += 3;
    }

    return 0;
}


#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_CAMERA
#error "Invalid model for current sensor"
#endif