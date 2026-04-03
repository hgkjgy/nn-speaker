# Skill 系統架構設計

> **版本**: 0.2.0（已實作漸進式載入）
> **日期**: 2026-04-03
> **範疇**: 架構設計與韌體端實作記錄（含漸進式載入）

---

## 1. 目錄結構設計

```text
firmware/                          # 專案根目錄
├── skills/                        # Skill 集合根目錄
│   ├── ARCHITECTURE.md            # 本文件
│   ├── loader/                    # Skill 載入器（Python 套件）
│   │   ├── __init__.py
│   │   ├── scanner.py             # 掃描 skills/ 子目錄
│   │   ├── parser.py              # 解析 SKILL.md frontmatter
│   │   ├── registry.py            # 全域 skill 註冊表
│   │   └── executor.py            # 調用 skill 並執行動作
│   ├── led_control/               # 範例 skill：LED 控制
│   │   └── SKILL.md
│   ├── <skill_name>/              # 其他 skill（未來擴充）
│   │   └── SKILL.md
│   └── ...
├── src/                           # ESP32 韌體原始碼
├── platformio.ini
└── ...
```

### 命名慣例

| 項目 | 規則 | 範例 |
|------|------|------|
| Skill 目錄名稱 | snake_case，全小寫 | `led_control`, `volume_adjust` |
| SKILL.md | 固定檔名，每個 skill 目錄恰好一份 | `skills/led_control/SKILL.md` |
| 補充資源 | 放在 skill 子目錄內，自由命名 | `skills/led_control/examples.json` |

---

## 2. SKILL.md Frontmatter Schema

每份 `SKILL.md` 以 YAML frontmatter 開頭（`---` 圍繞），其後為 Markdown 正文。

### 2.1 Schema 定義

```yaml
---
# ===== 必填欄位 =====
name: string            # skill 唯一識別名稱（與目錄名稱一致）
version: string         # 語意版本號 (semver)，例如 "1.0.0"
description: string     # 一句話摘要（< 120 字元），用於摘要層提示

# ===== 選填欄位 =====
author: string          # 作者或維護者
tags: [string]          # 標籤，用於分類與搜尋
enabled: boolean        # 是否啟用（預設 true）

# ===== 參數定義 =====
parameters:             # 此 skill 接受的參數清單
  - name: string        # 參數名稱
    type: string        # 資料型別：string | int | float | boolean | enum
    required: boolean   # 是否為必填
    default: any        # 預設值（選填）
    description: string # 參數說明
    enum_values: [any]  # 當 type 為 enum 時，列出可選值

# ===== 通訊設定 =====
transport:
  type: string          # 傳輸方式：serial | http | mqtt（本專案目前只用 serial）
  serial:               # 當 type 為 serial 時的設定
    baud_rate: int      # 預設 115200
    timeout_ms: int     # 回應超時（毫秒），預設 2000
    line_ending: string # 行尾字元："\n" | "\r\n"

# ===== 命令定義 =====
commands:               # 此 skill 可發送的命令清單
  - name: string        # 命令名稱（對應 firmware 處理的指令）
    template: string    # 命令模板，使用 {param} 作為佔位符
    description: string # 命令說明
    response_pattern: string  # 預期回應的 regex 模式（選填）
---
```

### 2.2 型別細則

| 欄位 | 型別 | 必填 | 說明 |
|------|------|------|------|
| `name` | string | ✅ | 必須與目錄名稱一致，僅允許 `[a-z0-9_]` |
| `version` | string | ✅ | 遵循 semver，例如 `"1.0.0"` |
| `description` | string | ✅ | 用於摘要層，提供給 LLM 做 skill 選擇 |
| `parameters` | list | ❌ | 無參數的 skill 可省略 |
| `transport` | object | ❌ | 省略時預設為 serial / 115200 |
| `commands` | list | ❌ | 純資訊型 skill 可無命令 |

---

## 3. Skill 載入器模組設計

載入器採用 **兩層式載入**（Lazy Loading）策略，**已在韌體端（C++）實作完成**：

- **摘要層（Summary Layer）** ✅ 已實作：啟動時掃描所有 skill，僅解析 frontmatter（name / description），透過 `SkillRegistry::generateSummaryPrompt()` 產生摘要 system prompt
- **詳細層（Detail Layer）** ✅ 已實作：當 LLM 透過 `[SKILL_REQUEST:xxx]` 請求某 skill 的詳細資訊時，才透過 `SkillRegistry::generateDetailForSkill()` 按需載入完整 SKILL.md 內容

