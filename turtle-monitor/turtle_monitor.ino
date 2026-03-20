// ══════════════════════════════════════════════════════════
// 🐢 烏龜計畫 — 封閉箱體環境監測系統 v2
// ESP32 + MH-Z19 (CO₂) + DHT22 (溫濕度) + OLED + 光敏電阻
// + SD 卡 + WiFi
// 每 5 分鐘記錄一次，CSV 格式存入 SD 卡
// OLED 即時顯示 CO₂/溫度/濕度/光照
// 可透過 WiFi 在瀏覽器即時查看數據
// ══════════════════════════════════════════════════════════

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <HardwareSerial.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <time.h>

// ── 使用者設定區 ──────────────────────────────────────────
// WiFi（不需要 WiFi 功能可以留空，系統照樣會存 SD 卡）
const char* WIFI_SSID     = "你的WiFi名稱";
const char* WIFI_PASSWORD = "你的WiFi密碼";

// NTP 時間同步（台灣 UTC+8）
const char* NTP_SERVER    = "pool.ntp.org";
const long  UTC_OFFSET    = 8 * 3600;

// 記錄間隔（毫秒）：5 分鐘 = 300000
const unsigned long LOG_INTERVAL_MS = 300000;

// OLED 刷新間隔（毫秒）：每 3 秒更新螢幕（不影響記錄間隔）
const unsigned long OLED_REFRESH_MS = 3000;

// 箱體編號（如果你有多個箱子，改成 "BOX-2" 等）
const char* BOX_ID = "BOX-1";

// CO₂ 警報閾值（ppm）——超過上限或低於下限時 LED 會快閃提醒開蓋通風
const int CO2_ALARM_LOW  = 150;
const int CO2_ALARM_HIGH = 5000;
// ── 設定區結束 ────────────────────────────────────────────

// ── 腳位定義 ─────────────────────────────────────────────
#define DHT_PIN       4
#define DHT_TYPE      DHT22
#define SD_CS_PIN     5
#define MHZ19_RX      16   // ESP32 RX2 ← MH-Z19 TX
#define MHZ19_TX      17   // ESP32 TX2 → MH-Z19 RX
#define LED_PIN       2    // 內建 LED（狀態指示）
#define LIGHT_PIN     34   // 光敏電阻（ADC，只能用 GPIO 32-39）
#define OLED_SDA      21   // I²C SDA（ESP32 預設）
#define OLED_SCL      22   // I²C SCL（ESP32 預設）

// OLED 設定
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1   // 無 reset 腳位
#define OLED_ADDR     0x3C // I²C 位址（常見 0x3C 或 0x3D）

// ── 全域物件 ─────────────────────────────────────────────
DHT dht(DHT_PIN, DHT_TYPE);
HardwareSerial mhzSerial(2);
WebServer server(80);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ── 狀態變數 ─────────────────────────────────────────────
float    lastTemp     = 0;
float    lastHumidity = 0;
int      lastCO2      = 0;
int      lastLight    = 0;    // 光照原始值 0-4095（12-bit ADC）
unsigned long lastLogTime    = 0;
unsigned long lastOLEDTime   = 0;
unsigned long totalReadings  = 0;
bool     sdReady      = false;
bool     wifiReady    = false;
bool     oledReady    = false;
String   logFileName  = "";

// OLED 顯示頁面輪播（每次刷新切換）
int oledPage = 0;

// ══════════════════════════════════════════════════════════
//  MH-Z19 UART 讀取（9-byte 命令協議）
// ══════════════════════════════════════════════════════════
int readCO2() {
  byte cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
  byte response[9];

  while (mhzSerial.available()) mhzSerial.read();

  mhzSerial.write(cmd, 9);
  delay(100);

  if (mhzSerial.available() < 9) return -1;
  mhzSerial.readBytes(response, 9);

  if (response[0] != 0xFF || response[1] != 0x86) return -2;

  byte checksum = 0;
  for (int i = 1; i < 8; i++) checksum += response[i];
  checksum = 0xFF - checksum + 1;
  if (checksum != response[8]) return -3;

  int co2 = (int)response[2] * 256 + (int)response[3];
  return co2;
}

