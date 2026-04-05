#ifndef _speaker_h_
#define _speaker_h_

class I2SOutput;
class WAVFileReader;
class OpenAITTS;

class Speaker
{
private:
    WAVFileReader *m_ok;
    WAVFileReader *m_cantdo;
    WAVFileReader *m_ready_ping;
    WAVFileReader *m_light_on;
    WAVFileReader *m_light_off;
    WAVFileReader *m_life;
    WAVFileReader *m_jokes[5];

    I2SOutput *m_i2s_output;
    OpenAITTS *m_tts;

public:
    Speaker(I2SOutput *i2s_output);
    ~Speaker();
    void playOK();
    void playReady();
    void playCantDo();
    void playLightOn();
    void playLightOff();
    void playRandomJoke();
    void playLife();

    /**
     * Call OpenAI TTS to synthesize the given text and play it through the speaker.
     * This is a blocking call – it will wait for the API response before returning.
     * @param text  UTF-8 text to speak.
     * @return true on success, false on failure.
     */
    bool playTTS(const char *text);

    /** Access the underlying OpenAITTS instance. */
    OpenAITTS *tts() { return m_tts; }
};

#endif
