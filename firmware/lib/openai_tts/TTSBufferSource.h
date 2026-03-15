#ifndef __tts_buffer_source_h__
#define __tts_buffer_source_h__

#include "SampleSource.h"

/**
 * A SampleSource backed by a heap-allocated (PSRAM) buffer of 16-bit mono PCM
 * already resampled to the target sample rate (16 kHz).
 *
 * Call loadFromPCM24k() to feed in raw 24 kHz data from the OpenAI TTS API
 * (response_format = "pcm").  The loader resamples 24 kHz → 16 kHz using
 * linear interpolation and stores the result.
 */
class TTSBufferSource : public SampleSource
{
private:
    int16_t *m_buffer;     // mono 16-bit samples at 16 kHz  (heap / PSRAM)
    int      m_num_samples; // total samples stored
    int      m_read_pos;    // current playback position

public:
    TTSBufferSource();
    ~TTSBufferSource() override;

    /**
     * Accept raw 24 kHz / 16-bit / mono / little-endian PCM bytes
     * (the format returned by OpenAI TTS with response_format = "pcm").
     * Internally resamples to 16 kHz and stores in PSRAM.
     *
     * @return true on success, false if allocation fails.
     */
    bool loadFromPCM24k(const uint8_t *pcm_data, int pcm_bytes);

    // --- SampleSource interface ---
    int  getFrames(Frame_t *frames, int number_frames) override;
    bool available() override;

    /** Reset playback position to the beginning. */
    void reset();

    /** Release the internal buffer. */
    void clear();
};

#endif
