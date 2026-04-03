---
name: led_control
version: "1.0.0"
description: "控制裝置上的 LED 指示燈（開啟、關閉、呼吸閃爍）"
author: nn-speaker-team
enabled: true

parameters:
  - name: action
    type: enum
    required: true
    description: "LED 動作：on（開啟）、off（關閉）、blink（呼吸閃爍）"
    enum_values:
      - "on"
      - "off"
      - "blink"

commands:
  - name: set_led
    handler: led_set
    format: "[SKILL:led_control:set_led:action={action}]"
    description: "設定 LED 狀態（on / off / blink）"
  - name: set_led_blink
    handler: led_blink
    format: "[SKILL:led_control:set_led_blink]"
    description: "讓 LED 開始呼吸閃爍（不需要參數）"
---

# LED 控制 Skill

控制 ESP32 開發板上的板載 LED 指示燈。

## 使用情境

此 skill 適用於以下場景：
- 使用者語音指令開關燈光
- 系統狀態指示（例如處理中閃爍）
- 除錯與測試硬體連線

## 命令詳細說明

### `set_led` — 基本開關

最簡單的用法，開啟、關閉或閃爍 LED。

**範例：**
- 開燈：`[SKILL:led_control:set_led:action=on]`
- 關燈：`[SKILL:led_control:set_led:action=off]`
- 閃爍：`[SKILL:led_control:set_led:action=blink]`

### `set_led_blink` — 閃爍快捷指令

直接讓 LED 進入呼吸閃爍模式，不需要參數。

**範例：**
- `[SKILL:led_control:set_led_blink]`
