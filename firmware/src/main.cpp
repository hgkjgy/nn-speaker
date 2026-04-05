#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <WiFi.h>
#include <Preferences.h>
#include <driver/i2s.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>
#include "I2SMicSampler.h"
#include "ADCSampler.h"
#include "I2SOutput.h"
#include "config.h"
#include "Application.h"
#include "LittleFS.h"
#include "IntentProcessor.h"
#include "Speaker.h"
#include "IndicatorLight.h"
#include "AudioKitHAL.h"
#include "OpenAILLM.h"
#include "OpenAITTS.h"
#include "SkillRegistry.h"
#include "LedSkillHandler.h"

// i2s config for using the internal ADC
i2s_config_t adcI2SConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0};

// i2s config for reading from both channels of I2S
i2s_config_t i2sMemsConfigBothChannels = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_MIC_CHANNEL,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0};

// i2s config for codec
i2s_config_t i2sCodecConfig = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 64,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0};

// i2s codec pins
i2s_pin_config_t i2s_codec_pins = {
    .bck_io_num = 5,
    .ws_io_num = 25,
    .data_out_num = 26,
    .data_in_num = 35};

// i2s microphone pins
i2s_pin_config_t i2s_mic_pins = {
    .bck_io_num = I2S_MIC_SERIAL_CLOCK,
    .ws_io_num = I2S_MIC_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SERIAL_DATA};

// i2s speaker pins
i2s_pin_config_t i2s_speaker_pins = {
    .bck_io_num = I2S_SPEAKER_SERIAL_CLOCK,
    .ws_io_num = I2S_SPEAKER_LEFT_RIGHT_CLOCK,
    .data_out_num = I2S_SPEAKER_SERIAL_DATA,
    .data_in_num = I2S_PIN_NO_CHANGE};

static Preferences g_wifiPrefs;
static Preferences g_openaiPrefs;
static String g_uartInputLine;
static String g_openaiApiKey;
static IndicatorLight *g_indicatorLight = nullptr;
static Speaker *g_speaker = nullptr;
static OpenAILLM *g_llm = nullptr;

static String trimCopy(const String &in)
{
  String out = in;
  out.trim();
  return out;
}

static void loadWifiCredentials(String &ssid, String &password)
{
  g_wifiPrefs.begin("wifi", true);
  ssid = g_wifiPrefs.getString("ssid", WIFI_SSID);
  password = g_wifiPrefs.getString("pass", WIFI_PSWD);
  g_wifiPrefs.end();
}

static bool saveWifiCredentials(const String &ssid, const String &password)
{
  if (ssid.length() == 0)
  {
    return false;
  }

  g_wifiPrefs.begin("wifi", false);
  bool okSsid = g_wifiPrefs.putString("ssid", ssid) > 0;
  bool okPass = g_wifiPrefs.putString("pass", password) >= 0;
  g_wifiPrefs.end();
  return okSsid && okPass;
}

static void clearWifiCredentials()
{
  g_wifiPrefs.begin("wifi", false);
  g_wifiPrefs.clear();
  g_wifiPrefs.end();
}

// --- OpenAI API key NVS helpers ---

static void loadOpenAIKey()
{
  g_openaiPrefs.begin("openai", true);
  g_openaiApiKey = g_openaiPrefs.getString("api_key", OPENAI_API_KEY);
  g_openaiPrefs.end();
}

static bool saveOpenAIKey(const String &key)
{
  if (key.length() == 0)
  {
    return false;
  }
  g_openaiPrefs.begin("openai", false);
  bool ok = g_openaiPrefs.putString("api_key", key) > 0;
  g_openaiPrefs.end();
  return ok;
}

static void clearOpenAIKey()
{
  g_openaiPrefs.begin("openai", false);
  g_openaiPrefs.clear();
  g_openaiPrefs.end();
}

