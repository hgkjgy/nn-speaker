#ifndef __openai_tts_h__
#define __openai_tts_h__

#include <Arduino.h>

class TTSBufferSource;

/**
 * Minimal OpenAI TTS (text-to-speech) client for ESP32.
 *
 * Calls POST https://api.openai.com/v1/audio/speech with
 *   response_format = "pcm"  (24 kHz / 16-bit / mono / little-endian)
 * and stores the result in a TTSBufferSource (resampled to 16 kHz).
 */
class OpenAITTS
{
private:
    String m_api_key_storage;
    const char *m_api_key;
    const char *m_model;  // e.g. "tts-1"
    const char *m_voice;  // e.g. "alloy", "echo", "fable", "onyx", "nova", "shimmer"
    TTSBufferSource *m_buffer_source;

public:
    /**
     * @param api_key  Your OpenAI API key (Bearer token).
     * @param model    TTS model name, default "tts-1".
     * @param voice    Voice name, default "alloy".
     */
    OpenAITTS(const char *api_key,
              const char *model = "tts-1",
              const char *voice = "alloy");
    ~OpenAITTS();

    /**
     * Synthesise text and load it into the internal TTSBufferSource.
     *
     * @param text  The text to speak (UTF-8).
     * @return pointer to the TTSBufferSource on success, nullptr on failure.
     *         The caller must NOT delete the returned pointer; it is owned
     *         by this OpenAITTS instance and reused on the next call.
     */
    TTSBufferSource *synthesize(const char *text);

    /** Set or change the API key (stores an owned copy). */
    void setApiKey(const char *key) { m_api_key_storage = key; m_api_key = m_api_key_storage.c_str(); }

    /** Access the underlying buffer source (may be empty/finished). */
    TTSBufferSource *bufferSource() { return m_buffer_source; }
};

#endif