### 3.1 模組職責

```text
┌──────────────────────────────────────────────────────┐
│                    SkillRegistry                     │
│  （單例，持有所有已註冊 skill 的摘要與狀態）            │
├──────────────────────────────────────────────────────┤
│  + scan_all() → list[SkillSummary]                   │
│  + get_summary(name) → SkillSummary                  │
│  + get_detail(name) → SkillDetail                    │
│  + list_enabled() → list[SkillSummary]               │
│  + generate_system_prompt() → str                    │
└───────────┬──────────────────────────┬───────────────┘
            │                          │
   ┌────────▼────────┐       ┌────────▼────────┐
   │  SkillScanner   │       │  SkillParser    │
   │                 │       │                 │
   │ + find_skills() │       │ + parse_front() │
   │   → list[Path]  │       │ + parse_full()  │
   └─────────────────┘       └─────────────────┘

   ┌─────────────────┐
   │ SkillExecutor   │
   │                 │
   │ + execute()     │
   │ + build_cmd()   │
   │ + send_serial() │
   └─────────────────┘
```

### 3.2 類別設計

#### `SkillSummary`（資料類別）

存放 frontmatter 解析後的摘要資訊。啟動時即載入，常駐記憶體。

```
欄位：
  name: str
  version: str
  description: str
  tags: list[str]
  enabled: bool
  parameters: list[ParameterDef]
  commands: list[CommandDef]
  transport: TransportConfig
  skill_dir: Path          # skill 目錄的絕對路徑
```

#### `SkillDetail`（資料類別）

包含 `SkillSummary` 的所有欄位，額外加上完整的 Markdown 正文。

```
欄位：
  summary: SkillSummary    # 嵌入摘要
  full_content: str        # 完整 SKILL.md 原始內容
  body_markdown: str       # 去除 frontmatter 後的 Markdown 正文
```

#### `SkillScanner`

```
方法：
  find_skills(base_dir: Path) → list[Path]
    - 遍歷 base_dir 下所有子目錄
    - 檢查每個子目錄是否包含 SKILL.md
    - 回傳符合條件的 SKILL.md 路徑清單
```

#### `SkillParser`

```
方法：
  parse_frontmatter(path: Path) → SkillSummary
    - 讀取 SKILL.md 前段 YAML frontmatter
    - 驗證必填欄位
    - 回傳 SkillSummary 物件

  parse_full(path: Path) → SkillDetail
    - 讀取完整 SKILL.md
    - 解析 frontmatter + body
    - 回傳 SkillDetail 物件
```

#### `SkillRegistry`（單例）

```
方法：
  scan_all() → list[SkillSummary]
    - 呼叫 SkillScanner.find_skills()
    - 對每個結果呼叫 SkillParser.parse_frontmatter()
    - 將摘要存入內部字典
    - 回傳所有摘要清單

  get_summary(name: str) → SkillSummary
    - 從內部字典取得指定 skill 的摘要

  get_detail(name: str) → SkillDetail
    - 按需呼叫 SkillParser.parse_full()
    - 快取結果以避免重複解析

  list_enabled() → list[SkillSummary]
    - 回傳 enabled=True 的所有 skill 摘要

  generate_system_prompt() → str
    - 將所有啟用 skill 的摘要彙整為系統提示詞片段
    - 格式：每個 skill 一段，包含名稱、描述、可用命令
    - ⚠️ 目前已委派給 generateSummaryPrompt()
```

#### 韌體端新增方法（✅ 已實作）

以下方法已在 C++ 韌體端實作（見 [`SkillRegistry.h`](../lib/skill_system/SkillRegistry.h) / [`SkillRegistry.cpp`](../lib/skill_system/SkillRegistry.cpp)）：

```
SkillRegistry::generateSummaryPrompt() → String
  - 產生只含 name/description 的摘要 prompt
  - 用於第一階段 LLM 呼叫的 system prompt
  - generateSystemPrompt() 已改為委派此方法

SkillRegistry::generateDetailForSkill(const String &skillName) → String
  - 按名稱取得單一 skill 的完整 detail（含 parameters、commands、body）
  - 用於第二階段 LLM 呼叫時注入對話歷史

SkillRegistry::findSkillByName(const String &skillName) → Skill*
  - 內部輔助方法，在已註冊 skill 清單中搜尋指定名稱的 skill
  - 找不到時回傳 nullptr
```

