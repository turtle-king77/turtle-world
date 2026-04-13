# ============================================================
# ESP32 設定檔 — 請填入實際數值後上傳至開發板
# ============================================================

# WiFi
WIFI_SSID     = 'YOUR_WIFI_SSID'
WIFI_PASSWORD = 'YOUR_WIFI_PASSWORD'

# turtle-monitor 伺服器（執行 docker-compose up -d 的那台電腦 IP）
SERVER_URL = 'http://192.168.1.100:8080'

# 裝置識別名稱（多台時請設不同名稱）
DEVICE_ID = 'esp32-turtle'

# 回報間隔（秒），預設 5 分鐘
REPORT_INTERVAL = 300

# ── 感測器接腳 ──────────────────────────────────────────────
DHT_PIN       = 4    # DHT11 資料腳
PIR_PIN       = 25   # HC-SR501 OUT
LIGHT_ADC_PIN = 34   # 光敏電阻（ADC1）
BUZZER_PIN    = 32   # 有源蜂鳴器
RELAY1_PIN    = 26   # 繼電器通道 1
RELAY2_PIN    = 27   # 繼電器通道 2

# MH-Z19B 接 UART2
CO2_TX_PIN = 17
CO2_RX_PIN = 16

# OLED SSD1306（I2C）
OLED_SCL = 22
OLED_SDA = 21

# ── 警報閾值 ────────────────────────────────────────────────
CO2_WARN_PPM  = 1000   # 黃燈警告
CO2_ALERT_PPM = 2000   # 紅燈 + 蜂鳴器
TEMP_ALERT_C  = 35     # 高溫警告
