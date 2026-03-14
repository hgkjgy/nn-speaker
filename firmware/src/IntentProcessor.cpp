#include <Arduino.h>
#include "IntentProcessor.h"
#include "Speaker.h"

IntentProcessor::IntentProcessor(Speaker *speaker)
{
    m_speaker = speaker;
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

IntentResult IntentProcessor::processIntent(const Intent &intent)
{
    if (intent.text.empty())
    {
        Serial.println("No text recognised");
        return FAILED;
    }
    Serial.printf("I heard \"%s\"\n", intent.text.c_str());

    const bool has_open_keyword = intent.text.find("開") != std::string::npos;
    const bool has_close_keyword = intent.text.find("關") != std::string::npos;
    const bool has_light_keyword = intent.text.find("燈") != std::string::npos;

    if (has_open_keyword && has_light_keyword)
    {
        Serial.printf("Turning on the light\n");
        m_speaker->playLightOn();
        return SILENT_SUCCESS;
    }

    if (has_close_keyword && has_light_keyword)
    {
        Serial.printf("Turning off the light\n");
        
        m_speaker->playLightOff();
        return SILENT_SUCCESS;
    }

    return FAILED;
}

void IntentProcessor::addDevice(const std::string &name, int gpio_pin)
{
    m_device_to_pin.insert(std::make_pair(name, gpio_pin));
    pinMode(gpio_pin, OUTPUT);
}
