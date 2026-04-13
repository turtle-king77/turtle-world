"""
ESP32 環境監測韌體 — 烏龜計畫
硬體：DHT11、MH-Z19B CO₂、光敏電阻、HC-SR501 PIR、SSD1306 OLED、有源蜂鳴器、繼電器

燒錄方式：
  1. 用 Thonny 連接 ESP32
  2. 上傳 config.py（填好 WiFi 和 SERVER_URL 後）
  3. 上傳此 main.py
  4. 重啟開發板

若 OLED 無法使用，先在 Thonny Shell 執行：
  import mip; mip.install('ssd1306')
"""

import network
import urequests
import ujson
import utime
from machine import ADC, I2C, Pin, UART

import dht
import ssd1306

from config import (
    CO2_ALERT_PPM, CO2_RX_PIN, CO2_TX_PIN, CO2_WARN_PPM,
    BUZZER_PIN, DEVICE_ID, DHT_PIN, LIGHT_ADC_PIN,
    OLED_SCL, OLED_SDA, PIR_PIN, RELAY1_PIN, RELAY2_PIN,
    REPORT_INTERVAL, SERVER_URL, TEMP_ALERT_C,
    WIFI_PASSWORD, WIFI_SSID,
)


# ── WiFi ─────────────────────────────────────────────────────────────────────

def connect_wifi() -> bool:
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if wlan.isconnected():
        return True
    print('連線 WiFi:', WIFI_SSID)
    wlan.connect(WIFI_SSID, WIFI_PASSWORD)
    for _ in range(20):
        if wlan.isconnected():
            print('IP:', wlan.ifconfig()[0])
            return True
        utime.sleep(1)
    print('WiFi 連線失敗')
    return False


# ── MH-Z19B CO₂ ──────────────────────────────────────────────────────────────

_co2_uart = None

def _get_co2_uart():
    global _co2_uart
    if _co2_uart is None:
        _co2_uart = UART(2, baudrate=9600, tx=CO2_TX_PIN, rx=CO2_RX_PIN)
    return _co2_uart


def read_co2() -> int | None:
    uart = _get_co2_uart()
    uart.write(b'\xff\x01\x86\x00\x00\x00\x00\x00\x79')
    utime.sleep_ms(150)
    resp = uart.read(9)
    if resp and len(resp) == 9 and resp[0] == 0xFF and resp[1] == 0x86:
        # 校驗和
        checksum = (~sum(resp[1:8]) + 1) & 0xFF
        if checksum == resp[8]:
            return resp[2] * 256 + resp[3]
    return None


# ── OLED 顯示 ─────────────────────────────────────────────────────────────────

def update_oled(oled, temp, humidity, co2, light, motion) -> None:
    oled.fill(0)
    oled.text('Turtle Monitor', 0, 0)
    oled.text(
        'T:{} H:{}%'.format(
            '{}C'.format(temp) if temp is not None else '--',
            humidity if humidity is not None else '--',
        ),
        0, 12,
    )
    oled.text(
        'CO2:{} ppm'.format(co2 if co2 is not None else '---'),
        0, 24,
    )
    oled.text('Light:{}'.format(light), 0, 36)
    oled.text('PIR:{}'.format('ON' if motion else 'OFF'), 0, 48)
    oled.show()


# ── 警報邏輯 ──────────────────────────────────────────────────────────────────

def check_alerts(buzzer, temp, co2) -> None:
    if co2 is not None and co2 > CO2_ALERT_PPM:
        # CO₂ 超標：蜂鳴兩聲
        for _ in range(2):
            buzzer.value(1)
            utime.sleep_ms(300)
            buzzer.value(0)
            utime.sleep_ms(200)
    elif temp is not None and temp > TEMP_ALERT_C:
        # 高溫：蜂鳴一聲
        buzzer.value(1)
        utime.sleep_ms(500)
        buzzer.value(0)


# ── 資料上傳 ──────────────────────────────────────────────────────────────────

def post_data(payload: dict) -> bool:
    try:
        r = urequests.post(
            SERVER_URL + '/sensors',
            headers={'Content-Type': 'application/json'},
            data=ujson.dumps(payload),
        )
        ok = r.status_code == 200
        r.close()
        return ok
    except Exception as e:
        print('上傳失敗:', e)
        return False


# ── 主程式 ────────────────────────────────────────────────────────────────────

def main() -> None:
    # 初始化硬體
    i2c = I2C(0, scl=Pin(OLED_SCL), sda=Pin(OLED_SDA))
    oled = ssd1306.SSD1306_I2C(128, 64, i2c)

    dht_sensor = dht.DHT11(Pin(DHT_PIN))
    pir        = Pin(PIR_PIN, Pin.IN)
    light_adc  = ADC(Pin(LIGHT_ADC_PIN))
    light_adc.atten(ADC.ATTN_11DB)   # 量測範圍 0–3.3V
    buzzer     = Pin(BUZZER_PIN, Pin.OUT, value=0)
    relay1     = Pin(RELAY1_PIN, Pin.OUT, value=1)  # 預設 OFF（高電位）
    relay2     = Pin(RELAY2_PIN, Pin.OUT, value=1)
    _ = relay1, relay2  # 預留繼電器控制

    oled.fill(0)
    oled.text('Turtle Monitor', 0, 0)
    oled.text('Starting...', 0, 20)
    oled.show()

    connected = connect_wifi()

    while True:
        # ── 讀取感測器 ────────────────────────────────────────
        temp = humidity = None
        try:
            dht_sensor.measure()
            temp     = dht_sensor.temperature()
            humidity = dht_sensor.humidity()
        except Exception as e:
            print('DHT11 讀取失敗:', e)

        co2    = read_co2()
        light  = 4095 - light_adc.read()   # 反轉：數值越大越亮
        motion = pir.value()

        print('T={}  H={}  CO2={}  Light={}  PIR={}'.format(
            temp, humidity, co2, light, motion))

        # ── 更新顯示 ──────────────────────────────────────────
        update_oled(oled, temp, humidity, co2, light, motion)

        # ── 警報檢查 ──────────────────────────────────────────
        check_alerts(buzzer, temp, co2)

        # ── 上傳資料 ──────────────────────────────────────────
        if connected:
            connected = connect_wifi()   # 重連（若斷線）
        if connected:
            post_data({
                'device_id':   DEVICE_ID,
                'temperature': temp,
                'humidity':    humidity,
                'co2':         co2,
                'light_level': light,
                'motion':      motion,
            })

        utime.sleep(REPORT_INTERVAL)


main()
