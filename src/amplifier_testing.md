# ESP32 Mic + Speaker Loopback: Program Workflow

A push-to-talk loopback. An ESP32 records speech from an INMP441 microphone while a button is held, then plays the recording back through a MAX98357A amplifier and speaker when the button is released. There is no Wi-Fi or cloud involved.

## Hardware

| Component | Pin | Role |
|---|---|---|
| INMP441 SCK / MAX98357A BCLK | GPIO 6 | I2S bit clock (shared) |
| INMP441 WS / MAX98357A LRC | GPIO 7 | I2S word select (shared) |
| INMP441 SD | GPIO 3 | Mic data into ESP32 |
| MAX98357A DIN | GPIO 10 | Speaker data out of ESP32 |
| Push button | GPIO 4 and 3.3V | Active HIGH, `INPUT_PULLDOWN` |

## Audio Settings

- Sample rate: 16 kHz
- Maximum recording: 3 seconds (48,000 samples, about 96 KB in RAM)
- Mic input: 32-bit samples, shifted right by 14 bits to 16-bit PCM
- Speaker output: 16-bit samples with a 4x software gain

## Workflow

### 1. Startup (`setup()`)
1. Start Serial at 115200 baud.
2. Set GPIO 4 as `INPUT_PULLDOWN`, so the pin reads LOW until the button connects it to 3.3V.
3. Print the ready and usage instructions.

### 2. Idle (`loop()`)
The loop polls GPIO 4 and does nothing until it reads HIGH (button pressed).

### 3. Recording (`recordAudio()`)
1. Switch I2S port 0 to **RX mode** for the INMP441 (`configI2SMic()`), which uninstalls any previous driver first.
2. Wait 50 ms for the mic to settle.
3. While the button is held and fewer than 48,000 samples are stored:
   - Read a block of 32-bit samples with `i2s_read()`.
   - Shift each sample right by 14 bits to get 16-bit audio.
   - Store it in `audioBuffer`.
4. When the button is released (or the 3-second limit is reached), uninstall the I2S driver and print the sample count.

### 4. Wait for Release
The program waits until the button is fully released, then applies a 50 ms debounce delay.

### 5. Playback (`playAudio()`)
1. If nothing was recorded, return.
2. Switch I2S port 0 to **TX mode** for the MAX98357A (`configI2SAmp()`), with `tx_desc_auto_clear` enabled to avoid noise when the buffer runs dry.
3. Wait 50 ms.
4. Process the buffer in 256-sample chunks:
   - Multiply each sample by the 4x gain.
   - Clamp the result to the 16-bit range (-32768 to 32767) to prevent wrap-around distortion.
   - Send the chunk with `i2s_write()`.
5. Uninstall the I2S driver and print "Playback finished."

### 6. Back to Idle
After a 300 ms delay, the loop waits for the next button press.

## Flow Diagram

```
Boot → Idle (poll button)
          ↑                │ button HIGH
          │                ▼
          │        I2S RX → record to buffer (max 3 s)
          │                │ button released
          │                ▼
          │        I2S TX → apply gain + clip → speaker
          └────────────────┘
```

## Key Design Points

- **One I2S port, two roles.** The mic and amplifier share the clock pins (GPIO 6 and 7), so the code uninstalls and reinstalls the driver to switch between RX and TX. Mic and speaker are never active at the same time, which also avoids acoustic feedback.
- **Software gain with clipping.** The 4x boost makes quiet playback audible, and the clamp stops loud samples from overflowing.
- **Simple RAM buffer.** Audio is stored in one static array, which is fine for 3 seconds but limits the recording length.

## Current Limitations

- Recording is capped at 3 seconds.
- Nothing is sent to the cloud, so there is no transcription or AI response. This is the audio-hardware test stage of the Gemini assistant.
- Fixed 4x gain can distort loud speech or amplify noise.
- Button polling runs in the main loop with no hardware debounce beyond the fixed delays.