static void processWifiUartCommand(const String &line)
{
  String command = trimCopy(line);
  if (command.length() == 0)
  {
    return;
  }

  if (command.equalsIgnoreCase("WIFI HELP"))
  {
    Serial.println("UART WiFi commands:");
    Serial.println("  WIFI SET <ssid>|<password>");
    Serial.println("  WIFI SHOW");
    Serial.println("  WIFI CLEAR");
    return;
  }

  if (command.equalsIgnoreCase("WIFI SHOW"))
  {
    String ssid;
    String password;
    loadWifiCredentials(ssid, password);
    Serial.printf("Stored SSID: %s\n", ssid.c_str());
    Serial.printf("Stored password length: %d\n", password.length());
    return;
  }

  if (command.equalsIgnoreCase("WIFI CLEAR"))
  {
    clearWifiCredentials();
    Serial.println("Stored WiFi credentials cleared.");
    return;
  }

  if (command.startsWith("WIFI SET "))
  {
    String payload = command.substring(strlen("WIFI SET "));
    int sep = payload.indexOf('|');
    if (sep <= 0)
    {
      Serial.println("Invalid format. Use: WIFI SET <ssid>|<password>");
      return;
    }

    String ssid = trimCopy(payload.substring(0, sep));
    String password = trimCopy(payload.substring(sep + 1));
    if (ssid.length() == 0)
    {
      Serial.println("SSID cannot be empty.");
      return;
    }

    if (saveWifiCredentials(ssid, password))
    {
      Serial.println("WiFi credentials saved.");
      Serial.println("Reboot to apply.");
    }
    else
    {
      Serial.println("Failed to save WiFi credentials.");
    }
    return;
  }

  Serial.println("Unknown command. Type WIFI HELP");
}

static void processLedUartCommand(const String &line)
{
  String command = trimCopy(line);
  if (command.length() == 0)
  {
    return;
  }

  if (g_indicatorLight == nullptr)
  {
    Serial.println("LED controller is not ready.");
    return;
  }

  if (command.equalsIgnoreCase("LED HELP"))
  {
    Serial.println("UART LED commands:");
    Serial.println("  LED ON");
    Serial.println("  LED OFF");
    Serial.println("  LED PULSE");
    return;
  }

  if (command.equalsIgnoreCase("LED ON"))
  {
    g_indicatorLight->setState(ON);
    Serial.println("LED set to ON.");
    return;
  }

  if (command.equalsIgnoreCase("LED OFF"))
  {
    g_indicatorLight->setState(OFF);
    Serial.println("LED set to OFF.");
    return;
  }

  if (command.equalsIgnoreCase("LED PULSE"))
  {
    g_indicatorLight->setState(PULSING);
    Serial.println("LED set to PULSING.");
    return;
  }
}

static void processTtsUartCommand(const String &line)
{
  String command = trimCopy(line);
  if (command.length() == 0)
  {
    return;
  }

  if (g_speaker == nullptr)
  {
    Serial.println("Speaker is not ready.");
    return;
  }

  if (command.equalsIgnoreCase("TTS HELP"))
  {
    Serial.println("UART TTS commands:");
    Serial.println("  TTS <text>   – synthesize and play the given text via OpenAI TTS");
    Serial.println("  TTS HELP     – show this help");
    return;
  }

  // Everything after "TTS " is the text to speak
  if (command.startsWith("TTS "))
  {
    String text = command.substring(strlen("TTS "));
    text.trim();
    if (text.length() == 0)
    {
      Serial.println("Usage: TTS <text>");
      return;
    }
    Serial.printf("TTS: synthesizing \"%s\" ...\n", text.c_str());
    bool ok = g_speaker->playTTS(text.c_str());
    if (ok)
    {
      Serial.println("TTS: playback started.");
    }
    else
    {
      Serial.println("TTS: synthesis failed.");
    }
    return;
  }
}

/**
 * Tool handler callback for chatV3.
 * Parses "<tool> filename content" from the LLM reply.
 * Does NOT actually write to LittleFS – just logs and returns success feedback.
 * Returns empty String if no tool call is detected.
 */
static String llmToolHandler(const String &reply)
{
  String trimmed = reply;
  trimmed.trim();
  if (!trimmed.startsWith("<tool>"))
  {
    return String(); // not a tool call
  }

  String rest = trimmed.substring(strlen("<tool>"));
  rest.trim();

  int spaceIdx = rest.indexOf(' ');
  if (spaceIdx <= 0)
  {
    Serial.println("Tool: invalid format, expected <tool> filename content");
    return String(); // malformed – treat as final answer
  }

  String filename = rest.substring(0, spaceIdx);
  String content = rest.substring(spaceIdx + 1);

  Serial.printf("Tool: (simulated) wrote %d bytes to %s\n", content.length(), filename.c_str());
  return "檔案 " + filename + " 已寫入";
}

