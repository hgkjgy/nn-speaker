#include <Arduino.h>
#include "Application.h"
#include "state_machine/DetectWakeWordState.h"
#include "state_machine/RecogniseCommandState.h"
#include "IndicatorLight.h"
#include "Speaker.h"
#include "IntentProcessor.h"

Application::Application(
    I2SSampler *sample_provider,
    IntentProcessor *intent_processor,
    Speaker *speaker,
    IndicatorLight *indicator_light)
{
    // detect wake word state - waits for the wake word to be detected
    m_detect_wake_word_state = new DetectWakeWordState(sample_provider);

    // command recogniser - streams audio to the server for recognition
    m_recognise_command_state = new RecogniseCommandState(
        sample_provider,
        indicator_light,
        speaker,
        intent_processor);

    // start off in the detecting wake word state
    m_current_state = m_detect_wake_word_state;
    m_current_state->enterState();

    m_speaker = speaker;
    m_indicator_light = indicator_light;

    // 等待 wake word：LED OFF
    if (m_indicator_light)
    {
        m_indicator_light->setState(OFF);
    }
}

// process the next batch of samples
void Application::run()
{
    bool state_done = m_current_state->run();

    if (state_done)
    {
        m_current_state->exitState();

        // switch to the next state - simple two-state machine
        if (m_current_state == m_detect_wake_word_state)
        {
            // 偵測到 wake word 當下立即亮燈
            if (m_indicator_light)
            {
                m_indicator_light->setState(ON);
            }

            m_current_state = m_recognise_command_state;
            m_speaker->playOK();
        }
        else
        {
            // 辨識流程結束（成功或失敗）回到等待狀態時關燈
            if (m_indicator_light)
            {
                m_indicator_light->setState(OFF);
            }

            m_current_state = m_detect_wake_word_state;
        }

        m_current_state->enterState();
    }

    vTaskDelay(10);
}