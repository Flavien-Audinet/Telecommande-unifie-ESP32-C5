#include <WiFi.h>
#include <Adafruit_NeoPixel.h>

// =====================
// AP 参数
// =====================
static const char *AP_SSID = "ESP32-C5_01";
static const char *AP_PASS = "12345678";   // 至少 8 位

// 可选：固定 AP 的网段（不想固定就注释掉这三行 + softAPConfig 那行）
IPAddress local_ip(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// =====================
// WS2812 参数
// =====================
#define WS2812_PIN   27      // 你的 WS2812 GPIO
#define WS2812_NUM   1       // 灯珠数量（灯带就改）
#define LED_BRIGHT   160     // 亮度 0~255

Adafruit_NeoPixel strip(WS2812_NUM, WS2812_PIN, NEO_GRB + NEO_KHZ800);

// 慢闪 500ms
static const uint32_t BLINK_MS = 500;

static int g_staCount = 0;
static bool g_blinkEnable = false;
static bool g_blinkOn = false;
static uint32_t g_lastBlink = 0;

// =====================
// 灯光工具函数
// =====================
static void led_set(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < WS2812_NUM; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }
  strip.show();
}

static void boot_animation() {
  strip.setBrightness(LED_BRIGHT);
  led_set(255, 0, 0);     delay(250);   // 红
  led_set(0, 255, 0);     delay(250);   // 绿
  led_set(0, 0, 255);     delay(250);   // 蓝
  led_set(255, 255, 255); delay(250);   // 白
}

static void show_idle_color() {
  // 无连接时：常亮白
  g_blinkEnable = false;
  g_blinkOn = false;
  led_set(255, 255, 255);
}

static void enable_connected_blink() {
  // 有连接时：橙色慢闪 500ms
  g_blinkEnable = true;
  g_blinkOn = true;
  g_lastBlink = millis();
  led_set(255, 120, 0); // 橙（可微调）
}

static void blink_task() {
  if (!g_blinkEnable) return;

  uint32_t now = millis();
  if (now - g_lastBlink >= BLINK_MS) {
    g_lastBlink = now;
    g_blinkOn = !g_blinkOn;

    if (g_blinkOn) led_set(255, 120, 0);  // 橙
    else           led_set(0, 0, 0);      // 灭
  }
}

// =====================
// WiFi 事件回调（Arduino-ESP32 3.x 推荐签名）
// =====================
void WiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  (void)info;

  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("[AP] Started");
      Serial.print("[AP] IP: ");
      Serial.println(WiFi.softAPIP());
      break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      g_staCount++;
      Serial.printf("[AP] Station connected, count=%d\n", g_staCount);
      enable_connected_blink();
      break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      if (g_staCount > 0) g_staCount--;
      Serial.printf("[AP] Station disconnected, count=%d\n", g_staCount);
      if (g_staCount <= 0) show_idle_color();
      break;

    default:
      break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);

  // WS2812 init
  strip.begin();
  strip.clear();
  strip.show();
  boot_animation();

  // WiFi AP init
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_AP);

  // 可选固定 IP
  WiFi.softAPConfig(local_ip, gateway, subnet);

  bool ok = WiFi.softAP(AP_SSID, AP_PASS);
  if (!ok) {
    Serial.println("[AP] softAP start FAILED!");
    led_set(255, 0, 0);
    while (true) delay(1000);
  }

  show_idle_color();

  Serial.println("================================");
  Serial.print("SSID: "); Serial.println(AP_SSID);
  Serial.print("PASS: "); Serial.println(AP_PASS);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  Serial.println("================================");
}

void loop() {
  blink_task();
  delay(5);
}
