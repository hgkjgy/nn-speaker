#ifndef _led_skill_handler_h_
#define _led_skill_handler_h_

class SkillRegistry;
class IndicatorLight;

/**
 * Register LED hardware handler functions with the skill registry.
 *
 * This registers two handlers:
 *   - "led_set"   : sets LED to on / off / blink based on "action" param
 *   - "led_blink" : sets LED to pulsing mode (no params needed)
 *
 * The handler names must match the "handler" fields in the LED
 * skill's SKILL.md frontmatter on LittleFS.
 *
 * @param registry  The SkillRegistry to register handlers into.
 * @param light     Pointer to the IndicatorLight instance (must outlive the handlers).
 */
void registerLedHandlers(SkillRegistry &registry, IndicatorLight *light);

#endif