static void processLlmUartCommand(const String &line)
{
  String command = trimCopy(line);
  if (command.length() == 0)
  {
    return;
  }

  if (g_llm == nullptr)
  {
    Serial.println("LLM is not ready.");
    return;
  }

  if (command.equalsIgnoreCase("LLM HELP"))
  {
    Serial.println("UART LLM commands:");
    Serial.println("  LLM1 <text>           – single-turn chat (no history)");
    Serial.println("  LLM2 <text>           – multi-turn chat (with history)");
    Serial.println("  LLM3 <text>           – multi-turn chat with tool use (write files)");
    Serial.println("  LLM SYSTEM <prompt>   – set the system prompt");
    Serial.println("  LLM MODEL <name>      – change the model (e.g. gpt-4o)");
    Serial.println("  LLM RESET             – clear multi-turn conversation history");
    Serial.println("  LLM HELP              – show this help");
    return;
  }

  if (command.equalsIgnoreCase("LLM RESET") || command.equalsIgnoreCase("LLM CLEAR"))
  {
    g_llm->clearHistory();
    Serial.println("LLM history cleared.");
    return;
  }

  if (command.startsWith("LLM SYSTEM "))
  {
    String prompt = command.substring(strlen("LLM SYSTEM "));
    prompt.trim();
    static String systemPromptStorage;
    systemPromptStorage = prompt;
    g_llm->setSystemPrompt(systemPromptStorage.c_str());
    Serial.printf("LLM system prompt set to: %s\n", systemPromptStorage.c_str());
    return;
  }

  if (command.startsWith("LLM MODEL "))
  {
    String model = command.substring(strlen("LLM MODEL "));
    model.trim();
    static String modelStorage;
    modelStorage = model;
    g_llm->setModel(modelStorage.c_str());
    Serial.printf("LLM model changed to: %s\n", modelStorage.c_str());
    return;
  }

  // LLM3: multi-turn chat with tool-call loop
  if (command.startsWith("LLM3 "))
  {
    String text = command.substring(strlen("LLM3 "));
    text.trim();
    if (text.length() == 0)
    {
      Serial.println("Usage: LLM3 <text>");
      return;
    }
    Serial.printf("LLM3: sending \"%s\" ...\n", text.c_str());
    unsigned long startTime = millis();
    String reply = g_llm->chatV3(text.c_str(), llmToolHandler, 5);
    unsigned long elapsed = millis() - startTime;

    if (reply.length() > 0)
    {
      Serial.println();
      Serial.println("--- Assistant (final) ---");
      Serial.println(reply);
      Serial.println("-------------------------");
      Serial.printf("(total %lu ms)\n", elapsed);
    }
    else
    {
      Serial.println("LLM3: no reply or error occurred.");
    }
    return;
  }

  // LLM1 / LLM2: single-turn or multi-turn chat
  if (command.startsWith("LLM1 ")||command.startsWith("LLM2 "))
  {
    String text = command.substring(strlen("LLM"));
    text.trim();
    if (text.length() == 0)
    {
      Serial.println("Usage: LLM<ver> <text>");
      return;
    }
    Serial.printf("LLM: sending \"%s\" ...\n", text.c_str());
    unsigned long startTime = millis();
    String reply;
    if (text[0] == '1') {
      text = text.substring(1);
      reply = g_llm->chatV1(text.c_str()+1);
    } else if (text[0] == '2') {
      text = text.substring(1);
      reply = g_llm->chatV2(text.c_str()+1);
    } else {
      reply = g_llm->chatV2(text.c_str());
    } 
    

    unsigned long elapsed = millis() - startTime;
    if (reply.length() > 0)
    {
      Serial.println();
      Serial.println("--- Assistant ---");
      Serial.println(reply);
      Serial.println("-----------------");
      Serial.printf("(took %lu ms)\n", elapsed);
    }
    else
    {
      Serial.println("LLM: no reply or error occurred.");
    }
    return;
  }
}

