"""
Turtle Monitor — IoT 感測器資料接收伺服器
接收 ESP32 / ESP8266 感測器資料，儲存於 SQLite，供 n8n / OpenClaw 查詢。
"""

import json
import os
import sqlite3
from datetime import datetime

from flask import Flask, jsonify, render_template_string, request

app = Flask(__name__)

DB_PATH = os.environ.get('DB_PATH', '/app/mac_data/sensors.db')

# ── 狀態頁面 HTML ─────────────────────────────────────────────────────────────

_HTML = """<!DOCTYPE html>
<html lang="zh-TW">
<head>
  <meta charset="utf-8">
  <meta http-equiv="refresh" content="60">
  <title>Turtle Monitor</title>
  <style>
    body  { font-family: monospace; background:#0d1117; color:#c9d1d9; padding:20px; }
    h1    { color:#3fb950; }
    p     { color:#8b949e; font-size:.9em; }
    table { border-collapse:collapse; width:100%; }
    th    { color:#8b949e; padding:8px 12px; text-align:left;
            border-bottom:2px solid #30363d; }
    td    { padding:8px 12px; border-bottom:1px solid #21262d; }
    .ok   { color:#3fb950; }
    .warn { color:#d29922; }
    .bad  { color:#f85149; }
  </style>
</head>
<body>
<h1>🐢 Turtle Monitor</h1>
<p>最後更新：{{ now }} ｜ 每 60 秒自動重整</p>
<table>
  <tr>
    <th>裝置</th><th>時間</th>
    <th>溫度</th><th>濕度</th><th>CO₂</th>
    <th>土壤濕度</th><th>光線</th><th>移動</th>
  </tr>
  {% for r in rows %}
  <tr>
    <td>{{ r.device_id }}</td>
    <td>{{ r.timestamp[:19] }}</td>
    <td class="{{ 'bad' if r.temperature and r.temperature > 35 else 'ok' }}">
      {{ r.temperature if r.temperature is not none else '—' }}°C
    </td>
    <td>{{ r.humidity if r.humidity is not none else '—' }}%</td>
    <td class="{{ 'bad' if r.co2 and r.co2 > 2000 else ('warn' if r.co2 and r.co2 > 1000 else 'ok') }}">
      {{ r.co2 if r.co2 is not none else '—' }} ppm
    </td>
    <td>{{ r.soil_moisture if r.soil_moisture is not none else '—' }}</td>
    <td>{{ r.light_level if r.light_level is not none else '—' }}</td>
    <td>{{ '✓' if r.motion else '—' }}</td>
  </tr>
  {% endfor %}
</table>
</body>
</html>"""


# ── 資料庫 ────────────────────────────────────────────────────────────────────

def _db() -> sqlite3.Connection:
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def _init_db() -> None:
    conn = _db()
    conn.execute("""
        CREATE TABLE IF NOT EXISTS sensor_readings (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id     TEXT    NOT NULL,
            timestamp     TEXT    NOT NULL,
            temperature   REAL,
            humidity      REAL,
            co2           INTEGER,
            soil_moisture INTEGER,
            light_level   INTEGER,
            motion        INTEGER DEFAULT 0,
            raw_data      TEXT
        )
    """)
    conn.commit()
    conn.close()


# ── 路由 ──────────────────────────────────────────────────────────────────────

@app.after_request
def _cors(response):
    response.headers['Access-Control-Allow-Origin'] = '*'
    return response


@app.route('/')
def status_page():
    conn = _db()
    rows = conn.execute("""
        SELECT * FROM sensor_readings
        WHERE id IN (SELECT MAX(id) FROM sensor_readings GROUP BY device_id)
        ORDER BY timestamp DESC
    """).fetchall()
    conn.close()
    return render_template_string(
        _HTML,
        rows=[dict(r) for r in rows],
        now=datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
    )


@app.route('/health')
def health():
    return jsonify({'status': 'ok', 'time': datetime.now().isoformat()})


@app.route('/sensors', methods=['POST'])
def receive():
    data = request.get_json(silent=True)
    if not data:
        return jsonify({'error': 'invalid JSON'}), 400

    conn = _db()
    conn.execute("""
        INSERT INTO sensor_readings
            (device_id, timestamp, temperature, humidity,
             co2, soil_moisture, light_level, motion, raw_data)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    """, (
        data.get('device_id', 'unknown'),
        data.get('timestamp', datetime.now().isoformat()),
        data.get('temperature'),
        data.get('humidity'),
        data.get('co2'),
        data.get('soil_moisture'),
        data.get('light_level'),
        int(bool(data.get('motion', 0))),
        json.dumps(data),
    ))
    conn.commit()
    conn.close()
    return jsonify({'status': 'ok'}), 200


@app.route('/sensors/latest')
def latest():
    """每台裝置的最新讀數。可加 ?device_id=xxx 只查一台。"""
    device_id = request.args.get('device_id')
    conn = _db()
    if device_id:
        row = conn.execute(
            'SELECT * FROM sensor_readings WHERE device_id=? ORDER BY id DESC LIMIT 1',
            (device_id,),
        ).fetchone()
        conn.close()
        return (jsonify(dict(row)) if row else jsonify({'error': 'no data'}), 404)[0 if row else 1]
    rows = conn.execute("""
        SELECT * FROM sensor_readings
        WHERE id IN (SELECT MAX(id) FROM sensor_readings GROUP BY device_id)
        ORDER BY timestamp DESC
    """).fetchall()
    conn.close()
    return jsonify([dict(r) for r in rows])


@app.route('/sensors/history')
def history():
    """回傳指定裝置的歷史讀數。?device_id=xxx&limit=100"""
    device_id = request.args.get('device_id', 'esp32-turtle')
    limit = min(int(request.args.get('limit', 100)), 1000)
    conn = _db()
    rows = conn.execute(
        'SELECT * FROM sensor_readings WHERE device_id=? ORDER BY id DESC LIMIT ?',
        (device_id, limit),
    ).fetchall()
    conn.close()
    return jsonify([dict(r) for r in rows])


@app.route('/sensors/summary')
def summary():
    """過去 24 小時各裝置的平均值，供 OpenClaw turtle-monitor skill 使用。"""
    conn = _db()
    rows = conn.execute("""
        SELECT
            device_id,
            ROUND(AVG(temperature), 1)  AS avg_temp,
            ROUND(AVG(humidity),    1)  AS avg_humidity,
            ROUND(AVG(co2),         0)  AS avg_co2,
            MAX(co2)                    AS max_co2,
            ROUND(AVG(soil_moisture), 0) AS avg_soil,
            COUNT(*)                    AS readings,
            MAX(timestamp)              AS last_seen
        FROM sensor_readings
        WHERE timestamp >= datetime('now', '-24 hours')
        GROUP BY device_id
    """).fetchall()
    conn.close()
    return jsonify([dict(r) for r in rows])


# ── 啟動 ──────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    _init_db()
    app.run(host='0.0.0.0', port=8080)