以下方法已在 [`OpenAILLM.h`](../lib/openai_llm/OpenAILLM.h) / [`OpenAILLM.cpp`](../lib/openai_llm/OpenAILLM.cpp) 實作：

```
OpenAILLM::parseSkillRequest(const String &reply) → String
  - 解析 LLM 回覆中的 [SKILL_REQUEST:xxx] 標籤
  - 回傳 skill 名稱（例如 "led_control"），若無匹配則回傳空字串
```

#### `SkillExecutor`

```
方法：
  execute(skill_name: str, command_name: str, params: dict) → str
    - 從 registry 取得 skill 摘要
    - 找到對應的 command 定義
    - 呼叫 build_cmd() 組裝命令字串
    - 呼叫 send_serial() 發送並接收回應

  build_cmd(template: str, params: dict) → str
    - 將命令模板中的 {param} 替換為實際參數值
    - 驗證所有 required 參數已提供

  send_serial(cmd: str, config: TransportConfig) → str
    - 透過 pyserial 開啟 serial port
    - 發送命令字串
    - 等待回應（依 timeout_ms 設定）
    - 回傳回應字串
```

### 3.3 啟動流程

```
1. 應用程式啟動
2. 建立 SkillRegistry 實例
3. 呼叫 registry.scan_all()
   ├── SkillScanner 掃描 skills/ 目錄
   ├── 對每個 SKILL.md 呼叫 SkillParser.parse_frontmatter()
   └── 將 SkillSummary 存入 registry
4. 呼叫 registry.generate_system_prompt()
   └── 產生包含所有啟用 skill 摘要的系統提示詞
5. 將系統提示詞注入 LLM 對話情境
```

### 3.4 調用流程

```
1. 使用者發出自然語言指令（例如「把 LED 打開」）
2. LLM 根據系統提示詞選擇合適的 skill 與命令
3. LLM 輸出結構化呼叫：
   {
     "skill": "led_control",
     "command": "set_led",
     "params": { "action": "on" }
   }
4. 應用程式呼叫 registry.get_detail("led_control")
   └── 按需載入完整 SKILL.md（詳細層）
5. 應用程式呼叫 executor.execute("led_control", "set_led", {"action": "on"})
   ├── build_cmd("LED {action}", {"action": "on"}) → "LED on"
   ├── send_serial("LED on\n", serial_config)
   └── 接收 ESP32 回應 "OK LED on"
6. 將結果回傳給 LLM 或直接呈現給使用者
```

### 3.5 漸進式載入流程（✅ 已實作）

韌體端的 `OpenAILLM::chatV3()` 已改為兩階段 LLM 呼叫流程：

#### 階段一：摘要提示 → 決策

1. 呼叫 `SkillRegistry::generateSummaryPrompt()` 取得摘要 system prompt（僅含 name / description）
2. 將摘要 prompt 與使用者訊息一同送出第一次 API 呼叫
3. LLM 根據摘要判斷：
   - 若已能直接選擇 skill 並補齊參數 → 直接輸出 `[SKILL:handler param=val]` → 流程結束
   - 若需要更多細節 → 輸出 `[SKILL_REQUEST:skill_name]`

#### 階段二（條件式）：Detail 注入 → 命令生成

1. `OpenAILLM::parseSkillRequest()` 偵測 `[SKILL_REQUEST:xxx]` 標籤
2. 呼叫 `SkillRegistry::generateDetailForSkill(skillName)` 載入完整 skill detail
3. 將 detail 以 user message 形式注入對話歷史
4. 發出第二次 API 呼叫
5. LLM 根據 detail 產生 `[SKILL:handler param=val]` 命令
6. 回到正常的 `parseAndExecute()` 流程

#### `[SKILL_REQUEST:xxx]` 協議格式

```
[SKILL_REQUEST:skill_name]
```

| 項目 | 說明 |
|------|------|
| `skill_name` | 必須是摘要清單中已列出的 skill 名稱 |
| 用途 | LLM 向韌體請求某 skill 的完整 detail |
| 觸發時機 | 僅在摘要資訊不足以產生具體命令時使用 |
| 回應方式 | 韌體以 user message 形式回饋 detail，再進行第二輪 LLM 呼叫 |

#### 流程時序圖