static void processKeyUartCommand(const String &line)
{
  String command = trimCopy(line);
  if (command.length() == 0)
  {
    return;
  }

  if (command.equalsIgnoreCase("KEY HELP"))
  {
    Serial.println("UART KEY commands:");
    Serial.println("  KEY SET <api_key>  – save OpenAI API key to NVS");
    Serial.println("  KEY SHOW           – show first 8 chars of current key");
    Serial.println("  KEY CLEAR          – clear saved key (revert to default)");
    return;
  }

  if (command.equalsIgnoreCase("KEY SHOW"))
  {
    String display = g_openaiApiKey.substring(0, 8) + "...";
    Serial.printf("Current OpenAI API key: %s\n", display.c_str());
    Serial.printf("Key length: %d\n", g_openaiApiKey.length());
    return;
  }

  if (command.equalsIgnoreCase("KEY CLEAR"))
  {
    clearOpenAIKey();
    g_openaiApiKey = OPENAI_API_KEY;
    // Update live objects
    if (g_llm)
    {
      g_llm->setApiKey(g_openaiApiKey.c_str());
    }
    if (g_speaker && g_speaker->tts())
    {
      g_speaker->tts()->setApiKey(g_openaiApiKey.c_str());
    }
    Serial.println("OpenAI API key cleared. Reverted to default.");
    return;
  }

  if (command.startsWith("KEY SET "))
  {
    String key = trimCopy(command.substring(strlen("KEY SET ")));
    if (key.length() == 0)
    {
      Serial.println("API key cannot be empty.");
      return;
    }

    if (saveOpenAIKey(key))
    {
      g_openaiApiKey = key;
      // Update live objects
      if (g_llm)
      {
        g_llm->setApiKey(g_openaiApiKey.c_str());
      }
      if (g_speaker && g_speaker->tts())
      {
        g_speaker->tts()->setApiKey(g_openaiApiKey.c_str());
      }
      String display = g_openaiApiKey.substring(0, 8) + "...";
      Serial.printf("OpenAI API key saved: %s\n", display.c_str());
    }
    else
    {
      Serial.println("Failed to save OpenAI API key.");
    }
    return;
  }

  Serial.println("Unknown command. Type KEY HELP");
}

static void handleUartWifiProvisioning(const String &line)
{
  String command = trimCopy(line);
  if (command.startsWith("KEY ") || command.equalsIgnoreCase("KEY HELP"))
  {
    processKeyUartCommand(command);
    return;
  }
  if (command.startsWith("LLM ") || command.startsWith("LLM1 ") || command.startsWith("LLM2 ") || command.startsWith("LLM3 ") || command.equalsIgnoreCase("LLM HELP"))
  {
    processLlmUartCommand(command);
    return;
  }
  if (command.startsWith("TTS ") || command.equalsIgnoreCase("TTS HELP"))
  {
    processTtsUartCommand(command);
    return;
  }
  if (command.startsWith("LED ") || command.equalsIgnoreCase("LED HELP"))
  {
    processLedUartCommand(command);
    return;
  }
  processWifiUartCommand(command);
}

void es8388_init(void)
{
    audiokit::AudioKit kit;
    auto cfg = kit.defaultConfig(audiokit::KitInputOutput);
    cfg.sample_rate = AUDIO_HAL_16K_SAMPLES;
    cfg.i2s_active = false;
    Serial.println("set AudioKit");
    kit.begin(cfg);
    kit.setVolume(100);
}

