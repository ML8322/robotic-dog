import requests
import time
from openai import OpenAI
import base64
import json
from PIL import Image
import io
import os

client = OpenAI(api_key="xxxxxxxxxxxxxx") # Hidden

ESP32_IP = "http://xxxxxx" # Hidden

# --- GET IMAGE ---
def get_image():
    for _ in range(3):  # try quickly
        try:
            response = requests.get(f"{ESP32_IP}/capture", timeout=4)
            return response.content
        except:
            print("Camera retry...")
            time.sleep(0.5)

    print("Camera timeout")
    return None

# --- COMPRESS IMAGE (VERY IMPORTANT) ---
def compress_image(image_bytes):
    img = Image.open(io.BytesIO(image_bytes))
    img = img.resize((320, 240))  # smaller = faster + cheaper

    buffer = io.BytesIO()
    img.save(buffer, format="JPEG", quality=50)

    return buffer.getvalue()

# --- SEND COMMAND ---
def send_command(cmd):
    try:
        requests.get(f"{ESP32_IP}/cmd?c={cmd}", timeout=2)
        print("Sent:", cmd)
        time.sleep(1)
    except:
        print("Command failed:", cmd)

# --- SEND SPEECH ---
def send_speech(file_path):
    try:
        with open(file_path, "rb") as f:
            requests.post(f"{ESP32_IP}/audio", files={"file": f}, timeout=5)
        print("Speech sent")
    except:
        print("Speech failed")

# --- AI VISION ---
def process_image(image_bytes):
    compressed = compress_image(image_bytes)
    b64 = base64.b64encode(compressed).decode()

    response = client.chat.completions.create(
        model="gpt-5-mini",
        messages=[
            {
                "role": "system",
                "content": (
                    "You are an autonomous robot dog.\n\n"
                    "Speak like an intelligent robot dog a with strong, passionate curiosity.\n"
                    "You must decide ONE action at a time.\n\n"

                    "Commands:\n"
                    "e = walk forward\n"
                    "g = walk backward\n"
                    "f = turn right\n"
                    "h = turn left\n"
                    "u = raise body\n"
                    "j = lower body\n"
                    "x = sit down\n"
                    "r = tilt head right\n"
                    "l = tilt head left\n"
                    "m = look up\n"
                    "i = look down\n"
                    "o = center head\n\n"

                    "Rules:\n"
                    "- Only ONE command\n"
                    "- Use repeat=true for movement (e,g,f,h)\n"
                    "- Use repeat=false for head/single actions\n"
                    "- Avoid obstacles\n"
                    "- Explore naturally\n\n"

                    "Respond ONLY JSON:\n"
                    '{"speech":"...", "command":"e", "repeat":true}'
                )
            },
            {
                "role": "user",
                "content": [
                    {"type": "text", "text": "What should you do based on this view?"},
                    {"type": "image_url", "image_url": {"url": f"data:image/jpeg;base64,{b64}"}}
                ]
            }
        ]
    )

    content = response.choices[0].message.content
    print("AI raw:", content)

    data = json.loads(content)

    return data["speech"], data["command"], data["repeat"]

# --- TEXT TO SPEECH ---
def text_to_speech(text):
    response = client.audio.speech.create(
        model="gpt-4o-mini-tts",
        voice="alloy",
        input=text
    )

    with open("speech.wav", "wb") as f:
        f.write(response.content)

    return "speech.wav"

# --- MAIN LOOP ---
while True:
    print("📸 Capturing...")

    img = get_image()
    if img is None:
        time.sleep(2)
        continue

    speech, command, repeat = process_image(img)

    print("🧠:", speech)
    print("⚡:", command, "| repeat =", repeat)

    # --- EXECUTE COMMAND FIRST ---
    if repeat:
        print("Repeating:", command)

        for _ in range(3):
            try:
                requests.get(f"{ESP32_IP}/cmd?c={command}", timeout=2)
                print("Sent:", command)
            except: 
                print("Command failed:", command)

            time.sleep(0.6)

    else:
        print("Single action:", command)

        try:
            requests.get(f"{ESP32_IP}/cmd?c={command}", timeout=2)
            print("Sent:", command)
        except:
            print("Command failed:", command)

        time.sleep(1)

    # --- SPEAK ON ESP32 ---
    speech_file = text_to_speech(speech)
    send_speech(speech_file)

    # --- WAIT BEFORE NEXT CAPTURE ---
    time.sleep(4)
