#ifndef __openai_llm_h__
#define __openai_llm_h__

#include <Arduino.h>

class SkillRegistry;

/**
 * Minimal OpenAI Chat Completions client for ESP32.
 *
 * Calls POST https://api.openai.com/v1/chat/completions
 * and returns the assistant's reply as a String.
 *
 * Currently supports single-turn conversation only
 * (one system message + one user message).
 */
class OpenAILLM
{
private:
    static const uint8_t MAX_STORED_MESSAGES = 12;

    String m_api_key_storage;
    const char *m_api_key;
    const char *m_model;          // e.g. "gpt-4o-mini"
    const char *m_system_prompt;  // optional system prompt
    SkillRegistry *m_skill_registry; // optional skill registry
    String m_history_roles[MAX_STORED_MESSAGES];
    String m_history_contents[MAX_STORED_MESSAGES];
    uint8_t m_history_count;
    uint8_t m_max_history_messages;

    void appendHistoryMessage(const char *role, const String &content);
    String sendChatRequest(const String &request_body);

    /**
     * Parse a [SKILL_REQUEST:xxx] tag from LLM response.
     * @param response  The LLM reply text.
     * @return The skill name if found, or empty String if not.
     */
    String parseSkillRequest(const String &response);

public:
    /**
     * @param api_key        Your OpenAI API key (Bearer token).
     * @param model          Model name, default "gpt-4o-mini".
     * @param system_prompt  Optional system prompt, default nullptr.
     */
    OpenAILLM(const char *api_key,
              const char *model = "gpt-4o-mini",
              const char *system_prompt = nullptr);

    /** Set or change the system prompt. */
    void setSystemPrompt(const char *prompt) { m_system_prompt = prompt; }

    /** Set the skill registry for chatV3 integration. */
    void setSkillRegistry(SkillRegistry *registry) { m_skill_registry = registry; }

    /** Set or change the model. */
    void setModel(const char *model) { m_model = model; }

    /** Set or change the API key (stores an owned copy). */
    void setApiKey(const char *key) { m_api_key_storage = key; m_api_key = m_api_key_storage.c_str(); }

    /** Clear conversation history (keeps current system prompt). */
    void clearHistory();

    /**
     * Set max number of history messages kept in memory.
     * (message unit = one role/content item, not one turn)
     */
    void setMaxHistoryMessages(uint8_t max_messages);

    /**
     * Send a user message and return the assistant's reply.
     *
     * Previous turns are included automatically (multi-turn chat).
     *
     * @param user_message  The user's input text (UTF-8).
     * @return The assistant's reply on success, or an empty String on failure.
     */
    String chatV2(const char *user_message);
    String chatV1(const char *user_message);

    /**
     * Tool handler callback type.
     * Receives the assistant reply. Should return a non-empty feedback
     * string if a tool call was detected and handled (e.g. "檔案 x.txt 已寫入"),
     * or an empty String if no tool call was found (= final answer).
     */
    typedef String (*ToolHandler)(const String &reply);

    /**
     * Multi-turn chat with automatic tool-call loop.
     *
     * Injects a tool-use system prompt, sends the user message via chatV2,
     * then checks the reply with toolHandler.  If the handler returns
     * non-empty feedback the feedback is sent as the next user turn and
     * the loop repeats – up to maxIterations times.
     *
     * @param user_message   The user's input text (UTF-8).
     * @param toolHandler    Callback that inspects the reply for tool calls.
     * @param maxIterations  Maximum number of tool-call rounds (default 5).
     * @return The final assistant reply (the one without a tool call).
     */
    String chatV3(const char *user_message,
                  ToolHandler toolHandler,
                  uint8_t maxIterations = 5);
};

#endif
