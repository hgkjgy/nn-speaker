#ifndef _application_h_
#define _application_h_

class I2SSampler;
class I2SOutput;
class State;
class IndicatorLight;
class Speaker;
class IntentProcessor;

class Application
{
private:
    State *m_detect_wake_word_state;
    State *m_recognise_command_state;
    State *m_current_state;
    Speaker *m_speaker;
    IndicatorLight *m_indicator_light;

public:
    Application(
        I2SSampler *sample_provider,
        IntentProcessor *intent_processor,
        Speaker *speaker,
        IndicatorLight *indicator_light);

    ~Application();
    void run();
};

#endif