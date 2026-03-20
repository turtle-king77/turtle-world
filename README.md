# 🐢 烏龜計畫 — n8n 自動化系統

> 穩扎穩打，每天進步一點點

月球風化層生物復育研究（旺宏科學獎 2026）的 n8n 自動化工作流程 + OpenClaw AI 個人助手。

---

## 📦 系統架構

```
六個自動化 Workflow：
├── Workflow 1：🔬 文獻自動搜尋          每週一 08:00  PubMed → Ollama 翻譯 → Notion 文獻DB
├── Workflow 2：🤖 AI 企劃書完善         每週日 22:00  Ollama → 白皮書末尾追加建議
├── Workflow 3：📊 進度追蹤更新          每天   21:00  Notion DB → 主頁進度快照
├── Workflow 4：📝 週報自動生成          每週日 23:00  Ollama → Notion 研究週報
├── Workflow 5：🧠 多 AI 研究審查        每週三 20:00  Ollama×2 + Gemini → 白皮書
└── Workflow 6：🔍 Gemini 文獻探索       每週四 08:00  Gemini AI → 文獻資料庫
```

**AI 引擎**：
- **Ollama**（本地免費）— qwen2.5:14b，負責中文翻譯、企劃分析、週報生成
- **Gemini**（雲端免費）— gemini-2.0-flash，負責文獻探索、多角度審查
- **Claude**（雲端免費額度）— claude-3-5-sonnet，負責深度分析、研究審查
- **OpenClaw**（本地免費）— 個人 AI 助手，透過 Telegram/Discord 即時互動查詢

---

## 🚀 安裝步驟

### Step 1：安裝 Ollama（本地 AI）
前往 https://ollama.com 下載安裝，然後在終端機執行：
```bash
ollama pull qwen2.5:14b
```
（下載約 9GB，需等待幾分鐘）

### Step 2：安裝 Docker Desktop
前往 https://www.docker.com/products/docker-desktop/ 下載安裝。

