# ESP32 AI Speaker (LLM + TTS + Skills)

This project is based on the nn-speaker repository and extended with LLM, TTS, and skill-based control.

## 📌 Branch
This assignment is implemented in:

👉 `llm_tool`

---

## 🔧 Features

- WiFi provisioning via UART
- OpenAI Chat (LLM)
  - LLM1: single-turn
  - LLM2: multi-turn (history)
  - LLM3: tool-call + skill system
- OpenAI TTS (text-to-speech)
- LED control (UART + skill system)
- Skill system (LittleFS + dynamic loading)
- Audio input/output via I2S
- Memory monitoring (ESP32 heap / PSRAM)

---

## 📁 Project Structure

Main firmware code is located in:
