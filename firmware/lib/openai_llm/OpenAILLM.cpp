#include "OpenAILLM.h"

#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Read timeout while waiting for server response (milliseconds).
#define READ_TIMEOUT_MS 30000

// Maximum JSON response size we are willing to buffer (bytes).
#define MAX_RESPONSE_BYTES (32 * 1024)

// --------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------

/**
 * Escape a string for safe embedding in JSON.
 */
static String jsonEscape(const char *text)
{
    String escaped;
    escaped.reserve(strlen(text) + 32);
    for (const char *p = text; *p; ++p)
    {
        switch (*p)
        {
        case '"':  escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n";  break;
        case '\r': escaped += "\\r";  break;
        case '\t': escaped += "\\t";  break;
        default:   escaped += *p;     break;
        }
    }
    return escaped;
}

static String jsonEscape(const String &text)
{
    return jsonEscape(text.c_str());
}

/**
 * Build the JSON request body for the Chat Completions API.
 */
static String buildRequestBodyV2(const char *model,
                               const char *system_prompt,
                               const String *history_roles,
                               const String *history_contents,
                               uint8_t history_count,
                               const char *user_message)
{
    String body;
    body.reserve(512 + strlen(user_message));

    body += "{\"model\":\"";
    body += model;
    body += "\",\"messages\":[";

    // Optional system message
    if (system_prompt && system_prompt[0] != '\0')
    {
        body += "{\"role\":\"system\",\"content\":\"";
        body += jsonEscape(system_prompt);
        body += "\"},";
    }

    // History messages
    for (uint8_t i = 0; i < history_count; ++i)
    {
        body += "{\"role\":\"";
        body += history_roles[i];
        body += "\",\"content\":\"";
        body += jsonEscape(history_contents[i]);
        body += "\"},";
    }

    // Current user message
    body += "{\"role\":\"user\",\"content\":\"";
    body += jsonEscape(user_message);
    body += "\"}]}";

    return body;
}

static String buildRequestBodyV1(const char *model,
                               const char *user_message)
{
    String body;
    body.reserve(512 + strlen(user_message));

    body += "{\"model\":\"";
    body += model;
    body += "\",\"messages\":[";


    // Current user message
    body += "{\"role\":\"user\",\"content\":\"";
    body += jsonEscape(user_message);
    body += "\"}]}";

    return body;
}



/**
 * Wait until data is available on the client or timeout.
 */
static bool waitForData(WiFiClientSecure *client, unsigned long timeout_ms)
{
    unsigned long start = millis();
    while (client->connected() && !client->available())
    {
        if (millis() - start > timeout_ms)
            return false;
        delay(10);
    }
    return client->available() > 0;
}

/**
 * Read one HTTP header line (terminated by \n).
 */
static String readHeaderLine(WiFiClientSecure *client, unsigned long timeout_ms)
{
    String line;
    unsigned long start = millis();
    while (client->connected())
    {
        if (client->available())
        {
            char c = (char)client->read();
            if (c == '\n')
                break;
            if (c != '\r')
                line += c;
        }
        else
        {
            if (millis() - start > timeout_ms)
                break;
            delay(1);
        }
    }
    return line;
}

/**
 * Read the full HTTP response body (supports Content-Length and chunked).
 */
static String readResponseBody(WiFiClientSecure *client, unsigned long timeout_ms)
{
    // ---- Parse headers ----
    int contentLength = -1;
    bool chunked = false;

    while (true)
    {
        String line = readHeaderLine(client, timeout_ms);
        if (line.length() == 0)
            break; // empty line = end of headers

        String lower = line;
        lower.toLowerCase();

        if (lower.startsWith("content-length:"))
        {
            contentLength = line.substring(15).toInt();
        }
        else if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0)
        {
            chunked = true;
        }
    }

    // ---- Read body ----
    String body;

    if (contentLength > 0)
    {
        // Known length
        if (contentLength > MAX_RESPONSE_BYTES)
            contentLength = MAX_RESPONSE_BYTES;

        body.reserve(contentLength);
        int remaining = contentLength;
        unsigned long start = millis();

        while (remaining > 0 && client->connected())
        {
            if (client->available())
            {
                char c = (char)client->read();
                body += c;
                remaining--;
                start = millis(); // reset on progress
            }
            else
            {
                if (millis() - start > timeout_ms)
                    break;
                delay(1);
            }
        }
    }
    else if (chunked)
    {
        // Chunked transfer encoding
        while (true)
        {
            String sizeLine = readHeaderLine(client, timeout_ms);
            sizeLine.trim();
            if (sizeLine.length() == 0)
            {
                sizeLine = readHeaderLine(client, timeout_ms);
                sizeLine.trim();
            }

            int chunkSize = (int)strtol(sizeLine.c_str(), NULL, 16);
            if (chunkSize <= 0)
                break;

            unsigned long start = millis();
            for (int i = 0; i < chunkSize && (int)body.length() < MAX_RESPONSE_BYTES; i++)
            {
                while (!client->available())
                {
                    if (!client->connected() || millis() - start > timeout_ms)
                        goto done;
                    delay(1);
                }
                body += (char)client->read();
                start = millis();
            }
            // Read trailing \r\n
            readHeaderLine(client, timeout_ms);
        }
    }
    else
    {
        // Fallback: read until connection closed
        unsigned long start = millis();
        while (client->connected() && (int)body.length() < MAX_RESPONSE_BYTES)
        {
            if (client->available())
            {
                body += (char)client->read();
                start = millis();
            }
            else
            {
                if (millis() - start > timeout_ms)
                    break;
                delay(1);
            }
        }
    }

done:
    return body;
}

// --------------------------------------------------------------------
// OpenAILLM
// --------------------------------------------------------------------

