#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "IntentProcessor.h"
#include "Speaker.h"

IntentProcessor::IntentProcessor(Speaker *speaker, OpenAILLM *llm)
{
    m_speaker = speaker;
    m_llm = llm;
}

String IntentProcessor::noOpToolHandler(const String &reply)
{
    // No legacy tool handling needed; skill execution is handled
    // inside chatV3 via SkillRegistry.
    return String();
}

IntentResult IntentProcessor::turnOnDevice(const Intent &intent)
{
    if (intent.intent_confidence < 0.8)
    {
        Serial.printf("Only %.f%% certain on intent\n", 100 * intent.intent_confidence);
        return FAILED;
    }
    if (intent.device_name.empty())
    {
        Serial.println("No device found");
        return FAILED;
    }
    if (intent.device_confidence < 0.8)
    {
        Serial.printf("Only %.f%% certain on device\n", 100 * intent.device_confidence);
        return FAILED;
    }
    if (intent.trait_value.empty())
    {
        Serial.println("Can't work out the intent action");
        return FAILED;
    }
    if (intent.trait_confidence < 0.8)
    {
        Serial.printf("Only %.f%% certain on trait\n", 100 * intent.trait_confidence);
        return FAILED;
    }
    bool is_turn_on = intent.trait_value == "on";

    // global device name "lights"
    if (intent.device_name == "lights")
    {
        for (const auto &dev_pin : m_device_to_pin)
        {
            digitalWrite(dev_pin.second, is_turn_on);
        }
    }
    else
    {
        // see if the device name is something we know about
        if (m_device_to_pin.find(intent.device_name) == m_device_to_pin.end())
        {
            Serial.printf("Don't recognise the device '%s'\n", intent.device_name.c_str());
            return FAILED;
        }
        digitalWrite(m_device_to_pin[intent.device_name], is_turn_on);
    }
    // success!
    return SUCCESS;
}

IntentResult IntentProcessor::tellJoke()
{
    m_speaker->playRandomJoke();
    return SILENT_SUCCESS;
}

IntentResult IntentProcessor::life()
{
    m_speaker->playLife();
    return SILENT_SUCCESS;
}

IntentResult IntentProcessor::processIntent(const Intent &intent, std::string &responseText)
{
    responseText.clear();

    if (intent.text.empty())
    {
        Serial.println("No text recognised");
        return FAILED;
    }
    Serial.printf("I heard \"%s\"\n", intent.text.c_str());


    if (!m_llm)
    {
        Serial.println("IntentProcessor: LLM not available");
        return FAILED;
    }

    // Delegate intent processing to the LLM (with skill system support)
    unsigned long startTime = millis();
    String reply = m_llm->chatV3(intent.text.c_str(), noOpToolHandler, 5);
    unsigned long elapsed = millis() - startTime;

    if (reply.length() == 0)
    {
        Serial.println("IntentProcessor: LLM returned empty response");
        return FAILED;
    }

    Serial.printf("IntentProcessor: LLM replied (%lu ms): %s\n", elapsed, reply.c_str());
    responseText = reply.c_str();
    return SUCCESS;
}

void IntentProcessor::addDevice(const std::string &name, int gpio_pin)
{
    m_device_to_pin.insert(std::make_pair(name, gpio_pin));
    pinMode(gpio_pin, OUTPUT);
}

