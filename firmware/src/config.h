// WiFi credentials
#define WIFI_SSID "hsc48"
#define WIFI_PSWD "04124572"

// are you using an I2S microphone - comment this out if you want to use an analog mic and ADC input
#define USE_I2S_MIC_INPUT

// I2S Microphone Settings

// Which channel is the I2S microphone on? I2S_CHANNEL_FMT_ONLY_LEFT or I2S_CHANNEL_FMT_ONLY_RIGHT
#define I2S_MIC_CHANNEL I2S_CHANNEL_FMT_ONLY_LEFT
// #define I2S_MIC_CHANNEL I2S_CHANNEL_FMT_ONLY_RIGHT
#define I2S_MIC_SERIAL_CLOCK GPIO_NUM_33
#define I2S_MIC_LEFT_RIGHT_CLOCK GPIO_NUM_26
#define I2S_MIC_SERIAL_DATA GPIO_NUM_25

// Analog Microphone Settings - ADC1_CHANNEL_7 is GPIO35
#define ADC_MIC_CHANNEL ADC1_CHANNEL_7

// speaker settings
#define I2S_SPEAKER_SERIAL_CLOCK GPIO_NUM_14
#define I2S_SPEAKER_LEFT_RIGHT_CLOCK GPIO_NUM_12
#define I2S_SPEAKER_SERIAL_DATA GPIO_NUM_27

// command recognition settings
//#define COMMAND_RECOGNITION_ACCESS_KEY "P5QMUSMFV6IRRSTABXFQ7UIXPFRMC4L5"
#define COMMAND_RECOGNITION_ACCESS_KEY "N6HHFVXOVIZFCLAPEMC7LPDZ4KXONLSJ"

// OpenAI LLM settings
#define OPENAI_LLM_MODEL "gpt-4o-mini"

// OpenAI TTS settings
#define OPENAI_API_KEY "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"
#define OPENAI_TTS_MODEL "tts-1"
#define OPENAI_TTS_VOICE "alloy"   // alloy, echo, fable, onyx, nova, shimmer
