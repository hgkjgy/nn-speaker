#include "OpenAITTS.h"
#include "TTSBufferSource.h"

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>

// Maximum PCM response we are willing to buffer (bytes).
// 24 kHz × 2 bytes × 30 s ≈ 1.44 MB – fits comfortably in 4 MB PSRAM.
#define MAX_PCM_BYTES (24000 * 2 * 30)

// Read timeout while waiting for server response (milliseconds).
#define READ_TIMEOUT_MS 30000

// --------------------------------------------------------------------
// Helpers
// --------------------------------------------------------------------

/**
 * Build a minimal JSON body for the OpenAI TTS request.
 */
static String buildRequestBody(const char *model, const char *voice, const char *text)
{
    // Escape double-quotes and backslashes in `text` for JSON safety.
    String escaped;
    escaped.reserve(strlen(text) + 32);
    for (const char *p = text; *p; ++p)
    {
        if (*p == '"')
            escaped += "\\\"";
        else if (*p == '\\')
            escaped += "\\\\";
        else if (*p == '\n')
            escaped += "\\n";
        else if (*p == '\r')
            escaped += "\\r";
        else
            escaped += *p;
    }

    String body;
    body.reserve(128 + escaped.length());
    body += "{\"model\":\"";
    body += model;
    body += "\",\"voice\":\"";
    body += voice;
    body += "\",\"input\":\"";
    body += escaped;
    body += "\",\"response_format\":\"pcm\"}";
    return body;
}

/**
 * Wait until data is available on the client or timeout.
 * @return true if data became available, false on timeout.
 */
static bool waitForData(WiFiClientSecure *client, unsigned long timeout_ms)
{
    unsigned long start = millis();
    while (client->connected() && !client->available())
    {
        if (millis() - start > timeout_ms)
        {
            return false;
        }
        delay(10);
    }
    return client->available() > 0;
}

/**
 * Read one HTTP header line (terminated by \n).
 * Returns empty string on timeout.
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
            {
                break;
            }
            if (c != '\r')
            {
                line += c;
            }
        }
        else
        {
            if (millis() - start > timeout_ms)
            {
                break;
            }
            delay(1);
        }
    }
    return line;
}

/**
 * Read exactly `count` bytes from the client into buf.
 * @return number of bytes actually read.
 */
static int readExact(WiFiClientSecure *client, uint8_t *buf, int count, unsigned long timeout_ms)
{
    int total = 0;
    unsigned long start = millis();
    while (total < count && client->connected())
    {
        if (client->available())
        {
            int rd = client->read(buf + total, count - total);
            if (rd > 0)
            {
                total += rd;
                start = millis(); // reset timeout on progress
            }
        }
        else
        {
            if (millis() - start > timeout_ms)
                break;
            delay(1);
        }
    }
    return total;
}

/**
 * Read a chunked HTTP body into buf (up to buf_capacity).
 * Handles the "Transfer-Encoding: chunked" framing.
 * @return total bytes of decoded body data.
 */
static int readChunkedBody(WiFiClientSecure *client, uint8_t *buf, int buf_capacity, unsigned long timeout_ms)
{
    int total = 0;

    while (true)
    {
        // Read chunk size line (hex number followed by \r\n)
        String sizeLine = readHeaderLine(client, timeout_ms);
        sizeLine.trim();
        if (sizeLine.length() == 0)
        {
            // Could be a blank line before chunk size; try once more
            sizeLine = readHeaderLine(client, timeout_ms);
            sizeLine.trim();
        }

        // Parse hex size
        int chunkSize = (int)strtol(sizeLine.c_str(), NULL, 16);
        if (chunkSize <= 0)
        {
            // Last chunk (0) or parse error
            break;
        }

        // Read chunk data
        int toRead = chunkSize;
        if (total + toRead > buf_capacity)
        {
            toRead = buf_capacity - total;
        }

        int rd = readExact(client, buf + total, toRead, timeout_ms);
        total += rd;

        // If chunk was larger than remaining capacity, drain the rest
        if (toRead < chunkSize)
        {
            int discard = chunkSize - toRead;
            uint8_t tmp[256];
            while (discard > 0)
            {
                int d = readExact(client, tmp, (discard > 256 ? 256 : discard), timeout_ms);
                if (d <= 0) break;
                discard -= d;
            }
        }

        // Read trailing \r\n after chunk data
        readHeaderLine(client, timeout_ms);

        if (total >= buf_capacity)
        {
            Serial.println("OpenAITTS: buffer full, stopping read");
            break;
        }
    }

    return total;
}

// --------------------------------------------------------------------
// OpenAITTS
// --------------------------------------------------------------------

OpenAITTS::OpenAITTS(const char *api_key, const char *model, const char *voice)
    : m_api_key(api_key), m_model(model), m_voice(voice)
{
    m_buffer_source = new TTSBufferSource();
}

OpenAITTS::~OpenAITTS()
{
    delete m_buffer_source;
}

