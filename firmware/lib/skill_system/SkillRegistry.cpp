#include "SkillRegistry.h"
#include "LittleFS.h"

SkillRegistry &SkillRegistry::instance()
{
    static SkillRegistry inst;
    return inst;
}

// ---- LittleFS scanning ----

void SkillRegistry::scanRecursive(File &dir, int &loaded)
{
    File entry = dir.openNextFile();
    while (entry)
    {
        String path = String(entry.path());
        if (path.length() == 0)
            path = String(entry.name());
        if (!path.startsWith("/"))
            path = "/" + path;

        Serial.printf("  scan: '%s' (dir=%d, size=%u)\n",
                      path.c_str(), entry.isDirectory(), (unsigned)entry.size());

        if (entry.isDirectory())
        {
            // Recurse into subdirectory
            scanRecursive(entry, loaded);
            entry.close();
            entry = dir.openNextFile();
            continue;
        }

        // Check if this file is a SKILL.md
        if (path.endsWith("/SKILL.md"))
        {
            Serial.printf("SkillRegistry: found skill file %s\n", path.c_str());

            entry.close();

            Skill *skill = new Skill();
            if (skill->loadFromFile(path))
            {
                // Avoid duplicate
                bool dup = false;
                for (const auto *s : m_skills)
                {
                    if (s->name == skill->name) { dup = true; break; }
                }
                if (!dup)
                {
                    m_skills.push_back(skill);
                    ++loaded;
                    Serial.printf("SkillRegistry: loaded skill \"%s\" from LittleFS\n",
                                  skill->name.c_str());
                }
                else
                {
                    Serial.printf("SkillRegistry: duplicate skill \"%s\", skipped\n",
                                  skill->name.c_str());
                    delete skill;
                }
            }
            else
            {
                Serial.printf("SkillRegistry: failed to parse %s\n", path.c_str());
                delete skill;
            }
        }
        else
        {
            entry.close();
        }

        entry = dir.openNextFile();
    }
}

int SkillRegistry::scanDirectory(const String &basePath)
{
    int loaded = 0;

    String normalizedBase = basePath;
    if (!normalizedBase.startsWith("/"))
        normalizedBase = "/" + normalizedBase;
    if (normalizedBase.endsWith("/") && normalizedBase.length() > 1)
        normalizedBase.remove(normalizedBase.length() - 1);

    Serial.printf("SkillRegistry::scanDirectory: opening %s\n", normalizedBase.c_str());

    File root = LittleFS.open(normalizedBase);
    if (!root)
    {
        Serial.printf("SkillRegistry::scanDirectory: cannot open %s\n", normalizedBase.c_str());
        return 0;
    }
    if (!root.isDirectory())
    {
        Serial.printf("SkillRegistry::scanDirectory: %s is not a directory\n", normalizedBase.c_str());
        root.close();
        return 0;
    }

    scanRecursive(root, loaded);
    root.close();

    Serial.printf("SkillRegistry::scanDirectory: %d skill(s) loaded from %s\n",
                  loaded, normalizedBase.c_str());
    return loaded;
}

// ---- Handler registration ----

void SkillRegistry::registerHandler(const String &handlerName, SkillHandler handler)
{
    m_handlers[handlerName] = handler;
    Serial.printf("SkillRegistry: registered handler \"%s\"\n", handlerName.c_str());
}

// ---- Query ----

Skill *SkillRegistry::getSkill(const String &name)
{
    for (auto *s : m_skills)
    {
        if (s->name == name) return s;
    }
    return nullptr;
}

String SkillRegistry::getAllSummaries() const
{
    String summaries;
    for (const auto *s : m_skills)
    {
        summaries += s->generateSummary();
    }
    return summaries;
}

String SkillRegistry::generateSystemPrompt() const
{
    // Delegate to summary-only prompt for progressive loading
    return generateSummaryPrompt();
}

