# ESP32 Voice Assistant: Program Workflow

A push-to-talk voice assistant. An ESP32 records speech from an INMP441 microphone, sends it to Google Gemini, and prints the transcription and the answer to the Serial Monitor.

## Hardware

| Component | Pin | Role |
|---|---|---|
| INMP441 SCK / MAX98357A BCLK | GPIO 6 | I2S bit clock |
| INMP441 WS / MAX98357A LRC | GPIO 7 | I2S word select |
| INMP441 SD | GPIO 3 | Mic data into ESP32 |
| MAX98357A DIN | GPIO 10 | Speaker data out (defined, not yet used) |
| Push button | GPIO 4 and 3.3V | Active HIGH, `INPUT_PULLDOWN` |

## Audio Settings

- Sample rate: 16 kHz
- Maximum recording: 3 seconds (48,000 samples)
- Mic reads 32-bit samples, shifted right by 14 bits to 16-bit PCM

## Workflow

### 1. Startup (`setup()`)
1. Start Serial at 115200 baud.
2. Set GPIO 4 as `INPUT_PULLDOWN`, so the pin reads LOW until the button connects it to 3.3V.
3. Connect to Wi-Fi and print the IP address.
4. Print the "Ready" message.

### 2. Idle (`loop()`)
The loop polls GPIO 4 and does nothing until it reads HIGH (button pressed).

### 3. Recording (`recordAudio()`)
1. Install the I2S driver for the INMP441 (32-bit, 16 kHz, left channel).
2. While the button is held and fewer than 48,000 samples are stored:
   - Read a block of samples with `i2s_read()`.
   - Convert each sample from 32-bit to 16-bit.
   - Store it in `audioBuffer`.
3. On release (or at the 3-second limit), uninstall the I2S driver and print the sample count and duration.

### 4. Wait for Release
The program waits until the button is fully released, then applies a 50 ms debounce delay.

### 5. Sending to Gemini (`processAudioWithGemini()`)
1. If the clip is shorter than 0.5 seconds, print a warning and return.
2. Open a TLS connection to `generativelanguage.googleapis.com:443`.
3. Calculate the total request size (JSON prefix + base64 length + JSON suffix) for the `Content-Length` header.
4. Send the HTTP POST headers.
5. Stream the body:
   - **JSON prefix:** opens the `inlineData` block with MIME type `audio/wav`.
   - **Audio (`streamAudioBase64()`):** a 44-byte WAV header plus the first 700 bytes of PCM, then 768-byte PCM chunks. Each chunk is base64-encoded and written straight to the socket. Chunk sizes are multiples of 3 so the pieces join without padding errors, and no large RAM buffer is needed.
   - **JSON suffix:** contains the prompt asking Gemini to return `user_text` and `response` as JSON.

### 6. Receiving the Response
1. Wait up to 25 seconds for data.
2. Read the status line.
3. Read the headers and detect `Transfer-Encoding: chunked`.
4. Read the body, decoding chunks if needed.
5. Close the connection.

### 7. Parsing and Output
1. If the status is not 200, print the error and body.
2. Otherwise, parse the outer JSON and extract `candidates[0].content.parts[0].text`.
3. Strip any ```` json ```` fences.
4. Parse that text as JSON:
   - **Success:** print **You Said** (`user_text`) and **Gemini Answer** (`response`).
   - **Failure:** print the raw text as a fallback.

### 8. Back to Idle
After a 300 ms delay, the loop waits for the next button press.

## Flow Diagram

```
Boot → Wi-Fi connect → Idle (poll button)
          ↑                    │ button HIGH
          │                    ▼
          │            Record mic → buffer (max 3 s)
          │                    │ button released
          │                    ▼
          │            TLS connect to Gemini
          │                    ▼
          │            Stream WAV as base64 JSON
          │                    ▼
          │            Read + unchunk response
          │                    ▼
          │            Parse JSON → print to Serial
          └────────────────────┘
```

## Current Limitations

- Output goes only to the Serial Monitor. The MAX98357A amplifier is wired in the pin list (`I2S_SD_OUT`), but the code never plays the answer aloud.
- Recording is capped at 3 seconds.
- `client.setInsecure()` skips TLS certificate verification.