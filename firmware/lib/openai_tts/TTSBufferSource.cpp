#include "TTSBufferSource.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

TTSBufferSource::TTSBufferSource()
    : m_buffer(nullptr), m_num_samples(0), m_read_pos(0)
{
}

TTSBufferSource::~TTSBufferSource()
{
    clear();
}

/**
 * Resample 24 kHz mono 16-bit PCM → 16 kHz mono 16-bit PCM using linear
 * interpolation and store in PSRAM.
 *
 * Ratio: 16000 / 24000 = 2 / 3.
 * For every output sample index 'o', the fractional input position is
 *   i_f = o * 3.0 / 2.0
 * We linearly interpolate between floor(i_f) and ceil(i_f).
 */
bool TTSBufferSource::loadFromPCM24k(const uint8_t *pcm_data, int pcm_bytes)
{
    clear();

    if (!pcm_data || pcm_bytes < 2)
    {
        return false;
    }

    const int src_samples = pcm_bytes / sizeof(int16_t);
    // output length: floor(src_samples * 2 / 3)
    const int dst_samples = (src_samples * 2) / 3;

    if (dst_samples == 0)
    {
        return false;
    }

    // Allocate in PSRAM if available, otherwise fall back to normal heap.
    m_buffer = (int16_t *)heap_caps_malloc(dst_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m_buffer)
    {
        m_buffer = (int16_t *)malloc(dst_samples * sizeof(int16_t));
    }
    if (!m_buffer)
    {
        Serial.println("TTSBufferSource: failed to allocate buffer");
        return false;
    }

    const int16_t *src = (const int16_t *)pcm_data;

    // Resample using fixed-point arithmetic (avoid float on each sample).
    // i_f = o * 3 / 2  →  use numerator = o*3, denominator = 2.
    for (int o = 0; o < dst_samples; o++)
    {
        int num = o * 3; // numerator of fractional input index * 2
        int idx = num / 2;
        int frac = num % 2; // 0 or 1  (represents 0.0 or 0.5)

        if (idx >= src_samples - 1)
        {
            m_buffer[o] = src[src_samples - 1];
        }
        else if (frac == 0)
        {
            m_buffer[o] = src[idx];
        }
        else
        {
            // Linear interpolation at 0.5
            m_buffer[o] = (int16_t)(((int32_t)src[idx] + (int32_t)src[idx + 1]) / 2);
        }
    }

    m_num_samples = dst_samples;
    m_read_pos = 0;

    Serial.printf("TTSBufferSource: loaded %d samples (%.1f s at 16 kHz)\n",
                  m_num_samples, m_num_samples / 16000.0f);
    return true;
}

int TTSBufferSource::getFrames(Frame_t *frames, int number_frames)
{
    int frames_written = 0;
    for (int i = 0; i < number_frames; i++)
    {
        if (m_read_pos >= m_num_samples)
        {
            return frames_written;
        }
        int16_t sample = m_buffer[m_read_pos++];
        frames[i].left = sample;
        frames[i].right = sample;
        frames_written++;
    }
    return frames_written;
}

bool TTSBufferSource::available()
{
    return m_buffer && m_read_pos < m_num_samples;
}

void TTSBufferSource::reset()
{
    m_read_pos = 0;
}

void TTSBufferSource::clear()
{
    if (m_buffer)
    {
        heap_caps_free(m_buffer);
        m_buffer = nullptr;
    }
    m_num_samples = 0;
    m_read_pos = 0;
}
