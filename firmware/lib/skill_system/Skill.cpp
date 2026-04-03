#include "Skill.h"
#include "LittleFS.h"

// ---------- Simple YAML frontmatter helpers ----------

static String trimQuotes(const String &s)
{
    String t = s;
    t.trim();
    if (t.length() >= 2)
    {
        char first = t.charAt(0);
        char last  = t.charAt(t.length() - 1);
        if ((first == '"' && last == '"') || (first == '\'' && last == '\''))
        {
            t = t.substring(1, t.length() - 1);
        }
    }
    return t;
}

/// Return the indentation level (number of leading spaces) of a line.
static int indentLevel(const String &line)
{
    int n = 0;
    for (int i = 0; i < (int)line.length(); ++i)
    {
        if (line.charAt(i) == ' ') ++n;
        else break;
    }
    return n;
}

/// Check if a line is a YAML list item (starts with optional indent + "- ")
static bool isListItem(const String &line)
{
    String t = line;
    t.trim();
    return t.startsWith("- ");
}

/// Extract the part after "- " from a list item line.
static String listItemValue(const String &line)
{
    String t = line;
    t.trim();
    if (t.startsWith("- "))
        return t.substring(2);
    return t;
}

/// Parse an inline YAML list like ["on", "off", "blink"]
static std::vector<String> parseInlineList(const String &s)
{
    std::vector<String> result;
    String t = s;
    t.trim();
    if (t.startsWith("[") && t.endsWith("]"))
    {
        t = t.substring(1, t.length() - 1);
    }
    int pos = 0;
    while (pos < (int)t.length())
    {
        int comma = t.indexOf(',', pos);
        String item;
        if (comma < 0)
        {
            item = t.substring(pos);
            pos = t.length();
        }
        else
        {
            item = t.substring(pos, comma);
            pos = comma + 1;
        }
        item = trimQuotes(item);
        if (item.length() > 0)
            result.push_back(item);
    }
    return result;
}

// ---------- Skill::loadFromFile ----------

