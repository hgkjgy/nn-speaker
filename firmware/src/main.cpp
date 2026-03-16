#include <Arduino.h>
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
#include "SPIFFS.h"
#include "IntentProcessor.h"
#include "Speaker.h"
#include "IndicatorLight.h"
#include "AudioKitHAL.h"
#include "OpenAILLM.h"

// ===== Memory buffers =====
uint8_t* internal_buf = nullptr;
uint8_t* dma_buf = nullptr;
uint8_t* psram_buf = nullptr;

// ===== WiFi / UART =====
static Preferences g_wifiPrefs;
static String g_uartInputLine;
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
    Serial.println("  TTS <text>   - synthesize and play the given text via OpenAI TTS");
    Serial.println("  TTS HELP     - show this help");
    return;
  }

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
    Serial.println("  LLM <text>            - send a message to OpenAI Chat API");
    Serial.println("  LLM SYSTEM <prompt>   - set the system prompt");
    Serial.println("  LLM MODEL <name>      - change the model (e.g. gpt-4o)");
    Serial.println("  LLM HELP              - show this help");
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

  if (command.startsWith("LLM "))
  {
    String text = command.substring(strlen("LLM "));
    text.trim();
    if (text.length() == 0)
    {
      Serial.println("Usage: LLM <text>");
      return;
    }

    Serial.printf("LLM: sending \"%s\" ...\n", text.c_str());
    unsigned long startTime = millis();
    String reply = g_llm->chat(text.c_str());
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

static void handleUartWifiProvisioning(const String &line)
{
  String command = trimCopy(line);

  if (command.startsWith("LLM ") || command.equalsIgnoreCase("LLM HELP"))
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

// ===== Audio init =====
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

// ===== Memory observation =====
void printMemoryStatus(const char* title)
{
  Serial.println();
  Serial.println("========================================");
  Serial.println(title);
  Serial.println("========================================");

  Serial.printf("Heap total size            : %u bytes\n", ESP.getHeapSize());
  Serial.printf("Heap free size             : %u bytes\n", ESP.getFreeHeap());
  Serial.printf("Heap min free size         : %u bytes\n", ESP.getMinFreeHeap());
  Serial.printf("Heap max alloc size        : %u bytes\n", ESP.getMaxAllocHeap());

  Serial.println();

  Serial.printf("Free MALLOC_CAP_INTERNAL   : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("Free MALLOC_CAP_8BIT       : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_8BIT));
  Serial.printf("Free MALLOC_CAP_DMA        : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_DMA));
  Serial.printf("Free MALLOC_CAP_SPIRAM     : %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

  Serial.println();

  Serial.printf("Largest block INTERNAL     : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.printf("Largest block 8BIT         : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  Serial.printf("Largest block DMA          : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
  Serial.printf("Largest block SPIRAM       : %u bytes\n",
                heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));

  Serial.println("========================================");
}

void allocateMemory()
{
  Serial.println();
  Serial.println("Allocating memory...");

  internal_buf = (uint8_t*)heap_caps_malloc(16 * 1024, MALLOC_CAP_INTERNAL);
  dma_buf = (uint8_t*)heap_caps_malloc(8 * 1024, MALLOC_CAP_DMA);
  psram_buf = (uint8_t*)heap_caps_malloc(64 * 1024, MALLOC_CAP_SPIRAM);

  if (internal_buf != nullptr) {
    Serial.println("[OK] Internal SRAM allocated: 16 KB");
    memset(internal_buf, 0x11, 16 * 1024);
  } else {
    Serial.println("[FAIL] Internal SRAM allocation failed");
  }

  if (dma_buf != nullptr) {
    Serial.println("[OK] DMA memory allocated: 8 KB");
    memset(dma_buf, 0x22, 8 * 1024);
  } else {
    Serial.println("[FAIL] DMA memory allocation failed");
  }

  if (psram_buf != nullptr) {
    Serial.println("[OK] PSRAM allocated: 64 KB");
    memset(psram_buf, 0x33, 64 * 1024);
  } else {
    Serial.println("[FAIL] PSRAM allocation failed");
    Serial.println("Note: Your board may not support PSRAM, or PSRAM is not enabled.");
  }
}

void freeMemory()
{
  Serial.println();
  Serial.println("Freeing memory...");

  if (internal_buf != nullptr) {
    free(internal_buf);
    internal_buf = nullptr;
    Serial.println("[OK] Freed internal SRAM");
  }

  if (dma_buf != nullptr) {
    free(dma_buf);
    dma_buf = nullptr;
    Serial.println("[OK] Freed DMA memory");
  }

  if (psram_buf != nullptr) {
    free(psram_buf);
    psram_buf = nullptr;
    Serial.println("[OK] Freed PSRAM");
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
  Serial.println("ESP32 Exercise 2 - Memory Observation and Allocation");

  // WiFi startup
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
    // ESP.restart();
  }

  Serial.printf("Total heap: %d\n", ESP.getHeapSize());
  Serial.printf("Free heap: %d\n", ESP.getFreeHeap());
  Serial.printf("Total PSRAM: %d\n", ESP.getPsramSize());
  Serial.printf("Free PSRAM: %d\n", ESP.getFreePsram());
  Serial.printf("Internal heap free: %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.printf("Internal heap largest block: %u\n", heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.printf("Chip model: %s\n", ESP.getChipModel());

  printMemoryStatus("=== Before Allocation ===");
  allocateMemory();
  printMemoryStatus("=== After Allocation ===");
  freeMemory();
  printMemoryStatus("=== After Free ===");

  // startup SPIFFS for the wav files
  SPIFFS.begin();

  // make sure we don't get killed for our long running tasks
  esp_task_wdt_init(10, false);

  es8388_init();

#ifdef USE_I2S_MIC_INPUT
  I2SSampler *i2s_sampler = new I2SMicSampler(i2s_codec_pins, false);
#else
  I2SSampler *i2s_sampler = new ADCSampler(ADC_UNIT_1, ADC_MIC_CHANNEL);
#endif

  I2SOutput *i2s_output = new I2SOutput();
  i2s_output->start(I2S_NUM_0, i2s_codec_pins, i2sCodecConfig);

  Speaker *speaker = new Speaker(i2s_output);
  g_speaker = speaker;

  g_llm = new OpenAILLM(OPENAI_API_KEY, "gpt-4o-mini");

  IndicatorLight *indicator_light = new IndicatorLight();
  g_indicatorLight = indicator_light;

  IntentProcessor *intent_processor = new IntentProcessor(speaker);
  /*
  intent_processor->addDevice("kitchen", GPIO_NUM_5);
  intent_processor->addDevice("bedroom", GPIO_NUM_21);
  intent_processor->addDevice("table", GPIO_NUM_23);
  */

  Application *application = new Application(i2s_sampler, intent_processor, speaker, indicator_light);

  TaskHandle_t applicationTaskHandle;
  xTaskCreate(applicationTask, "Application Task", 4096, application, 1, &applicationTaskHandle);

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