Import("env")
import os

env_path = os.path.join(env.get("PROJECT_DIR"), ".env")

if os.path.exists(env_path):
    with open(env_path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                key, value = line.split("=", 1)
                value = value.strip('"\'')
                # Inject each key/value pair as a macro definition (-D KEY="value")
                env.Append(CPPDEFINES=[(key, f'\\"{value}\\"')])