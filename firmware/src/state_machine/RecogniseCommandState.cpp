#include <Arduino.h>
#include <ArduinoJson.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "I2SSampler.h"
#include "RingBuffer.h"
#include "RecogniseCommandState.h"
#include "IndicatorLight.h"
#include "Speaker.h"
#include "IntentProcessor.h"
#include "WitAiChunkedUploader.h"
#include "../config.h"
#include <string.h>
#include <string>

#define WINDOW_SIZE 320
#define STEP_SIZE 160
#define POOLING_SIZE 6
#define AUDIO_LENGTH 16000

RecogniseCommandState::RecogniseCommandState(
    I2SSampler *sample_provider,
    IndicatorLight *indicator_light,
    Speaker *speaker,
    IntentProcessor *intent_processor)
{
    // save the sample provider for use later
    m_sample_provider = sample_provider;
    m_indicator_light = indicator_light;
    m_speaker = speaker;
    m_intent_processor = intent_processor;
    m_speech_recogniser = NULL;
}
void RecogniseCommandState::enterState()
{
    if (m_indicator_light)
    {
        m_indicator_light->setState(ON);
    }

    Serial.println("Recording...");
    m_speaker->playReady();

    m_start_time = millis();
    m_elapsed_time = 0;
    m_last_audio_position = -1;

    uint32_t free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram before connection %d\n", free_ram);

    m_speech_recogniser = new WitAiChunkedUploader(COMMAND_RECOGNITION_ACCESS_KEY);

    if (!m_speech_recogniser || !m_speech_recogniser->connected())
    {
        Serial.println("Recording/Upload failed.");
    }
    else
    {
        Serial.println("Uploading audio...");
    }

    free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram after connection %d\n", free_ram);
}

bool RecogniseCommandState::run()
{
    if (!m_speech_recogniser || !m_speech_recogniser->connected())
    {
        // no http client - something went wrong somewhere move to the next state
        Serial.println("Error - Attempt to run with no http client");

        // 辨識失敗 / 結束：LED OFF
        if (m_indicator_light)
        {
            m_indicator_light->setState(OFF);
        }

        return true;
    }

    if (m_last_audio_position == -1)
    {
        // set to 1 second in the past to allow for slow connection time
        m_last_audio_position = m_sample_provider->getCurrentWritePosition() - 16000;
    }

    // how many samples have been captured since we last ran
    int audio_position = m_sample_provider->getCurrentWritePosition();

    // work out how many samples there are taking into account wrap around
    int sample_count = (audio_position - m_last_audio_position + m_sample_provider->getRingBufferSize()) % m_sample_provider->getRingBufferSize();

    if (sample_count > 0)
    {
        // send the samples to the server
        m_speech_recogniser->startChunk(sample_count * sizeof(int16_t));
        RingBufferAccessor *reader = m_sample_provider->getRingBufferReader();
        reader->setIndex(m_last_audio_position);

        // send the samples up in chunks
        int16_t sample_buffer[500];
        while (sample_count > 0)
        {
            int chunk_count = std::min(sample_count, 500);
            for (int i = 0; i < chunk_count; i++)
            {
                sample_buffer[i] = reader->getCurrentSample();
                reader->moveToNextSample();
            }
            m_speech_recogniser->sendChunkData((const uint8_t *)sample_buffer, chunk_count * 2);
            sample_count -= chunk_count;
        }

        m_last_audio_position = reader->getIndex();
        m_speech_recogniser->finishChunk();
        delete reader;

        // has 3 seconds passed?
        unsigned long current_time = millis();
        m_elapsed_time += current_time - m_start_time;
        m_start_time = current_time;

        if (m_elapsed_time > 3000)
        {
            // 辨識中 / 傳送完成等待結果：仍保持亮燈
            if (m_indicator_light)
            {
                m_indicator_light->setState(ON);
            }

            Serial.println("3 seconds has elapsed - finishing recognition request");

            Intent intent = m_speech_recogniser->getResults();
            std::string llmResponseText;
            IntentResult intentResult = m_intent_processor->processIntent(intent, llmResponseText);

            switch (intentResult)
            {
            case SUCCESS:
                if (!llmResponseText.empty())
                {
                    Serial.printf("RecogniseCommandState: playing TTS for response (%d chars)\n",
                                  (int)llmResponseText.size());
                    if (!m_speaker->playTTS(llmResponseText.c_str()))
                    {
                        m_speaker->playOK();
                    }
                }
                else
                {
                    m_speaker->playOK();
                }
                break;

            case FAILED:
                m_speaker->playCantDo();
                break;

            case SILENT_SUCCESS:
                // nothing to do
                break;
            }

            // 辨識結束（成功或失敗）：LED OFF
            if (m_indicator_light)
            {
                m_indicator_light->setState(OFF);
            }

            return true;
        }
    }

    // still work to do, stay in this state
    return false;
}

void RecogniseCommandState::exitState()
{
    if (m_indicator_light)
    {
        m_indicator_light->setState(OFF);
    }

    Serial.println("Recognition finished.");

    delete m_speech_recogniser;
    m_speech_recogniser = NULL;

    uint32_t free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram after request %d\n", free_ram);
}