OpenAILLM::OpenAILLM(const char *api_key, const char *model, const char *system_prompt)
    : m_api_key(api_key),
      m_model(model),
      m_system_prompt(system_prompt),
      m_history_count(0),
      m_max_history_messages(MAX_STORED_MESSAGES)
{
}

void OpenAILLM::setMaxHistoryMessages(uint8_t max_messages)
{
    if (max_messages == 0)
    {
        m_max_history_messages = 1;
    }
    else if (max_messages > MAX_STORED_MESSAGES)
    {
        m_max_history_messages = MAX_STORED_MESSAGES;
    }
    else
    {
        m_max_history_messages = max_messages;
    }

    while (m_history_count > m_max_history_messages)
    {
        for (uint8_t i = 1; i < m_history_count; ++i)
        {
            m_history_roles[i - 1] = m_history_roles[i];
            m_history_contents[i - 1] = m_history_contents[i];
        }
        --m_history_count;
    }
}

void OpenAILLM::clearHistory()
{
    for (uint8_t i = 0; i < m_history_count; ++i)
    {
        m_history_roles[i] = "";
        m_history_contents[i] = "";
    }
    m_history_count = 0;
}

void OpenAILLM::appendHistoryMessage(const char *role, const String &content)
{
    if (!role || role[0] == '\0' || content.length() == 0)
    {
        return;
    }

    if (m_history_count >= m_max_history_messages)
    {
        for (uint8_t i = 1; i < m_history_count; ++i)
        {
            m_history_roles[i - 1] = m_history_roles[i];
            m_history_contents[i - 1] = m_history_contents[i];
        }
        --m_history_count;
    }

    m_history_roles[m_history_count] = role;
    m_history_contents[m_history_count] = content;
    ++m_history_count;
}

String OpenAILLM::chatV2(const char *user_message)
{
    if (!user_message || user_message[0] == '\0')
    {
        Serial.println("OpenAILLM: empty user message");
        return String();
    }

    // Build JSON body (message construction only)
    String requestBody = buildRequestBodyV2(
        m_model,
        m_system_prompt,
        m_history_roles,
        m_history_contents,
        m_history_count,
        user_message);
    Serial.printf("OpenAILLM: request body length = %d\n", requestBody.length());

    String result = sendChatRequest(requestBody);
    if (result.length() == 0)
    {
        return String();
    }

    // Save successful turn into memory for multi-turn chat.
    appendHistoryMessage("user", String(user_message));
    appendHistoryMessage("assistant", result);

    return result;
}

String OpenAILLM::chatV1(const char *user_message)
{
    if (!user_message || user_message[0] == '\0')
    {
        Serial.println("OpenAILLM: empty user message");
        return String();
    }

    // Build JSON body (message construction only)
    String requestBody = buildRequestBodyV1(m_model, user_message);
    Serial.printf("OpenAILLM: request body length = %d\n", requestBody.length());

    String result = sendChatRequest(requestBody);
    if (result.length() == 0)
    {
        return String();
    }

    return result;
}


String OpenAILLM::sendChatRequest(const String &request_body)
{
    // Connect via TLS
    WiFiClientSecure client;
    client.setInsecure(); // skip certificate verification (simplicity)

    const char *host = "api.openai.com";
    const int port = 443;

    Serial.printf("OpenAILLM: connecting to %s:%d ...\n", host, port);
    if (!client.connect(host, port))
    {
        Serial.println("OpenAILLM: connection failed");
        return String();
    }
    Serial.println("OpenAILLM: connected");

    // Send HTTP request
    client.printf("POST /v1/chat/completions HTTP/1.1\r\n");
    client.printf("Host: %s\r\n", host);
    client.printf("Authorization: Bearer %s\r\n", m_api_key);
    client.printf("Content-Type: application/json\r\n");
    client.printf("Content-Length: %d\r\n", request_body.length());
    client.printf("Connection: close\r\n");
    client.printf("\r\n");
    client.print(request_body);
    Serial.printf("OpenAILLM: %s\n", request_body.c_str());

    // Wait for response
    if (!waitForData(&client, READ_TIMEOUT_MS))
    {
        Serial.println("OpenAILLM: timeout waiting for response");
        client.stop();
        return String();
    }

    // Read status line
    String statusLine = readHeaderLine(&client, READ_TIMEOUT_MS);
    Serial.printf("OpenAILLM: %s\n", statusLine.c_str());

    // Check for HTTP 200
    if (statusLine.indexOf("200") < 0)
    {
        Serial.println("OpenAILLM: non-200 response");
        // Read and print the error body for debugging
        String errBody = readResponseBody(&client, READ_TIMEOUT_MS);
        Serial.println(errBody);
        client.stop();
        return String();
    }

    // Read body (headers are consumed inside readResponseBody)
    String body = readResponseBody(&client, READ_TIMEOUT_MS);
    client.stop();

    Serial.printf("OpenAILLM: response body length = %d\n", body.length());

    // Parse JSON to extract assistant reply
    // Use ArduinoJson to parse the response
    DynamicJsonDocument doc(MAX_RESPONSE_BYTES);
    DeserializationError err = deserializeJson(doc, body);
    if (err)
    {
        Serial.printf("OpenAILLM: JSON parse error: %s\n", err.c_str());
        Serial.println("OpenAILLM: raw body:");
        Serial.println(body);
        return String();
    }

    // Navigate to choices[0].message.content
    const char *content = doc["choices"][0]["message"]["content"];
    if (!content)
    {
        Serial.println("OpenAILLM: no content in response");
        Serial.println(body);
        return String();
    }

    String result(content);
    Serial.printf("OpenAILLM: reply length = %d\n", result.length());

    return result;
}