// This task does all the heavy lifting for our application
void applicationTask(void *param)
{
  Application *application = static_cast<Application *>(param);

  const TickType_t xMaxBlockTime = pdMS_TO_TICKS(100);
  while (true)
  {
    // wait for some audio samples to arrive
    uint32_t ulNotificationValue = ulTaskNotifyTake(pdTRUE, xMaxBlockTime);
    if (ulNotificationValue > 0)
    {
      application->run();
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Starting up");
  Serial.println("UART WiFi provisioning enabled. Type WIFI HELP and press Enter.");
  Serial.println("UART LED control enabled. Type LED HELP and press Enter.");
  Serial.println("UART TTS enabled. Type TTS HELP and press Enter.");
  Serial.println("UART LLM enabled. Type LLM HELP and press Enter.");
  Serial.println("UART KEY management enabled. Type KEY HELP and press Enter.");

#ifdef BOARD_HAS_PSRAM
  // Prefer external RAM for generic malloc to keep internal RAM for TLS handshake.
  heap_caps_malloc_extmem_enable(0);
#endif

  // start up wifi
  // launch WiFi
  String wifiSsid;
  String wifiPassword;
  loadWifiCredentials(wifiSsid, wifiPassword);
  Serial.printf("Connecting to SSID: %s\n", wifiSsid.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    //ESP.restart();
  }
  Serial.printf("Total heap: %d\n", ESP.getHeapSize());
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());
  Serial.printf("Internal heap free: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("Internal heap largest block: %u\n", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.printf("Chip model: %s\n", ESP.getChipModel());

  // startup LittleFS for the wav files
  LittleFS.begin();
  // make sure we don't get killed for our long running tasks
  esp_task_wdt_init(10, false);

  es8388_init();
  // start up the I2S input (from either an I2S microphone or Analogue microphone via the ADC)
#ifdef USE_I2S_MIC_INPUT
  // Direct i2s input from INMP441 or the SPH0645
  I2SSampler *i2s_sampler = new I2SMicSampler(i2s_codec_pins, false);
#else
  // Use the internal ADC
  I2SSampler *i2s_sampler = new ADCSampler(ADC_UNIT_1, ADC_MIC_CHANNEL);
#endif

  // start the i2s speaker output
  I2SOutput *i2s_output = new I2SOutput();
  i2s_output->start(I2S_NUM_0, i2s_codec_pins, i2sCodecConfig);
  Speaker *speaker = new Speaker(i2s_output);

  g_speaker = speaker;

  // Load OpenAI API key from NVS (falls back to config.h default)
  loadOpenAIKey();
  Serial.printf("OpenAI API key: %s...\n", g_openaiApiKey.substring(0, 8).c_str());

  g_llm = new OpenAILLM(OPENAI_API_KEY, OPENAI_LLM_MODEL);
  g_llm->setApiKey(g_openaiApiKey.c_str());
  g_speaker->tts()->setApiKey(g_openaiApiKey.c_str());

  // indicator light to show when we are listening
  IndicatorLight *indicator_light = new IndicatorLight();
  g_indicatorLight = indicator_light;

  // --- Skill system initialization ---
  // 1. Scan LittleFS for skill definitions (SKILL.md files)
  int skillCount = SkillRegistry::instance().scanDirectory("/skills");
  Serial.printf("Loaded %d skill(s) from LittleFS\n", skillCount);

  // 2. Register C++ hardware handlers
  registerLedHandlers(SkillRegistry::instance(), indicator_light);

  // 3. Connect skill registry to LLM
  g_llm->setSkillRegistry(&SkillRegistry::instance());
  Serial.println("Skill system initialized.");

  // and the intent processor
  IntentProcessor *intent_processor = new IntentProcessor(speaker, g_llm);
  /*
  intent_processor->addDevice("kitchen", GPIO_NUM_5);
  intent_processor->addDevice("bedroom", GPIO_NUM_21);
  intent_processor->addDevice("table", GPIO_NUM_23);
  */

  // create our application
  Application *application = new Application(i2s_sampler, intent_processor, speaker, indicator_light);

  // set up the i2s sample writer task
  TaskHandle_t applicationTaskHandle;
  xTaskCreate(applicationTask, "Application Task", 16384, application, 1, &applicationTaskHandle);

  // start sampling from i2s device - use I2S_NUM_0 as that's the one that supports the internal ADC
#ifdef USE_I2S_MIC_INPUT
  i2s_sampler->start(I2S_NUM_0, i2sCodecConfig, applicationTaskHandle);
#else
  i2s_sampler->start(I2S_NUM_0, adcI2SConfig, applicationTaskHandle);
#endif
}

void loop()
{
  while (Serial.available() > 0)
  {
    char ch = static_cast<char>(Serial.read());

    if (ch == '\r' || ch == '\n')
    {
      if (g_uartInputLine.length() > 0)
      {
        Serial.println();
        handleUartWifiProvisioning(g_uartInputLine);
        g_uartInputLine = "";
      }
      continue;
    }

    g_uartInputLine += ch;
    Serial.print(ch);
  }

  vTaskDelay(10);
}
