"""
ESP8266 自動澆水韌體 — 烏龜計畫
硬體：DHT22（溫濕度）、土壤濕度感測器（A0）、繼電器 + 沉水式抽水馬達

燒錄方式：
  1. 用 Thonny 連接 ESP8266
  2. 填好 config.py 中的 WiFi 和 SERVER_URL
  3. 上傳 config.py 和此 main.py
  4. 重啟開發板

接線：
  DHT22 DATA → GPIO4 (D2)
  土壤感測器 AO → A0
  繼電器 IN   → GPIO5 (D1)
  繼電器 VCC  → 3V3，GND → GND
  抽水馬達接繼電器 NO/COM（5V 外部電源）
"""

import network
import urequests
import ujson
import utime
from machine import ADC, Pin

import dht

from config import (
    DEVICE_ID, DHT_PIN, RELAY_PIN, REPORT_INTERVAL,
    SERVER_URL, SOIL_DRY_THRESHOLD, WATER_DURATION_SEC,
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


# ── 澆水 ─────────────────────────────────────────────────────────────────────

def water_plant(relay: Pin) -> None:
    print('開始澆水 {}s...'.format(WATER_DURATION_SEC))
    relay.value(0)                        # 繼電器 ON（低電位觸發）
    utime.sleep(WATER_DURATION_SEC)
    relay.value(1)                        # 繼電器 OFF
    print('澆水完成')


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
    relay      = Pin(RELAY_PIN, Pin.OUT, value=1)   # 預設 OFF
    soil_adc   = ADC(0)                              # A0，0–1023
    dht_sensor = dht.DHT22(Pin(DHT_PIN))

    connected = connect_wifi()

    while True:
        # ── 讀取溫濕度 ────────────────────────────────────────
        temp = humidity = None
        try:
            dht_sensor.measure()
            temp     = dht_sensor.temperature()
            humidity = dht_sensor.humidity()
        except Exception as e:
            print('DHT22 讀取失敗:', e)

        # ── 讀取土壤濕度 ──────────────────────────────────────
        soil = soil_adc.read()   # 低 = 濕，高 = 乾
        print('T={}  H={}  Soil={}'.format(temp, humidity, soil))

        # ── 判斷是否澆水 ──────────────────────────────────────
        watered = False
        if soil > SOIL_DRY_THRESHOLD:
            water_plant(relay)
            watered = True

        # ── 上傳資料 ──────────────────────────────────────────
        if connected:
            connected = connect_wifi()
        if connected:
            post_data({
                'device_id':    DEVICE_ID,
                'temperature':  temp,
                'humidity':     humidity,
                'soil_moisture': soil,
                'watered':      watered,
            })

        utime.sleep(REPORT_INTERVAL)


main()