TTSBufferSource *OpenAITTS::synthesize(const char *text)
{
    if (!text || strlen(text) == 0)
    {
        Serial.println("OpenAITTS: empty text, nothing to synthesize");
        return nullptr;
    }

    // --- Build request ------------------------------------------------
    String body = buildRequestBody(m_model, m_voice, text);
    int content_length = body.length();

    Serial.printf("OpenAITTS: synthesizing %d bytes of text …\n", (int)strlen(text));
    Serial.printf("OpenAITTS: request body: %s\n", body.c_str());

    // --- TLS connection -----------------------------------------------
    WiFiClientSecure *client = new WiFiClientSecure();
    if (!client)
    {
        Serial.println("OpenAITTS: failed to allocate WiFiClientSecure");
        return nullptr;
    }
    client->setInsecure(); // skip certificate verification (simple path)
    client->setTimeout(30);

    if (!client->connect("api.openai.com", 443))
    {
        Serial.println("OpenAITTS: TLS connect failed");
        delete client;
        return nullptr;
    }

    // --- Send HTTP request --------------------------------------------
    client->printf("POST /v1/audio/speech HTTP/1.1\r\n");
    client->printf("Host: api.openai.com\r\n");
    client->printf("Authorization: Bearer %s\r\n", m_api_key);
    client->printf("Content-Type: application/json\r\n");
    client->printf("Content-Length: %d\r\n", content_length);
    client->printf("Connection: close\r\n");
    client->printf("\r\n");
    client->print(body);

    // --- Wait for server to start responding --------------------------
    Serial.println("OpenAITTS: waiting for response …");
    if (!waitForData(client, READ_TIMEOUT_MS))
    {
        Serial.println("OpenAITTS: timeout waiting for response");
        delete client;
        return nullptr;
    }

    // --- Read response headers ----------------------------------------
    int http_status = 0;
    int resp_content_length = -1;
    bool is_chunked = false;

    while (true)
    {
        String line = readHeaderLine(client, READ_TIMEOUT_MS);
        if (line.length() == 0)
        {
            break; // blank line = end of headers
        }

        Serial.printf("  header: %s\n", line.c_str());

        if (line.startsWith("HTTP/"))
        {
            int sp1 = line.indexOf(' ');
            if (sp1 > 0)
            {
                http_status = line.substring(sp1 + 1).toInt();
            }
        }
        else if (line.startsWith("Content-Length:") || line.startsWith("content-length:"))
        {
            resp_content_length = line.substring(line.indexOf(':') + 1).toInt();
        }
        else if (line.indexOf("chunked") >= 0)
        {
            is_chunked = true;
        }
    }

    Serial.printf("OpenAITTS: HTTP %d, Content-Length %d, chunked=%d\n",
                  http_status, resp_content_length, is_chunked);

    if (http_status != 200)
    {
        // Drain and print error body for debugging
        String err;
        unsigned long t = millis();
        while ((client->connected() || client->available()) && (millis() - t < 5000))
        {
            while (client->available())
            {
                err += (char)client->read();
                t = millis();
            }
            delay(1);
        }
        Serial.printf("OpenAITTS: error body: %s\n", err.c_str());
        delete client;
        return nullptr;
    }

    // --- Allocate receive buffer in PSRAM -----------------------------
    int buf_capacity = (resp_content_length > 0) ? resp_content_length : (256 * 1024);
    if (buf_capacity > MAX_PCM_BYTES)
    {
        buf_capacity = MAX_PCM_BYTES;
    }

    uint8_t *raw_buf = (uint8_t *)heap_caps_malloc(buf_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!raw_buf)
    {
        raw_buf = (uint8_t *)malloc(buf_capacity);
    }
    if (!raw_buf)
    {
        Serial.println("OpenAITTS: failed to allocate receive buffer");
        delete client;
        return nullptr;
    }

    // --- Read body data -----------------------------------------------
    int total_read = 0;

    if (is_chunked)
    {
        total_read = readChunkedBody(client, raw_buf, buf_capacity, READ_TIMEOUT_MS);
    }
    else
    {
        // Non-chunked: read until content-length or connection close
        int to_read = (resp_content_length > 0) ? resp_content_length : buf_capacity;
        if (to_read > buf_capacity)
            to_read = buf_capacity;

        unsigned long last_data = millis();
        while (total_read < to_read && (client->connected() || client->available()))
        {
            if (client->available())
            {
                int rd = client->read(raw_buf + total_read, to_read - total_read);
                if (rd > 0)
                {
                    total_read += rd;
                    last_data = millis();
                }
            }
            else
            {
                if (millis() - last_data > 5000)
                {
                    Serial.println("OpenAITTS: read timeout");
                    break;
                }
                delay(1);
            }

            if (resp_content_length > 0 && total_read >= resp_content_length)
                break;
        }
    }

    delete client;

    Serial.printf("OpenAITTS: received %d bytes of PCM data\n", total_read);

    if (total_read < 2)
    {
        Serial.println("OpenAITTS: no usable PCM data received");
        heap_caps_free(raw_buf);
        return nullptr;
    }

    // --- Resample 24 kHz → 16 kHz and store in TTSBufferSource --------
    bool ok = m_buffer_source->loadFromPCM24k(raw_buf, total_read);
    heap_caps_free(raw_buf);

    if (!ok)
    {
        Serial.println("OpenAITTS: failed to load PCM into buffer");
        return nullptr;
    }

    return m_buffer_source;
}