String SkillRegistry::generateSummaryPrompt() const
{
    if (m_skills.empty()) return String();

    String prompt;
    prompt += "你有以下技能可以使用。當使用者的請求需要使用某個技能時，";
    prompt += "請回覆 [SKILL_REQUEST:技能名稱] 來啟動該技能。\n\n";
    prompt += "可用技能：\n";

    for (const auto *s : m_skills)
    {
        prompt += s->generateSummary();
    }

    prompt += "\n如果使用者的請求不需要任何技能，請直接用自然語言回應。\n";

    return prompt;
}

String SkillRegistry::generateDetailForSkill(const String &skillName) const
{
    Skill *skill = findSkillByName(skillName);
    if (!skill) return String();

    String prompt;
    prompt += "以下是技能 \"" + skillName + "\" 的詳細定義，";
    prompt += "請根據使用者的請求產生正確的命令標籤：\n\n";
    prompt += skill->generateDetail();
    prompt += "\n### 使用方式\n";
    prompt += "當使用者要求執行硬體操作時，在回覆中加入技能指令標籤。\n";
    prompt += "格式: [SKILL:技能名稱:指令名稱:參數1=值1,參數2=值2]\n";
    prompt += "範例: [SKILL:led_control:set_led:action=on]\n";
    prompt += "你可以在回覆文字中夾帶這個標籤，系統會自動解析並執行。\n";

    return prompt;
}

Skill *SkillRegistry::findSkillByName(const String &skillName) const
{
    for (auto *s : m_skills)
    {
        if (s->name == skillName) return s;
    }
    return nullptr;
}

// ---- Execution ----

String SkillRegistry::executeCommand(const String &skillName,
                                     const String &commandName,
                                     std::map<String, String> &params)
{
    Skill *skill = getSkill(skillName);
    if (!skill)
    {
        return "ERR skill not found: " + skillName;
    }

    // Find the command definition to get the handler name
    for (const auto &cmd : skill->commands)
    {
        if (cmd.name == commandName)
        {
            if (cmd.handler.length() == 0)
            {
                return "ERR command has no handler: " + commandName;
            }

            auto it = m_handlers.find(cmd.handler);
            if (it == m_handlers.end())
            {
                return "ERR handler not registered: " + cmd.handler;
            }

            // Dispatch to the registered handler
            return it->second(commandName, params);
        }
    }

    return "ERR unknown command: " + commandName + " in skill " + skillName;
}

bool SkillRegistry::parseAndExecute(const String &reply, String &result)
{
    // Look for [SKILL:...:...:...]
    int startIdx = reply.indexOf("[SKILL:");
    if (startIdx < 0) return false;

    int endIdx = reply.indexOf(']', startIdx);
    if (endIdx < 0) return false;

    // Extract the content between [SKILL: and ]
    String tag = reply.substring(startIdx + 7, endIdx); // skip "[SKILL:"

    // Split by ':'  format: skillName:commandName:params
    int firstColon = tag.indexOf(':');
    if (firstColon < 0)
    {
        result = "ERR invalid skill tag format";
        return true;
    }

    String skillName = tag.substring(0, firstColon);

    String rest = tag.substring(firstColon + 1);
    int secondColon = rest.indexOf(':');

    String commandName;
    String paramStr;

    if (secondColon < 0)
    {
        // No parameters
        commandName = rest;
    }
    else
    {
        commandName = rest.substring(0, secondColon);
        paramStr = rest.substring(secondColon + 1);
    }

    // Parse key=value pairs separated by ','
    std::map<String, String> params;
    if (paramStr.length() > 0)
    {
        int pos = 0;
        while (pos < (int)paramStr.length())
        {
            int comma = paramStr.indexOf(',', pos);
            String pair;
            if (comma < 0)
            {
                pair = paramStr.substring(pos);
                pos = paramStr.length();
            }
            else
            {
                pair = paramStr.substring(pos, comma);
                pos = comma + 1;
            }

            int eq = pair.indexOf('=');
            if (eq > 0)
            {
                String key = pair.substring(0, eq);
                String val = pair.substring(eq + 1);
                key.trim();
                val.trim();
                params[key] = val;
            }
        }
    }

    Serial.printf("SkillRegistry: executing [%s:%s] with %d params\n",
                  skillName.c_str(), commandName.c_str(), params.size());

    result = executeCommand(skillName, commandName, params);
    return true;
}
