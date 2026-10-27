#include <Arduino.h>
#include <driver/i2s.h>

// ---------- GPIO PIN DEFINITIONS ----------
#define I2S_SCK      6   // INMP441 SCK <-> MAX98357A BCLK
#define I2S_WS       7   // INMP441 WS  <-> MAX98357A LRC
#define I2S_SD_IN    3   // INMP441 SD  -> ESP32
#define I2S_SD_OUT   10  // ESP32 -> MAX98357A DIN

#define BUTTON_PIN   4   // Push button -> GPIO 4 and 3.3V

#define SAMPLE_RATE     16000
#define MAX_RECORD_SEC  3
#define MAX_SAMPLES     (SAMPLE_RATE * MAX_RECORD_SEC)

static int16_t audioBuffer[MAX_SAMPLES];
static int recordedSamples = 0;

// Function Declarations
void configI2SMic();
void configI2SAmp();
void recordAudio();
void playAudio();

// ============================================================
// CONFIGURE I2S FOR INMP441 MICROPHONE
// ============================================================
void configI2SMic() {
  i2s_driver_uninstall(I2S_NUM_0);

  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD_IN
  };

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

// ============================================================
// CONFIGURE I2S FOR MAX98357A
// ============================================================
void configI2SAmp() {
  i2s_driver_uninstall(I2S_NUM_0);

  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_SD_OUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

// ============================================================
// RECORD AUDIO WHILE BUTTON IS PRESSED
// ============================================================
void recordAudio() {
  configI2SMic();

  delay(50);

  Serial.println("Recording... Speak now!");

  int32_t raw[256];
  size_t bytesRead;

  recordedSamples = 0;

  // BUTTON PRESSED = HIGH
  while (digitalRead(BUTTON_PIN) == HIGH && recordedSamples < MAX_SAMPLES) {
    i2s_read(
      I2S_NUM_0,
      raw,
      sizeof(raw),
      &bytesRead,
      portMAX_DELAY
    );

    int n = bytesRead / sizeof(int32_t);

    for (int i = 0; i < n && recordedSamples < MAX_SAMPLES; i++) {
      // Convert INMP441 32-bit data to 16-bit
      audioBuffer[recordedSamples++] = (int16_t)(raw[i] >> 14);
    }
  }

  i2s_driver_uninstall(I2S_NUM_0);

  Serial.print("Recorded samples: ");
  Serial.println(recordedSamples);
}

// ============================================================
// PLAY AUDIO THROUGH MAX98357A + SPEAKER
// ============================================================
void playAudio() {
  if (recordedSamples == 0)
    return;

  configI2SAmp();

  delay(50);

  Serial.println("Playing through speaker...");

  // Software amplification
  const float GAIN = 4.0;

  int16_t outChunk[256];
  size_t bytesWritten;

  int idx = 0;

  while (idx < recordedSamples) {
    int chunk = min((int)256, recordedSamples - idx);

    for (int i = 0; i < chunk; i++) {
      int32_t amplified = (int32_t)(audioBuffer[idx + i] * GAIN);

      // Prevent clipping
      if (amplified > 32767)
        amplified = 32767;
      if (amplified < -32768)
        amplified = -32768;

      outChunk[i] = (int16_t)amplified;
    }

    i2s_write(
      I2S_NUM_0,
      outChunk,
      chunk * sizeof(int16_t),
      &bytesWritten,
      portMAX_DELAY
    );

    idx += chunk;
  }

  i2s_driver_uninstall(I2S_NUM_0);

  Serial.println("Playback finished.");
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Internal pull-down resistor
  // Button connects GPIO 4 to 3.3V
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);

  Serial.println("Mic + Amplifier system ready.");
  Serial.println("Press and hold the button and speak.");
  Serial.println("Release the button to hear playback.");
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  // BUTTON PRESSED = HIGH
  if (digitalRead(BUTTON_PIN) == HIGH) {
    // Record while button is held
    recordAudio();

    // Wait until button is released
    while (digitalRead(BUTTON_PIN) == HIGH) {
      delay(10);
    }

    delay(50);

    // Play recorded voice
    playAudio();

    // Debounce
    delay(300);
  }
}