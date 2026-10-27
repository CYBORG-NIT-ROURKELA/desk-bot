# Build-Time Workflow: .env Injection

Credentials live in `.env` and are injected into the firmware at compile time, so they never appear in `main.cpp`.

## Workflow

1. You run `pio run` (or press Upload).
2. PlatformIO sees `extra_scripts = pre:load_env.py` and runs that script before compiling anything.
3. The script opens `.env` and reads it line by line, skipping blanks and comments.
4. Each `KEY="value"` line is split and cleaned of quotes, then added to the compiler flags as a macro, so `WIFI_SSID="MyNet"` becomes `-DWIFI_SSID="MyNet"`. `StringifyMacro` handles the quote escaping.
5. The compiler builds `main.cpp`, and every `WIFI_SSID`, `WIFI_PASSWORD` and `GEMINI_API_KEY` in your code is replaced with the real string.
6. The `#error` check stops the build with a clear message if any key is missing.

## Flow

```
.env  →  load_env.py  →  -D compiler macros  →  main.cpp  →  firmware
```

## Why It Helps

- **No secrets in source code.** `main.cpp` contains only the names, so you can share or commit it safely.
- **One place to change credentials.** Edit `.env`, and the code stays untouched.
- **Safe version control.** With `.env` in `.gitignore`, your Wi-Fi password and API key never reach GitHub.
- **Easy per-machine setup.** A teammate (or a second board) copies a `.env.example`, fills in their own values, and builds.
- **Early failure.** A missing key fails at compile time, not as a confusing connection error on the device.