💡 中文介面：安裝後可套用 [Docker Desktop 中文漢化包](https://github.com/asxez/DockerDesktop-CN)

### Step 3：建立 .env 檔案
```bash
cd /Users/cengzhenhong/烏龜世界
cp .env.example .env
```

編輯 `.env`，只需填入兩個免費 API key：

```
POSTGRES_PASSWORD=（自訂密碼）
N8N_ENCRYPTION_KEY=（執行 openssl rand -hex 24 產生）
NOTION_TOKEN=（從 Notion Integrations 取得，免費）
GEMINI_API_KEY=（從 Google AI Studio 取得，免費）
```

### Step 4：取得 Notion Integration Token
1. 前往 https://www.notion.so/my-integrations
2. 點「+ New integration」
3. 名稱填「烏龜計畫 n8n」，關聯工作區
4. 複製 "Internal Integration Token"（`secret_` 開頭）
5. 貼到 `.env` 的 `NOTION_TOKEN`

⚠️ **重要**：需要將 n8n integration 分享給以下 Notion 頁面：
- 烏龜計畫（主頁）
- 全戰略整合白皮書
- 實驗設計與分工
- 文獻資料庫（新建的）
- 研究週報（新建的）

在每個頁面右上角點「...」→「Add connections」→ 選「烏龜計畫 n8n」

### Step 5：取得 Gemini API Key
1. 前往 https://aistudio.google.com/app/apikey
2. 點「Create API key」
3. 複製 API key（`AIzaSy-` 開頭）
4. 貼到 `.env` 的 `GEMINI_API_KEY`

### Step 6：啟動 n8n
```bash
# 先確認 Ollama 正在執行（在另一個終端機）
ollama serve

# 啟動 n8n
docker-compose up -d
```

開啟瀏覽器前往：http://localhost:5678

### Step 7：匯入 Workflows

在 n8n 介面：
1. 左側選單點「Workflows」
2. 點右上角「Add workflow」→「Import from file」
3. 依序匯入 `workflows/` 資料夾中的 6 個 JSON 檔案

### Step 8：驗證並啟用
1. 在每個 Workflow 點「Execute Workflow」手動測試一次
2. 確認 Notion 有正確更新後，點左上角的開關啟用排程

### Step 9：安裝 OpenClaw AI 助手（選用）

OpenClaw 是開源的個人 AI 助手，可透過 Telegram/Discord 即時查詢研究進度、文獻、週報。

```bash
# 一鍵安裝（需要 Node.js >= 22）
cd /Users/cengzhenhong/烏龜世界
bash openclaw/setup.sh
```

設定時選擇：
- AI 模型 → Ollama（本地免費，自動偵測已安裝的模型）
- 通訊頻道 → Telegram 或 Discord（依個人偏好）

安裝自訂 skills（已包含在 `setup.sh`）：
```bash
# 若只想先安裝，不跑互動設定：
OPENCLAW_SKIP_ONBOARD=1 bash openclaw/setup.sh

# 若要同時從 ClawHub 安裝公開 skills（多個用逗號分隔）：
OPENCLAW_CLAWHUB_SKILLS=sonoscli,calendar-helper bash openclaw/setup.sh
```

ClawHub（OpenClaw skills registry）：https://clawhub.ai/

OpenClaw Dashboard：http://127.0.0.1:18789/

Discord 指令表：`openclaw/discord-openclaw-command-table.md`

**自訂 skills**（`openclaw/skills/` 資料夾）：
| Skill | 功能 |
|---|---|
| turtle-literature | 查詢文獻資料庫 |
| turtle-progress | 查詢任務進度和完成率 |
| turtle-weekly | 查詢研究週報 |
| turtle-whitepaper | 討論白皮書和研究計畫 |
| study-auto-builder | 自動生成 108 課綱分層題庫（學科×階段×考試類型×難易度）+ 落點分析 |

---

## 📋 Notion 資料庫 ID 對照表

| 資料庫 | Notion URL | 用途 |
|---|---|---|
| 📚 文獻資料庫 | `34dac46d-492b-412f-a5b5-3ff571c124e4` | Workflow 1/6 寫入、Workflow 3/4 讀取 |
| 📝 研究週報 | `1b31e595-1e2f-4ade-b2eb-1bbe6f24eab8` | Workflow 4 寫入 |
| ✅ 任務資料庫 | `31c0634d-8ff4-80c0-94b2-f4dae132857b` | Workflow 3/4 讀取 |
| 🐢 烏龜計畫主頁 | `1d20634d-8ff4-820d-8c58-81cbb6417628` | Workflow 3 更新進度快照 |
| 📄 全戰略整合白皮書 | `31b0634d-8ff4-81ee-aff6-c0769489f090` | Workflow 2/5 追加 AI 建議 |

---

## 🔧 常用指令

```bash
# 啟動 Ollama（本地 AI）
ollama serve

# 啟動 n8n
docker-compose up -d

# 停止
docker-compose down

# 查看 log
docker-compose logs -f n8n

# 重新啟動（更新設定後）
docker-compose down && docker-compose up -d

# OpenClaw 相關
openclaw doctor      # 檢查狀態
openclaw update      # 更新版本
```

---

## 📅 排程總覽

| Workflow | 排程 | Cron 表達式 |
|---|---|---|
| 文獻自動搜尋 | 每週一 08:00 | `0 8 * * 1` |
| AI 企劃書完善 | 每週日 22:00 | `0 22 * * 0` |
| 進度追蹤更新 | 每天 21:00 | `0 21 * * *` |
| 週報自動生成 | 每週日 23:00 | `0 23 * * 0` |
| 多 AI 研究審查 | 每週三 20:00 | `0 20 * * 3` |
| Gemini 文獻探索 | 每週四 08:00 | `0 8 * * 4` |

所有時間皆為台灣時區（Asia/Taipei）。

---

## 💰 費用

**完全免費！** 所有 AI 服務都不需要信用卡：
- Ollama（本地）：免費，在你的 Mac 上執行
- Gemini API：免費方案，每分鐘 15 次請求
- OpenClaw：免費開源（MIT），使用 Ollama 本地模型
- Notion API：免費
- PubMed API：免費

---

*烏龜計畫 · 彰化縣立和美高級中學 · 曾圳鴻 · 2025-2026*