```
  User          Application        SkillRegistry       OpenAI LLM
   │                │                    │                  │
   │  語音指令      │                    │                  │
   │───────────────▶│                    │                  │
   │                │  generateSummaryPrompt()              │
   │                │───────────────────▶│                  │
   │                │  摘要 prompt       │                  │
   │                │◀───────────────────│                  │
   │                │                    │                  │
   │                │  Phase 1: 摘要 + 使用者訊息            │
   │                │─────────────────────────────────────▶│
   │                │                    │  [SKILL_REQUEST:led] │
   │                │◀─────────────────────────────────────│
   │                │                    │                  │
   │                │  generateDetailForSkill("led")        │
   │                │───────────────────▶│                  │
   │                │  detail 字串       │                  │
   │                │◀───────────────────│                  │
   │                │                    │                  │
   │                │  Phase 2: detail 注入 + 第二次呼叫     │
   │                │─────────────────────────────────────▶│
   │                │                    │  [SKILL:led action=on] │
   │                │◀─────────────────────────────────────│
   │                │                    │                  │
   │                │  parseAndExecute()                    │
   │  結果回覆      │                    │                  │
   │◀───────────────│                    │                  │
```

---

## 4. LED 控制 Skill 範例

以下是完整的 `skills/led_control/SKILL.md` 範例內容：

````markdown
---
name: led_control
version: "1.0.0"
description: "控制 ESP32 開發板上的 LED 燈，支援開啟、關閉、閃爍等操作"
author: nn-speaker-team
tags:
  - hardware
  - led
  - indicator
enabled: true

parameters:
  - name: action
    type: enum
    required: true
    description: "LED 操作類型"
    enum_values: ["on", "off", "blink"]
  - name: blink_interval_ms
    type: int
    required: false
    default: 500
    description: "閃爍間隔（毫秒），僅在 action 為 blink 時有效"
  - name: duration_ms
    type: int
    required: false
    default: 0
    description: "持續時間（毫秒），0 表示無限持續直到下一個命令"

transport:
  type: serial
  serial:
    baud_rate: 115200
    timeout_ms: 2000
    line_ending: "\n"

commands:
  - name: set_led
    template: "LED {action}"
    description: "設定 LED 狀態為 on、off 或 blink"
    response_pattern: "^OK LED (on|off|blink)$"
  - name: set_led_blink
    template: "LED blink {blink_interval_ms}"
    description: "設定 LED 閃爍，並指定間隔時間"
    response_pattern: "^OK LED blink \\d+$"
  - name: set_led_timed
    template: "LED {action} {duration_ms}"
    description: "設定 LED 狀態並指定持續時間"
    response_pattern: "^OK LED (on|off|blink) \\d+$"
---

# LED 控制 Skill

控制 ESP32 開發板上的板載 LED 燈。

## 使用情境

此 skill 適用於以下場景：
- 使用者語音指令開關燈光
- 系統狀態指示（例如處理中閃爍）
- 除錯與測試硬體連線

## 命令詳細說明

### `set_led` — 基本開關

最簡單的用法，開啟或關閉 LED。

**範例：**
- 開燈：發送 `LED on`，預期回應 `OK LED on`
- 關燈：發送 `LED off`，預期回應 `OK LED off`

### `set_led_blink` — 閃爍模式

讓 LED 以指定間隔持續閃爍。

**範例：**
- 每 500ms 閃爍：發送 `LED blink 500`，預期回應 `OK LED blink 500`
- 快速閃爍：發送 `LED blink 100`，預期回應 `OK LED blink 100`

### `set_led_timed` — 限時操作

設定 LED 狀態並在指定時間後自動恢復。

**範例：**
- 亮 3 秒後自動熄滅：發送 `LED on 3000`，預期回應 `OK LED on 3000`

## 錯誤處理

| 回應 | 說明 |
|------|------|
| `OK LED <state>` | 命令執行成功 |
| `ERR UNKNOWN_CMD` | 無法識別的命令格式 |
| `ERR INVALID_PARAM` | 參數值不合法（例如負數間隔） |
| `ERR TIMEOUT` | 命令執行超時 |

## 備註

- ESP32 韌體需要實作對應的 serial 命令解析器
- LED 預設使用板載 LED（通常為 GPIO 2）
- 閃爍模式下，再次發送任何 LED 命令會取消當前閃爍
````

---

## 5. 與 ESP32 韌體的互動方式