bool Skill::loadFromFile(const String &filepath)
{
    File f = LittleFS.open(filepath, "r");
    if (!f)
    {
        Serial.printf("Skill::loadFromFile: cannot open %s\n", filepath.c_str());
        return false;
    }

    String content = f.readString();
    f.close();

    // --- Split frontmatter and body ---
    if (!content.startsWith("---"))
    {
        Serial.println("Skill::loadFromFile: missing frontmatter delimiter");
        return false;
    }

    int secondDelim = content.indexOf("\n---", 3);
    if (secondDelim < 0)
    {
        Serial.println("Skill::loadFromFile: missing closing frontmatter delimiter");
        return false;
    }

    String frontmatter = content.substring(4, secondDelim); // skip "---\n"
    // Body starts after the closing "---\n"
    int bodyStart = content.indexOf('\n', secondDelim + 1);
    if (bodyStart >= 0)
        body = content.substring(bodyStart + 1);
    else
        body = "";

    // --- Parse frontmatter line by line ---
    // We support:
    //   key: value                (simple key-value)
    //   key:                      (start of block — list or nested object)
    //     - value                 (list item, plain string)
    //     - name: xxx             (list item that is a mapping)
    //       key2: val2

    // Collect lines
    std::vector<String> lines;
    {
        int pos = 0;
        while (pos < (int)frontmatter.length())
        {
            int nl = frontmatter.indexOf('\n', pos);
            if (nl < 0)
            {
                lines.push_back(frontmatter.substring(pos));
                break;
            }
            lines.push_back(frontmatter.substring(pos, nl));
            pos = nl + 1;
        }
    }

    // State machine for parsing
    enum Section { SEC_ROOT, SEC_PARAMETERS, SEC_COMMANDS };
    Section section = SEC_ROOT;

    // Current nested object being built
    SkillParameter curParam;
    SkillCommand curCmd;
    bool inParam = false;
    bool inCmd = false;
    // For collecting enum_values that come as multi-line list
    bool collectingEnumValues = false;
    int enumIndent = 0;

    auto flushParam = [&]() {
        if (inParam)
        {
            parameters.push_back(curParam);
            curParam = SkillParameter();
            inParam = false;
        }
        collectingEnumValues = false;
    };

    auto flushCmd = [&]() {
        if (inCmd)
        {
            commands.push_back(curCmd);
            curCmd = SkillCommand();
            inCmd = false;
        }
    };

    for (size_t i = 0; i < lines.size(); ++i)
    {
        String &line = lines[i];

        // Skip blank lines or comment lines
        {
            String trimmed = line;
            trimmed.trim();
            if (trimmed.length() == 0 || trimmed.startsWith("#"))
                continue;
        }

        int indent = indentLevel(line);
        String trimmed = line;
        trimmed.trim();

        // Handle enum_values multi-line list items
        if (collectingEnumValues && isListItem(trimmed) && indent >= enumIndent)
        {
            String val = listItemValue(trimmed);
            val = trimQuotes(val);
            curParam.enumValues.push_back(val);
            continue;
        }
        else if (collectingEnumValues && indent < enumIndent)
        {
            collectingEnumValues = false;
        }

        // Top-level keys (indent == 0)
        if (indent == 0)
        {
            // Flush any pending nested objects before switching section
            flushParam();
            flushCmd();

            int colon = trimmed.indexOf(':');
            if (colon < 0) continue;

            String key = trimmed.substring(0, colon);
            String val = trimmed.substring(colon + 1);
            key.trim();
            val.trim();

            if (key == "name")
            {
                name = trimQuotes(val);
                section = SEC_ROOT;
            }
            else if (key == "version")
            {
                version = trimQuotes(val);
                section = SEC_ROOT;
            }
            else if (key == "description")
            {
                description = trimQuotes(val);
                section = SEC_ROOT;
            }
            else if (key == "parameters")
            {
                section = SEC_PARAMETERS;
            }
            else if (key == "commands")
            {
                section = SEC_COMMANDS;
            }
            else
            {
                // Other top-level keys (author, enabled, tags, transport…) — ignore
                section = SEC_ROOT;
            }
            continue;
        }

        // Inside parameters section
        if (section == SEC_PARAMETERS)
        {
            // A new list item at indent ~2 starts a new parameter
            if (isListItem(trimmed) && indent <= 4)
            {
                flushParam();
                inParam = true;

                // The list item may contain "name: xxx"
                String itemContent = listItemValue(trimmed);
                int colon = itemContent.indexOf(':');
                if (colon > 0)
                {
                    String k = itemContent.substring(0, colon);
                    String v = itemContent.substring(colon + 1);
                    k.trim(); v.trim();
                    v = trimQuotes(v);
                    if (k == "name") curParam.name = v;
                    else if (k == "type") curParam.type = v;
                    else if (k == "required") curParam.required = (v == "true");
                    else if (k == "description") curParam.description = v;
                    else if (k == "default") curParam.defaultValue = v;
                    else if (k == "enum_values")
                    {
                        if (v.length() > 0)
                            curParam.enumValues = parseInlineList(v);
                        else
                        {
                            collectingEnumValues = true;
                            enumIndent = indent + 2;
                        }
                    }
                }
            }
            else if (inParam && indent > 2)
            {
                // Continuation key of current parameter
                int colon = trimmed.indexOf(':');
                if (colon > 0)
                {
                    String k = trimmed.substring(0, colon);
                    String v = trimmed.substring(colon + 1);
                    k.trim(); v.trim();
                    v = trimQuotes(v);
                    if (k == "name") curParam.name = v;
                    else if (k == "type") curParam.type = v;
                    else if (k == "required") curParam.required = (v == "true");
                    else if (k == "description") curParam.description = v;
                    else if (k == "default") curParam.defaultValue = v;
                    else if (k == "enum_values")
                    {
                        if (v.length() > 0)
                            curParam.enumValues = parseInlineList(v);
                        else
                        {
                            collectingEnumValues = true;
                            enumIndent = indent + 2;
                        }
                    }
                }
            }
            continue;
        }

        // Inside commands section
        if (section == SEC_COMMANDS)
        {
            if (isListItem(trimmed) && indent <= 4)
            {
                flushCmd();
                inCmd = true;

                String itemContent = listItemValue(trimmed);
                int colon = itemContent.indexOf(':');
                if (colon > 0)
                {
                    String k = itemContent.substring(0, colon);
                    String v = itemContent.substring(colon + 1);
                    k.trim(); v.trim();
                    v = trimQuotes(v);
                    if (k == "name") curCmd.name = v;
                    else if (k == "handler") curCmd.handler = v;
                    else if (k == "format") curCmd.format = v;
                    else if (k == "description") curCmd.description = v;
                }
            }
            else if (inCmd && indent > 2)
            {
                int colon = trimmed.indexOf(':');
                if (colon > 0)
                {
                    String k = trimmed.substring(0, colon);
                    String v = trimmed.substring(colon + 1);
                    k.trim(); v.trim();
                    v = trimQuotes(v);
                    if (k == "name") curCmd.name = v;
                    else if (k == "handler") curCmd.handler = v;
                    else if (k == "format") curCmd.format = v;
                    else if (k == "description") curCmd.description = v;
                }
            }
            continue;
        }
    }

    // Flush last pending items
    flushParam();
    flushCmd();

    Serial.printf("Skill::loadFromFile: loaded \"%s\" v%s (%d params, %d cmds)\n",
                  name.c_str(), version.c_str(),
                  parameters.size(), commands.size());
    return name.length() > 0;
}
