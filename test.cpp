#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include "esp_camera.h"
#include <esp_http_server.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>

volatile uint32_t captureCount = 0;
volatile float currentDistance = 0.0f;
volatile float waterTemp   = -99.0f;   // invalid = -99
volatile float surfaceTemp = -99.0f;
volatile float pressure_hPa = -1.0f;
volatile float altitude_m  = -1.0f;

bool bmp_ok = false;

// ================= WIFI =================
const char* ssid     = "bps_wifi";
const char* password = "sagabps@235";

// ================= PINS =================
#define TRIG_PIN     42
#define ECHO_PIN     41
#define DHT11_PIN    47
#define ONE_WIRE_BUS 2     // DS18B20 – change if needed

// I2C – MOVED AWAY FROM CAMERA PINS (was conflicting on GPIO8)
#define I2C_SDA      21     // ← changed – use free pin
#define I2C_SCL      14     // ← changed – use free pin

#define LED_GPIO_NUM 2

// ================= SENSORS =================
DHT dht(DHT11_PIN, DHT11);
Adafruit_BMP085 bmp;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);



float duration, distance;

float getDistanceCM(){
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH);
  distance = (duration*.0343)/2;
  Serial.print("Distance: ");
  Serial.println(distance);
  return distance;

}

// ================= CAMERA PINS (ESP32-S3) =================
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y2_GPIO_NUM      11
#define Y3_GPIO_NUM       9
#define Y4_GPIO_NUM       8   // now free for camera (was conflicting with old SDA)
#define Y5_GPIO_NUM      10
#define Y6_GPIO_NUM      12
#define Y7_GPIO_NUM      18
#define Y8_GPIO_NUM      17
#define Y9_GPIO_NUM      16
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13

