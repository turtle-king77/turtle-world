---
name: model-router
description: 智慧模型路由器 — 根據問題類型自動選擇最佳 AI 模型，平衡回應品質與成本
trigger: 每則訊息自動啟用；在回應前先判斷最佳模型
autoActivate: true
tools: []
---

# 🧭 智慧模型路由器（Model Router）

你在回應每則訊息前，必須先進行模型路由判斷。目標：用最適合的模型回答每個問題。

## 核心原則

**簡單問題不浪費好模型，複雜問題不委屈用弱模型。**

## 可用模型清單

| 別名 | 模型 ID | 特色 | 成本 |
|------|---------|------|------|
| `fast` | `ollama/qwen2.5:7b` | 極速、免費、本地 | 免費 |
| `local` | `ollama/qwen3:14b` | 推理強、免費、本地 | 免費 |
| `deep` | `deepseek/deepseek-chat` | 中文強、便宜 | 極低 |
| `r1` | `openrouter/deepseek/deepseek-r1` | DeepSeek 推理鏈模型 | 低 |
| `flash` | `google/gemini-2.5-flash` | 超長上下文 1M | 低 |
| `gemini-pro` | `google/gemini-2.5-pro` | Google 最強穩定版 | 中 |
| `gemini-next` | `google/gemini-3.1-pro-preview` | Google 最新預覽版 | 中 |
| `reason` | `openai/o4-mini` | OpenAI 推理模型 | 中 |
| `reason-pro` | `openai/o3-pro` | OpenAI 深度推理 | 高 |
| `gpt` | `openai/gpt-5.4` | GPT 最新、多模態 | 中 |
| `smart` | `anthropic/claude-sonnet-4-6` | Claude 最強均衡 | 中 |
| `opus` | `anthropic/claude-opus-4-6` | Claude 最頂級 | 高 |
| `haiku` | `anthropic/claude-haiku-4-5` | Claude 快速便宜版 | 低 |

## 路由決策表

收到訊息後，依照以下分類選擇模型：

### 🟢 Tier 1：`fast`（ollama/qwen2.5:7b）
- 閒聊、打招呼、日常對話
- 簡單事實查詢（「台灣首都是？」）
- 確認/是否類問題
- emoji 反應、短回覆

### 🔵 Tier 2：`local`（ollama/qwen3:14b）
- 數學計算、邏輯推理
- 出題（study-auto-builder）
- 程式碼片段、debug
- 段考/學測等級的課業問答
- 一般科學概念解釋

### 🟡 Tier 3：`deep`（deepseek/deepseek-chat）
- 中文長文寫作、作文批改
- 翻譯（中英互譯）
- 國文、歷史、社會科目
- 一般課業輔導（非高難度）
- 報告撰寫

### 🟠 Tier 4：`flash`（google/gemini-2.5-flash）
- 長文獻閱讀與摘要（> 2000 字輸入）
- 多篇論文交叉比對
- 整本書籍/章節分析
- 大量資料整理
- 需要超長上下文的任務

### 🔴 Tier 5：`smart`（anthropic/claude-sonnet-4-6）
- 奧賽等級題目（奧銅以上）
- 旺宏科學獎專題研究
- 深度科學問題（跨領域整合）
- 實驗設計與方法論
- 複雜推理鏈（> 3 步驟）
- 論文審查與批判性分析

### ⚡ 特殊路由：推理模型
當問題需要**逐步推理**（數學證明、邏輯鏈、演算法設計）時，優先使用推理模型：
- `reason`（openai/o4-mini）— 數學推理、程式邏輯、快速推理
- `r1`（deepseek-r1）— 數學證明、科學推理、便宜的推理替代方案
- `reason-pro`（openai/o3-pro）— 最頂級推理，僅用於極難問題（IMO 等級）

### 🖼️ 特殊路由：圖片分析
當訊息附帶圖片時，必須選用支援 `text+image` 的模型：
- 簡單圖片描述 → `haiku`
- 圖表/資料圖分析 → `gpt` 或 `smart`
- 科學實驗照片分析 → `smart` 或 `opus`

### 👑 特殊路由：最頂級任務
以下情境使用 `opus`（anthropic/claude-opus-4-6）：
- IMO / IPhO / IBO 金牌等級題目
- 完整研究計畫撰寫
- 跨 3+ 領域的綜合分析

## 路由流程

```
收到訊息
  → 掃描關鍵詞與意圖
  → 評估複雜度（1-5）
  → 選擇對應 Tier
  → 若當前模型 ≠ 建議模型 → 執行 /model <模型ID>
  → 回應訊息
```

## 自動切換機制

1. **判斷完成後**，若目前 session 模型不是最佳選擇，先執行 `/model <模型ID>` 切換。
2. **切換後**才開始生成回應。
3. **不要告訴使用者**你正在切換模型（除非使用者主動問）。
4. **連續同類問題**時，不需要重複切換（session 會記住）。

## 升級觸發詞

以下關鍵詞/情境自動升級到更高 Tier：

| 觸發 | 升級至 |
|------|--------|
| 「奧賽」「奧林匹亞」「IMO」「IPhO」「IChO」「IBO」 | `smart` |
| 「旺宏」「科學獎」「專題研究」「實驗設計」 | `smart` |
| 「論文」「paper」「文獻回顧」「review」 | `flash` |
| 「翻譯」「英翻中」「中翻英」 | `deep` |
| 「出題」「自動製作」「題庫」 | `local` |
| 「你好」「謝謝」「哈哈」「嗯」 | `fast` |
| 附帶長文本（> 2000 字）或多個 URL | `flash` |

## 降級規則（省成本）

- 若 `smart` 模型連續使用 > 5 輪且問題複雜度已降低 → 自動降回 `local` 或 `deep`
- 深夜時段（23:00-07:00）非緊急問題 → 優先用 `local` 或 `fast`

## 不路由的情況

- 使用者用 `/model` 手動指定了模型 → 遵從使用者選擇，不覆蓋
- 使用者說「用本地模型」「用免費的」→ 限制在 `fast` 或 `local`
- 使用者說「用最好的」「用 Claude」→ 直接用 `smart`

## 模型切換紀錄

每次切換時，在內部記錄（不對使用者顯示）：
```
[model-router] 問題類型=奧賽數學, 複雜度=5, 切換: local → smart
```

這有助於之後分析模型使用模式。
