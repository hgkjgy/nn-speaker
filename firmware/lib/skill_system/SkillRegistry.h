#ifndef _skill_registry_h_
#define _skill_registry_h_

#include <Arduino.h>
#include <FS.h>
#include <vector>
#include <map>
#include <functional>
#include "Skill.h"

/**
 * Handler function type for skill command execution.
 *
 * @param command   The command name (e.g. "set_led").
 * @param params    Key-value parameter map from the LLM tag.
 * @return "OK ..." on success, "ERR ..." on failure.
 */
typedef std::function<String(const String &command, std::map<String, String> &params)> SkillHandler;

/**
 * Singleton registry that holds skill definitions (loaded from LittleFS)
 * and handler functions (registered from C++).
 *
 * Skill metadata is read from SKILL.md files; execution logic is
 * provided by registering SkillHandler functions keyed by handler name.
 */
class SkillRegistry
{
private:
    std::vector<Skill *> m_skills;                       // owned skill objects
    std::map<String, SkillHandler> m_handlers;           // handler-name → function

    SkillRegistry() {}

    /** Recursive helper: walk dir tree and load any SKILL.md found. */
    void scanRecursive(File &dir, int &loaded);

public:
    /** Get the singleton instance. */
    static SkillRegistry &instance();

    // ---- LittleFS scanning ----

    /**
     * Scan a base directory on LittleFS for skill definitions.
     *
     * Looks for SKILL.md files under basePath/<name>/SKILL.md,
     * parses each and stores the resulting Skill objects.
     *
     * @param basePath  Root path on LittleFS (default "/skills").
     * @return Number of skills successfully loaded.
     */
    int scanDirectory(const String &basePath = "/skills");

    // ---- Handler registration ----

    /**
     * Register a C++ handler function under the given name.
     *
     * Skill commands reference handlers by name in their YAML
     * frontmatter (handler field).  This method maps that name
     * to an actual callable.
     */
    void registerHandler(const String &handlerName, SkillHandler handler);

    // ---- Query ----

    /** Retrieve a skill by name (returns nullptr if not found). */
    Skill *getSkill(const String &name);

    /** Return a combined summary of every registered skill. */
    String getAllSummaries() const;

    /**
     * Generate the full system-prompt section that describes the
     * skill system to the LLM, including command format and all
     * skill details (now uses summary-only for progressive loading).
     */
    String generateSystemPrompt() const;

    /**
     * Generate a summary-only system prompt listing available skills.
     * Used in the first stage of two-phase progressive loading.
     * The LLM can reply with [SKILL_REQUEST:name] to request detail.
     */
    String generateSummaryPrompt() const;

    /**
     * Get full detail for a single skill by name.
     * Used in the second stage when the LLM requests detail via
     * [SKILL_REQUEST:name].
     *
     * @param skillName  The skill name to look up.
     * @return Detail string with guidance prefix, or empty if not found.
     */
    String generateDetailForSkill(const String &skillName) const;

    /**
     * Find a Skill object by name (internal helper).
     *
     * @param skillName  The skill name to look up.
     * @return Pointer to the matching Skill, or nullptr if not found.
     */
    Skill *findSkillByName(const String &skillName) const;

    // ---- Execution ----

    /**
     * Execute a command on the named skill.
     *
     * Looks up the command's handler field and dispatches to the
     * corresponding registered SkillHandler.
     *
     * @return "OK ..." on success, "ERR ..." on failure.
     */
    String executeCommand(const String &skillName,
                          const String &commandName,
                          std::map<String, String> &params);

    /**
     * Parse a skill-command tag from the LLM reply and execute it.
     * Expected format: [SKILL:skill_name:command_name:key1=val1,key2=val2]
     *
     * @param  reply  The full LLM reply text.
     * @param  result (out) The execution result string.
     * @return true if a skill command was found and executed.
     */
    bool parseAndExecute(const String &reply, String &result);
};

#endif