// ================= HTML (unchanged except minor label clarity) =================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fish Monitor System</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
@import url('https://fonts.googleapis.com/css2?family=Poppins:wght@300;500;700&display=swap');
body{font-family:'Poppins',sans-serif;background:linear-gradient(135deg,#0f2027,#203a43,#2c5364);color:#fff;margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center}
.container{background:rgba(255,255,255,0.1);backdrop-filter:blur(12px);border-radius:20px;padding:35px;box-shadow:0 10px 40px rgba(0,0,0,0.5);text-align:center;width:90%;max-width:900px}
h1{font-size:2.4em;background:linear-gradient(90deg,#00dbde,#fc00ff);-webkit-background-clip:text;-webkit-text-fill-color:transparent}
img{width:80%;max-width:600px;margin:20px;border-radius:15px;border:4px solid #00ffea}
canvas{width:100%;max-width:700px;height:350px;margin:20px auto}
.readings{font-size:1.3em;margin-top:20px}
.value{font-weight:700;color:#00ffea}
.error{color:#ff6b6b}
.footer{margin-top:20px;font-size:0.9em;color:#88cccc}
</style>
</head>
<body>
<div class="container">
<h1>🐟 Fish Monitor System</h1>
<h2>Live Webcam Snapshot (~2s refresh)</h2>
<img id="liveCam" src="/capture?t=" alt="Snapshot">
<h2>Water Level (ultrasonic)</h2>
<canvas id="chart"></canvas>
<div class="readings">
    <div>🌡️ Water Temp (probe): <span class="value" id="waterTemp">---</span> °C</div>
    <div>🌡️ Surface Temp (DHT11): <span class="value" id="surfaceTemp">---</span> °C</div>
    <div>🗜️ Pressure: <span class="value" id="pressure">---</span> hPa</div>
    <div>🗻 Altitude: <span class="value" id="altitude">---</span> m</div>
    <div>📏 Distance: <span class="value" id="dist">---</span> cm</div>
</div>
<div class="footer">ESP32-S3 Camera + Sensors</div>
</div>
<script>
const ctx = document.getElementById('chart').getContext('2d');
const chart = new Chart(ctx, {
    type: 'line',
    data: { labels: [], datasets: [{
        label: 'Distance (cm)',
        borderColor: 'rgb(0,255,234)',
        backgroundColor: 'rgba(0,255,234,0.2)',
        data: [], fill: true, tension: 0.3
    }]},
    options: {
        responsive: true,
        scales: { x:{title:{display:true,text:'Time'}}, y:{title:{display:true,text:'Distance (cm)'}, suggestedMin:0, suggestedMax:400} }
    }
});
function updateChart(value) {
    if (chart.data.labels.length > 40) { chart.data.labels.shift(); chart.data.datasets[0].data.shift(); }
    chart.data.labels.push(new Date().toLocaleTimeString());
    chart.data.datasets[0].data.push(value);
    chart.update();
    document.getElementById('dist').textContent = value.toFixed(1);
}
setInterval(() => {
    document.getElementById('liveCam').src = '/capture?t=' + new Date().getTime();
    fetch('/sensors')
    .then(r => r.json())
    .then(d => {
        document.getElementById('waterTemp').textContent   = d.waterTemp   >= -50 ? d.waterTemp.toFixed(1)   : 'Error';
        document.getElementById('surfaceTemp').textContent = d.surfaceTemp >= -50 ? d.surfaceTemp.toFixed(1) : 'Error';
        document.getElementById('pressure').textContent    = d.pressure    >  0   ? d.pressure.toFixed(1)    : 'Error';
        document.getElementById('altitude').textContent    = d.altitude    > -1   ? d.altitude.toFixed(1)    : 'Error';
        document.getElementById('dist').textContent        = d.distance    >= 0   ? d.distance.toFixed(1)    : 'Error';
        if (d.distance >= 0) updateChart(d.distance);
    })
    .catch(e => console.error('Fetch failed:', e));
}, 2000);
</script>
</body>
</html>
)rawliteral";

// ================= CAMERA INIT =================
bool initCamera() {
    camera_config_t config{};
    config.ledc_channel    = LEDC_CHANNEL_0;
    config.ledc_timer      = LEDC_TIMER_0;
    config.pin_d0          = Y2_GPIO_NUM;
    config.pin_d1          = Y3_GPIO_NUM;
    config.pin_d2          = Y4_GPIO_NUM;
    config.pin_d3          = Y5_GPIO_NUM;
    config.pin_d4          = Y6_GPIO_NUM;
    config.pin_d5          = Y7_GPIO_NUM;
    config.pin_d6          = Y8_GPIO_NUM;
    config.pin_d7          = Y9_GPIO_NUM;
    config.pin_xclk        = XCLK_GPIO_NUM;
    config.pin_pclk        = PCLK_GPIO_NUM;
    config.pin_vsync       = VSYNC_GPIO_NUM;
    config.pin_href        = HREF_GPIO_NUM;
    config.pin_sccb_sda    = SIOD_GPIO_NUM;
    config.pin_sccb_scl    = SIOC_GPIO_NUM;
    config.pin_pwdn        = PWDN_GPIO_NUM;
    config.pin_reset       = RESET_GPIO_NUM;
    config.xclk_freq_hz    = 20000000;
    config.pixel_format    = PIXFORMAT_JPEG;

    if (psramFound()) {
        config.frame_size   = FRAMESIZE_VGA;
        config.jpeg_quality = 12;
        config.fb_count     = 2;
        config.fb_location  = CAMERA_FB_IN_PSRAM;
    } else {
        config.frame_size   = FRAMESIZE_QVGA;
        config.jpeg_quality = 15;
        config.fb_count     = 1;
        config.fb_location  = CAMERA_FB_IN_DRAM;
    }
    config.grab_mode = CAMERA_GRAB_LATEST;

    if (esp_camera_init(&config) != ESP_OK) return false;

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 1);
        s->set_contrast(s, 1);
        s->set_saturation(s, 0);
        s->set_exposure_ctrl(s, 1);
        s->set_ae_level(s, 1);
        s->set_aec_value(s, 400);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)4);
    }
    return true;
}

// ================= HANDLERS =================
static esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t sensors_handler(httpd_req_t *req) {
    char json[256];
    snprintf(json, sizeof(json),
             "{\"distance\":%.1f,\"waterTemp\":%.1f,\"surfaceTemp\":%.1f,\"pressure\":%.1f,\"altitude\":%.1f}",
             currentDistance, waterTemp, surfaceTemp, pressure_hPa, altitude_m);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    captureCount++;
    Serial.printf("Capture #%lu  size: %u bytes\n", captureCount, fb->len);

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return res;
}

// ================= SERVER =================
httpd_handle_t server = NULL;

void startServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;

    httpd_uri_t uris[] = {
        {"/",       HTTP_GET, index_handler,    NULL},
        {"/sensors", HTTP_GET, sensors_handler,  NULL},
        {"/capture", HTTP_GET, capture_handler,  NULL}
    };

    if (httpd_start(&server, &config) == ESP_OK) {
        for (auto &u : uris) httpd_register_uri_handler(server, &u);
        Serial.println("HTTP server started");
    } else {
        Serial.println("HTTP server failed");
    }
}

// ================= SETUP =================
void setup() {
    Serial.begin(115200);
    delay(300);

    pinMode(LED_GPIO_NUM, OUTPUT);
    digitalWrite(LED_GPIO_NUM, LOW);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);

    Serial.println("\n=== Sensor Init ===");
    dht.begin();

    Serial.print("DS18B20 devices found during init: ");
    ds18b20.begin();
    Serial.println(ds18b20.getDeviceCount());

    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("Scanning I2C bus...");
    int devices = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("I2C device found at 0x%02X\n", addr);
            devices++;
        }
    }
    if (devices == 0) Serial.println("No I2C devices found!");

    bmp_ok = bmp.begin();
    if (bmp_ok) {
        Serial.println("BMP180/085 initialized OK");
    } else {
        Serial.println("BMP180/085 NOT found – check wiring / pull-ups / address");
    }

    Serial.println("Initializing camera...");
    if (!initCamera()) {
        Serial.println("Camera failed → halting");
        while (true) delay(1000);
    }

    WiFi.begin(ssid, password);
    Serial.print("WiFi ");
    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }
    Serial.println("\nConnected → http://" + WiFi.localIP().toString());

    startServer();
}

// ================= LOOP =================
void loop() {
    currentDistance = getDistanceCM();

    // DHT11
    float t_dht = dht.readTemperature();
    if (!isnan(t_dht)) {
        surfaceTemp = t_dht;
    }

    // DS18B20
    ds18b20.requestTemperatures();
    float t_ds = ds18b20.getTempCByIndex(0);
    if (t_ds != DEVICE_DISCONNECTED_C && t_ds > -50 && t_ds < 100) {
        waterTemp = t_ds;
    }

    // BMP
    if (bmp_ok) {
        pressure_hPa = bmp.readPressure() / 100.0f;
        altitude_m   = bmp.readAltitude(1013.25);  // adjust sea-level hPa if known
    }

    // Debug print every cycle
    Serial.printf("Dist: %.1f cm | DHT: %.1f °C | DS18: %.1f °C | Pres: %.1f hPa | Alt: %.1f m\n",
                  currentDistance, surfaceTemp, waterTemp, pressure_hPa, altitude_m);

    delay(2000);
}