// ══════════════════════════════════════════════════════════
//  光敏電阻讀取
//  回傳 0-4095（ESP32 12-bit ADC）
//  數值越大 = 光線越強；數值越小 = 越暗
//  也同時計算近似 lux 值供參考
// ══════════════════════════════════════════════════════════
int readLight() {
  // 多次取樣取平均，降低雜訊
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(LIGHT_PIN);
    delay(2);
  }
  return (int)(sum / 10);
}

// 粗略轉換成 lux（非精確，但足夠判斷燈亮/燈暗）
// 基於典型 10kΩ 光敏電阻 + 10kΩ 分壓電路
float adcToLux(int adcVal) {
  if (adcVal <= 0) return 0;
  float voltage = adcVal / 4095.0 * 3.3;
  float resistance = (3.3 - voltage) / voltage * 10000.0; // 10kΩ 分壓
  if (resistance <= 0) return 99999;
  // 近似公式：lux ≈ 500000 / R (for typical LDR)
  float lux = 500000.0 / resistance;
  return lux;
}

// ══════════════════════════════════════════════════════════
//  OLED 顯示
// ══════════════════════════════════════════════════════════
void updateOLED() {
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // 標題列
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("\x03 ");  // 小愛心符號
  display.print(BOX_ID);
  display.print(" ");
  display.print(getTimeStr());

  // 分隔線
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  if (oledPage == 0) {
    // ── 頁面 1：CO₂ + 溫度 + 濕度 ──
    // CO₂ 大字顯示
    display.setTextSize(2);
    display.setCursor(0, 14);
    if (lastCO2 > 0) {
      display.print(lastCO2);
      display.setTextSize(1);
      display.print(" ppm");
    } else {
      display.print("CO2 ERR");
    }

    // 警報提示
    if (lastCO2 > 0 && (lastCO2 < CO2_ALARM_LOW || lastCO2 > CO2_ALARM_HIGH)) {
      display.setTextSize(1);
      display.setCursor(0, 32);
      display.print("!! ALARM !!");
    }

    // 溫度和濕度
    display.setTextSize(1);
    display.setCursor(0, 42);
    display.print("Temp: ");
    if (lastTemp > -900) {
      display.print(lastTemp, 1);
      display.print(" C");
    } else {
      display.print("ERR");
    }

    display.setCursor(0, 54);
    display.print("Hum:  ");
    if (lastHumidity > -900) {
      display.print(lastHumidity, 1);
      display.print(" %");
    } else {
      display.print("ERR");
    }

  } else {
    // ── 頁面 2：光照 + 系統狀態 ──
    display.setTextSize(1);
    display.setCursor(0, 14);
    display.print("Light: ");
    display.print(lastLight);
    display.print(" (");
    float lux = adcToLux(lastLight);
    if (lux < 10)        display.print("DARK");
    else if (lux < 500)  display.print("DIM");
    else if (lux < 5000) display.print("NORMAL");
    else                  display.print("BRIGHT");
    display.print(")");

    display.setCursor(0, 26);
    display.print("~");
    display.print((int)lux);
    display.print(" lux");

    // 燈光狀態判斷（16L:8D 光週期）
    display.setCursor(0, 38);
    if (lux > 100) {
      display.print("LED: ON  (Light period)");
    } else {
      display.print("LED: OFF (Dark period)");
    }

    display.setCursor(0, 50);
    display.print("Readings: ");
    display.print(totalReadings);

    display.setCursor(0, 58);
    display.print("SD:");
    display.print(sdReady ? "OK" : "NO");
    display.print(" WiFi:");
    display.print(wifiReady ? "OK" : "NO");
  }

  display.display();

  // 輪播頁面
  oledPage = (oledPage + 1) % 2;
}

// ══════════════════════════════════════════════════════════
//  SD 卡：建立當日 CSV 檔案
// ══════════════════════════════════════════════════════════
String getDateStr() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "unknown";
  char buf[11];
  strftime(buf, sizeof(buf), "%Y-%m-%d", &timeinfo);
  return String(buf);
}

String getTimeStr() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "00:00:00";
  char buf[9];
  strftime(buf, sizeof(buf), "%H:%M:%S", &timeinfo);
  return String(buf);
}

