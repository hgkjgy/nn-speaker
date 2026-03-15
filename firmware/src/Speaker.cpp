#include "Speaker.h"
#include "I2SOutput.h"
#include "WAVFileReader.h"
#include "OpenAITTS.h"
#include "TTSBufferSource.h"
#include "config.h"

Speaker::Speaker(I2SOutput *i2s_output)
{
    m_i2s_output = i2s_output;
    m_ok = new WAVFileReader("/ok.wav");
    m_ready_ping = new WAVFileReader("/ready_ping.wav");
    m_cantdo = new WAVFileReader("/cantdo.wav");
    m_light_on = new WAVFileReader("/light_on.wav");
    m_light_off = new WAVFileReader("/light_off.wav");
    m_life = new WAVFileReader("/life.wav");
    m_jokes[0] = new WAVFileReader("/joke0.wav");
    m_jokes[1] = new WAVFileReader("/joke1.wav");
    m_jokes[2] = new WAVFileReader("/joke2.wav");
    m_jokes[3] = new WAVFileReader("/joke3.wav");
    m_jokes[4] = new WAVFileReader("/joke4.wav");
    m_tts = new OpenAITTS(OPENAI_API_KEY, OPENAI_TTS_MODEL, OPENAI_TTS_VOICE);
}

Speaker::~Speaker()
{
    delete m_ok;
    delete m_ready_ping;
    delete m_cantdo;
    delete m_light_on;
    delete m_light_off;
    delete m_life;
    delete m_jokes[0];
    delete m_jokes[1];
    delete m_jokes[2];
    delete m_jokes[3];
    delete m_jokes[4];
    delete m_tts;
}

void Speaker::playOK()
{
    m_ok->reset();
    m_i2s_output->setSampleGenerator(m_ok);
}

void Speaker::playReady()
{
    m_ready_ping->reset();
    m_i2s_output->setSampleGenerator(m_ready_ping);
}

void Speaker::playCantDo()
{
    m_cantdo->reset();
    m_i2s_output->setSampleGenerator(m_cantdo);
}

void Speaker::playLightOn()
{
    m_light_on->reset();
    m_i2s_output->setSampleGenerator(m_light_on);
}

void Speaker::playLightOff()
{
    m_light_off->reset();
    m_i2s_output->setSampleGenerator(m_light_off);
}

void Speaker::playRandomJoke()
{
    int joke = random(5);
    m_i2s_output->setSampleGenerator(m_jokes[joke]);
}

void Speaker::playLife()
{
    m_life->reset();
    m_i2s_output->setSampleGenerator(m_life);
}

bool Speaker::playTTS(const char *text)
{
    TTSBufferSource *src = m_tts->synthesize(text);
    if (!src)
    {
        Serial.println("Speaker::playTTS failed – falling back to cantdo");
        playCantDo();
        return false;
    }
    src->reset();
    m_i2s_output->setSampleGenerator(src);
    return true;
}
