#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <mbedtls/base64.h>

// ---------- Credentials injected from .env via load_env.py ----------
#if !defined(WIFI_SSID) || !defined(WIFI_PASSWORD) || !defined(GEMINI_API_KEY)
#error "Missing WIFI_SSID / WIFI_PASSWORD / GEMINI_API_KEY. Check your .env file and extra_scripts in platformio.ini"
#endif

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

// Gemini Configuration
const char* gemini_host = "generativelanguage.googleapis.com";
const String gemini_api_key = GEMINI_API_KEY;

// ---------- GPIO PIN DEFINITIONS ----------
#define I2S_SCK      6   // INMP441 SCK <-> MAX98357A BCLK
#define I2S_WS       7   // INMP441 WS  <-> MAX98357A LRC
#define I2S_SD_IN    3   // INMP441 SD  -> ESP32
#define I2S_SD_OUT   10  // ESP32 -> MAX98357A DIN

#define BUTTON_PIN   4   // Push button -> GPIO 4 and 3.3V (Active HIGH)

#define SAMPLE_RATE     16000
#define MAX_RECORD_SEC  3
#define MAX_SAMPLES     (SAMPLE_RATE * MAX_RECORD_SEC)

static int16_t audioBuffer[MAX_SAMPLES];
static int recordedSamples = 0;
static bool i2sInstalled = false;