void initLogFile() {
  logFileName = "/turtle_" + getDateStr() + ".csv";

  if (!SD.exists(logFileName)) {
    File f = SD.open(logFileName, FILE_WRITE);
    if (f) {
      // CSV 標頭——多了 light_raw 和 lux_approx 兩欄
      f.println("date,time,box_id,co2_ppm,temp_c,humidity_pct,light_raw,lux_approx,alarm");
      f.close();
      Serial.println("CSV: " + logFileName);
    }
  }
}

// ══════════════════════════════════════════════════════════
//  寫入一筆數據到 SD 卡
// ══════════════════════════════════════════════════════════
void logToSD(int co2, float temp, float hum, int light) {
  if (!sdReady) return;

  String todayFile = "/turtle_" + getDateStr() + ".csv";
  if (todayFile != logFileName) initLogFile();

  File f = SD.open(logFileName, FILE_APPEND);
  if (!f) {
    Serial.println("SD WRITE FAIL");
    return;
  }

  String alarm = "OK";
  if (co2 > 0 && co2 < CO2_ALARM_LOW)  alarm = "LOW_CO2";
  if (co2 > CO2_ALARM_HIGH)             alarm = "HIGH_CO2";
  if (co2 < 0)                          alarm = "SENSOR_ERR";

  float lux = adcToLux(light);

  // CSV: date,time,box_id,co2_ppm,temp_c,humidity_pct,light_raw,lux_approx,alarm
  f.print(getDateStr()); f.print(",");
  f.print(getTimeStr()); f.print(",");
  f.print(BOX_ID);       f.print(",");
  f.print(co2);          f.print(",");
  f.print(temp, 1);      f.print(",");
  f.print(hum, 1);       f.print(",");
  f.print(light);        f.print(",");
  f.print(lux, 1);       f.print(",");
  f.println(alarm);
  f.close();

  totalReadings++;
}

