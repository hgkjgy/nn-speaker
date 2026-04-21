#include <Arduino.h>
#include "I2SSampler.h"
#include "AudioProcessor.h"
#include "NeuralNetwork.h"
#include "RingBuffer.h"
#include "DetectWakeWordState.h"

#define WINDOW_SIZE 320
#define STEP_SIZE 160
#define POOLING_SIZE 6
#define AUDIO_LENGTH 16000

DetectWakeWordState::DetectWakeWordState(I2SSampler *sample_provider)
{
    m_sample_provider = sample_provider;
    m_average_detect_time = 0;
    m_number_of_runs = 0;
    m_nn = NULL;
    m_audio_processor = NULL;
}

void DetectWakeWordState::enterState()
{
    m_audio_processor = new AudioProcessor(AUDIO_LENGTH, WINDOW_SIZE, STEP_SIZE, POOLING_SIZE);
    Serial.println("Created audio processor");

    m_nn = new NeuralNetwork();
    Serial.println("Created Neural Net");

    m_number_of_detections = 0;
}

bool DetectWakeWordState::run()
{
    if (!m_nn || !m_audio_processor)
    {
        return false;
    }

    long start = millis();

    RingBufferAccessor *reader = m_sample_provider->getRingBufferReader();
    reader->rewind(16000);

    float *input_buffer = m_nn->getInputBuffer();
    m_audio_processor->get_spectrogram(reader, input_buffer);

    delete reader;

    float output = m_nn->predict();

    long end = millis();

    m_average_detect_time = (end - start) * 0.1f + m_average_detect_time * 0.9f;
    m_number_of_runs++;

    if (m_number_of_runs == 100)
    {
        m_number_of_runs = 0;
        Serial.printf("Average detection time %.fms\n", m_average_detect_time);
    }

    // Serial.printf("Wake score: %.3f\n", output);

    if (output > 0.55f)
    {
        m_number_of_detections++;


        if (m_current_state == m_detect_wake_word_state)
        {
            if (m_indicator_light)
            {
                for (int i = 0; i < 3; i++)
                {
                    m_indicator_light->setState(ON);
                    delay(100);
                    m_indicator_light->setState(OFF);
                    delay(100);
                }
                m_indicator_light->setState(ON);
            }

            m_current_state = m_recognise_command_state;
            m_speaker->playOK();
        }
    }
    else
    {
        m_number_of_detections = 0;
    }

    return false;
}

void DetectWakeWordState::exitState()
{
    uint32_t free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram before DetectWakeWord cleanup %d\n", free_ram);

    delete m_audio_processor;
    m_audio_processor = NULL;

    delete m_nn;
    m_nn = NULL;

    free_ram = esp_get_free_heap_size();
    Serial.printf("Free ram after DetectWakeWord cleanup %d\n", free_ram);
}