void connectToWiFi() {
    Serial.print("Connecting to Wi-Fi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWi-Fi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

// ============================================================
// CONFIGURE I2S FOR INMP441 MICROPHONE (32-BIT RX)
// ============================================================
void configI2SMic() {
    if (i2sInstalled) {
        i2s_driver_uninstall(I2S_NUM_0);
        i2sInstalled = false;
    }

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
    i2sInstalled = true;
}

// ============================================================
// HELPER: CREATE 44-BYTE WAV HEADER
// ============================================================
void createWavHeader(byte* header, int pcmDataLen) {
    int totalDataLen = pcmDataLen + 36;
    int bitRate = SAMPLE_RATE * 16 * 1 / 8; // 1 channel, 16-bit

    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    header[4] = (byte)(totalDataLen & 0xff);
    header[5] = (byte)((totalDataLen >> 8) & 0xff);
    header[6] = (byte)((totalDataLen >> 16) & 0xff);
    header[7] = (byte)((totalDataLen >> 24) & 0xff);
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;
    header[20] = 1; header[21] = 0;
    header[22] = 1; header[23] = 0;
    header[24] = (byte)(SAMPLE_RATE & 0xff);
    header[25] = (byte)((SAMPLE_RATE >> 8) & 0xff);
    header[26] = (byte)((SAMPLE_RATE >> 16) & 0xff);
    header[27] = (byte)((SAMPLE_RATE >> 24) & 0xff);
    header[28] = (byte)(bitRate & 0xff);
    header[29] = (byte)((bitRate >> 8) & 0xff);
    header[30] = (byte)((bitRate >> 16) & 0xff);
    header[31] = (byte)((bitRate >> 24) & 0xff);
    header[32] = 2; header[33] = 0;
    header[34] = 16; header[35] = 0;
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    header[40] = (byte)(pcmDataLen & 0xff);
    header[41] = (byte)((pcmDataLen >> 8) & 0xff);
    header[42] = (byte)((pcmDataLen >> 16) & 0xff);
    header[43] = (byte)((pcmDataLen >> 24) & 0xff);
}

// ============================================================
// RECORD AUDIO WHILE BUTTON IS PRESSED (HIGH)
// ============================================================
void recordAudio() {
    configI2SMic();
    delay(50);

    Serial.println("\n=========================================");
    Serial.print(" [RECORDING]");

    int32_t raw[256];
    size_t bytesRead;
    recordedSamples = 0;
    unsigned long lastPrintTime = 0;

    while (digitalRead(BUTTON_PIN) == HIGH && recordedSamples < MAX_SAMPLES) {
        i2s_read(I2S_NUM_0, raw, sizeof(raw), &bytesRead, portMAX_DELAY);
        int n = bytesRead / sizeof(int32_t);

        for (int i = 0; i < n && recordedSamples < MAX_SAMPLES; i++) {
            audioBuffer[recordedSamples++] = (int16_t)(raw[i] >> 14);
        }

        if (millis() - lastPrintTime > 300) {
            Serial.print(" 🎙️");
            lastPrintTime = millis();
        }
    }

    if (i2sInstalled) {
        i2s_driver_uninstall(I2S_NUM_0);
        i2sInstalled = false;
    }

    Serial.println("\n [STOPPED] Processing recorded audio...");
    Serial.printf(" Total samples: %d (%.2f seconds)\n",
                  recordedSamples, (float)recordedSamples / SAMPLE_RATE);
    Serial.println("=========================================\n");
}

// ============================================================
// STREAM BASE64 WAV DIRECTLY TO TLS SOCKET
// ============================================================
void streamAudioBase64(WiFiClientSecure &client, int totalSamples) {
    int pcmBytes = totalSamples * sizeof(int16_t);
    uint8_t rawChunk[768];
    char b64Chunk[1025];
    size_t outLen = 0;

    // Chunk 0: 44-byte WAV header + 700 bytes PCM (Total = 744 bytes, divisible by 3)
    createWavHeader(rawChunk, pcmBytes);
    int pcmOffset = 0;
    int firstPcmBytes = (pcmBytes < 700) ? pcmBytes : 700;
    memcpy(rawChunk + 44, (uint8_t*)audioBuffer, firstPcmBytes);
    pcmOffset += firstPcmBytes;

    mbedtls_base64_encode((unsigned char*)b64Chunk, sizeof(b64Chunk), &outLen, rawChunk, 44 + firstPcmBytes);
    client.write((uint8_t*)b64Chunk, outLen);

    // Subsequent Chunks: 768 bytes PCM (divisible by 6)
    while (pcmOffset < pcmBytes) {
        int bytesToRead = pcmBytes - pcmOffset;
        if (bytesToRead > 768) {
            bytesToRead = 768;
        }

        memcpy(rawChunk, ((uint8_t*)audioBuffer) + pcmOffset, bytesToRead);
        pcmOffset += bytesToRead;

        mbedtls_base64_encode((unsigned char*)b64Chunk, sizeof(b64Chunk), &outLen, rawChunk, bytesToRead);
        client.write((uint8_t*)b64Chunk, outLen);
    }
}

// ============================================================
// SEND AUDIO REQUEST TO GEMINI & PRINT TRANSCRIPTION + ANSWER
// ============================================================
void processAudioWithGemini() {
    if (recordedSamples < (SAMPLE_RATE / 2)) {
        Serial.println("Audio clip too short or no audio recorded.");
        return;
    }

    Serial.println("Connecting to Gemini endpoint...");

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(25);

    if (!client.connect(gemini_host, 443)) {
        Serial.println("Connection to Gemini failed!");
        return;
    }

    int pcmBytes = recordedSamples * sizeof(int16_t);
    int wavBytes = pcmBytes + 44;
    size_t base64Len = 4 * ((wavBytes + 2) / 3);

    const char* jsonPrefix = "{\"contents\":[{\"parts\":[{\"inlineData\":{\"mimeType\":\"audio/wav\",\"data\":\"";
    const char* jsonSuffix = "\"}},{\"text\":\"Listen carefully to the audio. Output your response strictly as a JSON object with two keys: \\\"user_text\\\" containing the verbatim transcription of what was spoken, and \\\"response\\\" containing your answer to what was spoken. Do not use markdown formatting.\"}]}]}";

    size_t totalContentLength = strlen(jsonPrefix) + base64Len + strlen(jsonSuffix);

    // Send HTTP Headers
    String path = "/v1beta/models/gemini-2.5-flash:generateContent?key=" + gemini_api_key;
    client.print("POST " + path + " HTTP/1.1\r\n");
    client.print("Host: generativelanguage.googleapis.com\r\n");
    client.print("Content-Type: application/json\r\n");
    client.print("Content-Length: " + String(totalContentLength) + "\r\n");
    client.print("Connection: close\r\n\r\n");

    // Stream JSON payload directly into socket
    client.print(jsonPrefix);
    streamAudioBase64(client, recordedSamples);
    client.print(jsonSuffix);
    client.flush();

    Serial.println("Audio streamed successfully. Waiting for Gemini response...");

    // Wait for response timeout (25s max)
    unsigned long timeout = millis();
    while (client.connected() && !client.available()) {
        if (millis() - timeout > 25000) {
            Serial.println("Response timeout (25s reached)!");
            client.stop();
            return;
        }
        delay(10);
    }

    // Read HTTP Status Line
    String statusLine = client.readStringUntil('\n');

    // Parse Headers to detect Transfer-Encoding
    bool isChunked = false;
    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line.equalsIgnoreCase("Transfer-Encoding: chunked\r") || line.equalsIgnoreCase("Transfer-Encoding: chunked")) {
            isChunked = true;
        }
        if (line == "\r" || line.length() == 0) {
            break;
        }
    }

    // Read Response Body (Unchunking if needed)
    String responseBody = "";
    if (isChunked) {
        while (client.connected() || client.available()) {
            String line = client.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;

            long chunkSize = strtol(line.c_str(), NULL, 16);
            if (chunkSize <= 0) break;

            char* chunkBuf = (char*)malloc(chunkSize + 1);
            if (chunkBuf) {
                size_t bytesRead = 0;
                while (bytesRead < (size_t)chunkSize && (client.connected() || client.available())) {
                    bytesRead += client.readBytes(chunkBuf + bytesRead, chunkSize - bytesRead);
                }
                chunkBuf[bytesRead] = '\0';
                responseBody += chunkBuf;
                free(chunkBuf);
            }
            client.readStringUntil('\n'); // Consume trailing \r\n
        }
    } else {
        responseBody = client.readString();
    }

    client.stop();

    // Check HTTP Status
    if (statusLine.indexOf("200") != -1) {
        JsonDocument resDoc;
        DeserializationError error = deserializeJson(resDoc, responseBody);

        if (!error) {
            const char* aiResponseRaw = resDoc["candidates"][0]["content"]["parts"][0]["text"];

            if (aiResponseRaw) {
                // Clean potential markdown blocks if returned
                String cleanJson = aiResponseRaw;
                cleanJson.replace("```json", "");
                cleanJson.replace("```", "");
                cleanJson.trim();

                JsonDocument structuredDoc;
                DeserializationError structErr = deserializeJson(structuredDoc, cleanJson);

                if (!structErr) {
                    const char* userText = structuredDoc["user_text"];
                    const char* aiResponse = structuredDoc["response"];

                    Serial.println("-----------------------------------------");
                    Serial.print("🗣️ You Said:      ");
                    Serial.println(userText ? userText : "N/A");
                    Serial.println("-----------------------------------------");
                    Serial.print("🤖 Gemini Answer: ");
                    Serial.println(aiResponse ? aiResponse : "N/A");
                    Serial.println("-----------------------------------------\n");
                } else {
                    // Fallback printing if model didn't format strict JSON
                    Serial.println("-----------------------------------------");
                    Serial.print("🤖 Gemini Response: ");
                    Serial.println(cleanJson);
                    Serial.println("-----------------------------------------\n");
                }
            } else {
                Serial.println("No answer text generated by model.");
            }
        } else {
            Serial.print("JSON Response Parsing Error: ");
            Serial.println(error.f_str());
            Serial.println("Raw Body: " + responseBody);
        }
    } else {
        Serial.print("HTTP Error Response: ");
        Serial.println(statusLine);
        Serial.println(responseBody);
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    pinMode(BUTTON_PIN, INPUT_PULLDOWN);

    connectToWiFi();

    Serial.println("=========================================");
    Serial.println(" Mic + Gemini Voice Assistant Ready.");
    Serial.println(" Hold button (GPIO 4 connected to 3.3V) to speak.");
    Serial.println("=========================================\n");
}

void loop() {
    if (digitalRead(BUTTON_PIN) == HIGH) {
        recordAudio();

        while (digitalRead(BUTTON_PIN) == HIGH) {
            delay(10);
        }

        delay(50);
        processAudioWithGemini();

        delay(300);
    }
}