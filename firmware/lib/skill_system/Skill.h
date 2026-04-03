#ifndef _skill_h_
#define _skill_h_

#include <Arduino.h>
#include <vector>
#include <map>

/**
 * Describes one parameter that a skill command accepts.
 */
struct SkillParameter
{
    String name;
    String type;         // e.g. "string", "int", "enum"
    bool required;
    String description;
    String defaultValue;
    std::vector<String> enumValues; // valid values when type == "enum"
};

/**
 * Describes one command exposed by a skill.
 */
struct SkillCommand
{
    String name;
    String handler;     // C++ handler name, e.g. "led_set"
    String description;
    String format;      // template string, e.g. "[SKILL:led_control:set_led:action={action}]"
};

/**
 * Data-only skill definition loaded from a SKILL.md file on LittleFS.
 *
 * All metadata (name, version, description, parameters, commands) is
 * parsed from the YAML frontmatter of the SKILL.md file.  The C++
 * execution logic is registered separately via SkillRegistry handlers.
 */
class Skill
{
public:
    String name;
    String version;
    String description;
    String body;        // markdown body after frontmatter
    std::vector<SkillParameter> parameters;
    std::vector<SkillCommand> commands;

    /**
     * Load skill definition from a SKILL.md file on LittleFS.
     *
     * Parses the YAML frontmatter (between --- delimiters) and
     * populates all fields.
     *
     * @param filepath  Absolute LittleFS path, e.g. "/skills/led/SKILL.md"
     * @return true on success, false on parse/read error.
     */
    bool loadFromFile(const String &filepath);

    /**
     * Generate a short frontmatter-style summary for this skill.
     * Used in the LLM system prompt to list available skills.
     */
    String generateSummary() const
    {
        String s;
        s += "- **" + name + "** (v" + version + "): " + description + "\n";
        s += "  Commands: ";
        for (size_t i = 0; i < commands.size(); ++i)
        {
            if (i > 0) s += ", ";
            s += commands[i].name;
        }
        s += "\n";
        return s;
    }

    /**
     * Generate a detailed description including parameters and
     * command formats.  Used when the LLM needs full skill info.
     */
    String generateDetail() const
    {
        String d;
        d += "## Skill: " + name + " (v" + version + ")\n";
        d += description + "\n\n";

        // Parameters
        if (!parameters.empty())
        {
            d += "### Parameters\n";
            for (const auto &p : parameters)
            {
                d += "- **" + p.name + "** (" + p.type + ")";
                if (p.required) d += " [required]";
                d += ": " + p.description;
                if (p.defaultValue.length() > 0)
                {
                    d += " (default: " + p.defaultValue + ")";
                }
                if (!p.enumValues.empty())
                {
                    d += " values: ";
                    for (size_t i = 0; i < p.enumValues.size(); ++i)
                    {
                        if (i > 0) d += ", ";
                        d += p.enumValues[i];
                    }
                }
                d += "\n";
            }
            d += "\n";
        }

        // Commands
        if (!commands.empty())
        {
            d += "### Commands\n";
            for (const auto &c : commands)
            {
                d += "- **" + c.name + "**: " + c.description + "\n";
                d += "  Format: `" + c.format + "`\n";
            }
        }

        return d;
    }
};

#endif