### 5.1 Serial 通訊協議

本系統採用 **文字行命令協議（Text Line Protocol）**，透過 USB Serial（UART）與 ESP32 通訊。

#### 通訊參數

| 參數 | 值 |
|------|------|
| 鮑率 (Baud Rate) | 115200 |
| 資料位元 | 8 |
| 停止位元 | 1 |
| 校驗 | None (8N1) |
| 行尾字元 | `\n` (LF) |
| 編碼 | ASCII |

#### 命令格式

```
<CATEGORY> <ACTION> [PARAM1] [PARAM2] ...\n
```

- `CATEGORY`：命令類別（對應 skill 領域），如 `LED`、`VOLUME`
- `ACTION`：動作名稱，如 `on`、`off`、`blink`
- `PARAM*`：選填參數，以空格分隔
- 以 `\n` 結束

#### 回應格式

```
OK <CATEGORY> <ACTION> [DETAIL]\n
```

或錯誤回應：

```
ERR <ERROR_CODE> [MESSAGE]\n
```

#### 錯誤碼定義

| 錯誤碼 | 說明 |
|--------|------|
| `UNKNOWN_CMD` | 無法識別的命令類別或動作 |
| `INVALID_PARAM` | 參數格式或值不合法 |
| `TIMEOUT` | 硬體操作超時 |
| `BUSY` | 裝置忙碌中，無法執行 |
| `HARDWARE_ERR` | 硬體層級錯誤 |

### 5.2 ESP32 韌體端設計建議

韌體需要實作一個 **Serial 命令分發器（Command Dispatcher）**：

```
SerialCommandDispatcher
├── 從 Serial 讀取一行文字
├── 解析 CATEGORY 與 ACTION
├── 查找已註冊的命令處理器
├── 呼叫對應處理器並傳入參數
└── 將處理結果格式化為回應並寫回 Serial
```

#### 命令處理器介面（概念）

```
class CommandHandler:
    category: str                              # 例如 "LED"
    handle(action: str, params: list[str])     # 處理命令
        → (success: bool, message: str)        # 回傳結果
```

#### LED 命令處理器行為

| 收到命令 | 韌體行為 | 回應 |
|----------|----------|------|
| `LED on\n` | 將 LED GPIO 設為 HIGH | `OK LED on\n` |
| `LED off\n` | 將 LED GPIO 設為 LOW | `OK LED off\n` |
| `LED blink 500\n` | 啟動 500ms 間隔閃爍 timer | `OK LED blink 500\n` |
| `LED on 3000\n` | LED 亮起，3 秒後自動熄滅 | `OK LED on 3000\n` |
| `LED foo\n` | 無法識別的動作 | `ERR UNKNOWN_CMD\n` |
| `LED blink -1\n` | 不合法的參數 | `ERR INVALID_PARAM\n` |

### 5.3 通訊時序圖

```
  Python (Host)                     ESP32 (Firmware)
       │                                  │
       │  ──── "LED on\n" ──────────────▶ │
       │                                  │ (設定 GPIO HIGH)
       │  ◀─── "OK LED on\n" ─────────── │
       │                                  │
       │  ──── "LED blink 500\n" ───────▶ │
       │                                  │ (啟動 blink timer)
       │  ◀─── "OK LED blink 500\n" ──── │
       │                                  │
       │  ──── "LED off\n" ─────────────▶ │
       │                                  │ (停止 blink, GPIO LOW)
       │  ◀─── "OK LED off\n" ────────── │
       │                                  │
```

### 5.4 Serial Port 自動偵測

Skill 載入器啟動時，按以下優先順序決定 serial port：

1. 環境變數 `SKILL_SERIAL_PORT`（如已設定）
2. PlatformIO 專案設定中的 `monitor_port`
3. 自動掃描系統 serial port，尋找 ESP32 裝置的 USB VID/PID

---

## 6. 未來擴充方向

以下為目前不實作但保留擴充空間的項目：

- **Skill 依賴關係**：在 frontmatter 中加入 `depends_on` 欄位
- **Skill 組合（Composite Skill）**：一個 skill 可串接多個子 skill
- **雙向事件推送**：ESP32 主動推送事件給 Host（如按鈕按下）
- **Transport 擴充**：支援 WiFi HTTP / MQTT 做為替代傳輸層
- **Skill 版本相容性**：載入器檢查 skill 版本與韌體版本的相容矩陣