// ══════════════════════════════════════════════════════════
//  WiFi 網頁伺服器
// ══════════════════════════════════════════════════════════
void handleRoot() {
  float lux = adcToLux(lastLight);
  String lightStatus;
  if (lux > 100) lightStatus = "ON (Light period)";
  else           lightStatus = "OFF (Dark period)";

  String html = R"rawhtml(
<!DOCTYPE html>
<html><head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="30">
<title>🐢 烏龜計畫監測</title>
<style>
  body{font-family:system-ui;background:#0a0a0a;color:#e0e0e0;margin:0;padding:20px}
  h1{color:#4fc3f7;font-size:1.5em}
  .card{background:#1a1a2e;border-radius:12px;padding:20px;margin:10px 0;
        display:flex;justify-content:space-between;align-items:center}
  .label{color:#888;font-size:0.9em}
  .value{font-size:2.2em;font-weight:bold}
  .unit{font-size:0.5em;color:#888}
  .co2{color:#ff9800} .temp{color:#f44336} .hum{color:#4fc3f7} .light{color:#ffeb3b}
  .status{font-size:0.85em;color:#666;margin-top:15px}
  .alarm{color:#ff5252;font-weight:bold;animation:blink 1s infinite;
         background:#1a0000;padding:10px;border-radius:8px;margin:10px 0}
  .led-status{font-size:0.9em;padding:6px 12px;border-radius:20px;display:inline-block}
  .led-on{background:#1b5e20;color:#a5d6a7}
  .led-off{background:#1a1a2e;color:#616161}
  @keyframes blink{50%{opacity:0.3}}
</style>
</head><body>
<h1>🐢 烏龜計畫 — )rawhtml" + String(BOX_ID) + R"rawhtml(</h1>
<div class="card"><div><div class="label">CO₂ 濃度</div>
<div class="value co2">)rawhtml" + String(lastCO2) + R"rawhtml( <span class="unit">ppm</span></div></div></div>
<div class="card"><div><div class="label">溫度</div>
<div class="value temp">)rawhtml" + String(lastTemp, 1) + R"rawhtml( <span class="unit">°C</span></div></div></div>
<div class="card"><div><div class="label">濕度</div>
<div class="value hum">)rawhtml" + String(lastHumidity, 1) + R"rawhtml( <span class="unit">%</span></div></div></div>
<div class="card"><div><div class="label">光照強度</div>
<div class="value light">)rawhtml" + String((int)lux) + R"rawhtml( <span class="unit">lux</span></div>
<div class="led-status )rawhtml" + String(lux > 100 ? "led-on" : "led-off") + R"rawhtml(">💡 )rawhtml" + lightStatus + R"rawhtml(</div>
</div></div>
)rawhtml";

  if (lastCO2 > 0 && (lastCO2 < CO2_ALARM_LOW || lastCO2 > CO2_ALARM_HIGH)) {
    html += "<div class='alarm'>⚠️ CO₂ 超出安全範圍（"
          + String(CO2_ALARM_LOW) + "-" + String(CO2_ALARM_HIGH)
          + " ppm）！請開蓋通風 10 分鐘</div>";
  }

  html += "<div class='status'>累計 " + String(totalReadings) + " 筆 · "
        + "每 " + String(LOG_INTERVAL_MS / 60000) + " 分鐘 · "
        + getDateStr() + " " + getTimeStr() + "</div>";
  html += "<p class='status'><a href='/csv' style='color:#4fc3f7'>📥 今日 CSV</a>"
          " · <a href='/all' style='color:#4fc3f7'>📊 所有檔案</a>"
          " · <a href='/api' style='color:#4fc3f7'>🔗 JSON API</a></p>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleCSV() {
  if (!sdReady || !SD.exists(logFileName)) {
    server.send(404, "text/plain", "No data yet");
    return;
  }
  File f = SD.open(logFileName, FILE_READ);
  server.streamFile(f, "text/csv");
  f.close();
}

void handleAll() {
  if (!sdReady) {
    server.send(500, "text/plain", "SD not ready");
    return;
  }
  String json = "[";
  File root = SD.open("/");
  bool first = true;
  while (File entry = root.openNextFile()) {
    String name = String(entry.name());
    if (name.startsWith("turtle_") && name.endsWith(".csv")) {
      if (!first) json += ",";
      json += "{\"name\":\"" + name + "\",\"size\":" + String(entry.size()) + "}";
      first = false;
    }
    entry.close();
  }
  root.close();
  json += "]";
  server.send(200, "application/json", json);
}

void handleAPI() {
  float lux = adcToLux(lastLight);
  String json = "{";
  json += "\"box\":\"" + String(BOX_ID) + "\",";
  json += "\"co2\":" + String(lastCO2) + ",";
  json += "\"temp\":" + String(lastTemp, 1) + ",";
  json += "\"humidity\":" + String(lastHumidity, 1) + ",";
  json += "\"light_raw\":" + String(lastLight) + ",";
  json += "\"lux\":" + String(lux, 1) + ",";
  json += "\"readings\":" + String(totalReadings) + ",";
  json += "\"time\":\"" + getDateStr() + " " + getTimeStr() + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// ══════════════════════════════════════════════════════════
//  LED 狀態指示
// ══════════════════════════════════════════════════════════
void blinkLED(int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_PIN, LOW);
    delay(delayMs);
  }
}

// ══════════════════════════════════════════════════════════
//  setup()
// ══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("\n🐢 烏龜計畫環境監測系統 v2");
  Serial.println("═══════════════════════════════");

  pinMode(LED_PIN, OUTPUT);
  pinMode(LIGHT_PIN, INPUT);

  // 1. 初始化 OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledReady = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.println("Project Turtle");
    display.setCursor(10, 35);
    display.println("v2 Starting...");
    display.display();
    Serial.println("✓ OLED 就緒");
  } else {
    Serial.println("✗ OLED 初始化失敗（繼續運作）");
  }

  // 2. 初始化 DHT22
  dht.begin();
  Serial.println("✓ DHT22 就緒");

  // 3. 初始化 MH-Z19（UART2，9600 baud）
  mhzSerial.begin(9600, SERIAL_8N1, MHZ19_RX, MHZ19_TX);
  Serial.println("✓ MH-Z19 UART 就緒");
  Serial.println("  MH-Z19 預熱中（前 3 分鐘數據可能不準）");

  // 4. 初始化 SD 卡
  if (SD.begin(SD_CS_PIN)) {
    sdReady = true;
    Serial.println("✓ SD 卡就緒（" + String(SD.totalBytes() / 1048576) + " MB）");
  } else {
    Serial.println("✗ SD 卡失敗（數據僅顯示）");
  }

  // 5. 測試光敏電阻
  int testLight = readLight();
  Serial.println("✓ 光敏電阻就緒（目前值: " + String(testLight) + "）");

  // 6. 連接 WiFi
  if (strlen(WIFI_SSID) > 0 && String(WIFI_SSID) != "你的WiFi名稱") {
    Serial.print("  WiFi 連接中...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      wifiReady = true;
      Serial.println("\n✓ WiFi: " + WiFi.localIP().toString());

      configTime(UTC_OFFSET, 0, NTP_SERVER);
      Serial.println("✓ NTP 時間同步完成");

      server.on("/",    handleRoot);
      server.on("/csv", handleCSV);
      server.on("/all", handleAll);
      server.on("/api", handleAPI);
      server.begin();
      Serial.println("✓ 網頁: http://" + WiFi.localIP().toString());
    } else {
      Serial.println("\n⚠ WiFi 失敗，離線模式");
    }
  } else {
    Serial.println("  WiFi 未設定，離線模式");
  }

  // 7. 建立 CSV 檔案
  if (sdReady) initLogFile();

  // OLED 顯示啟動完成
  if (oledReady) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("System Ready!");
    display.println();
    display.print("SD:   "); display.println(sdReady ? "OK" : "FAIL");
    display.print("WiFi: "); display.println(wifiReady ? WiFi.localIP().toString() : "OFF");
    display.print("OLED: OK");
    display.display();
    delay(2000);
  }

  Serial.println("═══════════════════════════════");
  Serial.println("🐢 就緒！每 " + String(LOG_INTERVAL_MS / 60000) + " 分鐘記錄");
  Serial.println("═══════════════════════════════\n");

  blinkLED(3, 200);
  lastLogTime = millis() - LOG_INTERVAL_MS;  // 立即記錄第一筆
}

// ══════════════════════════════════════════════════════════
//  loop()
// ══════════════════════════════════════════════════════════
void loop() {
  if (wifiReady) server.handleClient();

  unsigned long now = millis();

  // ── OLED 即時刷新（每 3 秒） ──
  if (now - lastOLEDTime >= OLED_REFRESH_MS) {
    lastOLEDTime = now;

    // 即時讀取感測器給 OLED 顯示（不寫入 SD）
    lastLight = readLight();

    // CO₂ 不要太頻繁讀（MH-Z19 建議間隔 > 2 秒）
    // OLED 顯示用上次記錄的 CO₂ 值即可

    updateOLED();
  }

  // ── 每 5 分鐘正式記錄 ──
  if (now - lastLogTime < LOG_INTERVAL_MS) return;
  lastLogTime = now;

  // 讀取所有感測器
  float temp  = dht.readTemperature();
  float hum   = dht.readHumidity();
  int   co2   = readCO2();
  int   light = readLight();

  // DHT 重試
  if (isnan(temp) || isnan(hum)) {
    delay(2000);
    temp = dht.readTemperature();
    hum  = dht.readHumidity();
  }

  // 更新全域狀態
  lastTemp     = isnan(temp) ? -999 : temp;
  lastHumidity = isnan(hum)  ? -999 : hum;
  lastCO2      = co2;
  lastLight    = light;

  // Serial 輸出
  float lux = adcToLux(light);
  Serial.printf("[%s %s] CO2=%dppm  T=%.1fC  H=%.1f%%  Light=%d(~%.0flux)\n",
    getDateStr().c_str(), getTimeStr().c_str(),
    co2, lastTemp, lastHumidity, light, lux);

  // 存入 SD 卡
  logToSD(co2, lastTemp, lastHumidity, light);

  // 更新 OLED
  updateOLED();

  // LED 狀態
  if (co2 > 0 && (co2 < CO2_ALARM_LOW || co2 > CO2_ALARM_HIGH)) {
    blinkLED(10, 100);
  } else {
    blinkLED(1, 100);
  }
}
