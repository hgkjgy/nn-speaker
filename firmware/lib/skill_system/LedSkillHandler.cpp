#include "LedSkillHandler.h"
#include "SkillRegistry.h"
#include "IndicatorLight.h"

void registerLedHandlers(SkillRegistry &registry, IndicatorLight *light)
{
    // Handler: led_set — set LED to on / off / blink
    registry.registerHandler("led_set",
        [light](const String &command, std::map<String, String> &params) -> String
        {
            if (!light)
            {
                return "ERR IndicatorLight not available";
            }

            auto it = params.find("action");
            if (it == params.end())
            {
                return "ERR missing required parameter: action";
            }

            String action = it->second;
            action.toLowerCase();

            if (action == "on")
            {
                light->setState(ON);
                Serial.println("LedSkillHandler: LED ON");
                return "OK LED turned on";
            }
            else if (action == "off")
            {
                light->setState(OFF);
                Serial.println("LedSkillHandler: LED OFF");
                return "OK LED turned off";
            }
            else if (action == "blink")
            {
                light->setState(PULSING);
                Serial.println("LedSkillHandler: LED PULSING");
                return "OK LED set to pulsing/blink";
            }
            else
            {
                return "ERR unknown action: " + action + " (use on/off/blink)";
            }
        });

    // Handler: led_blink — shortcut to start pulsing
    registry.registerHandler("led_blink",
        [light](const String &command, std::map<String, String> &params) -> String
        {
            if (!light)
            {
                return "ERR IndicatorLight not available";
            }

            light->setState(PULSING);
            Serial.println("LedSkillHandler: LED PULSING (led_blink)");
            return "OK LED set to pulsing/blink";
        });

    Serial.println("LedSkillHandler: LED handlers registered");
}
