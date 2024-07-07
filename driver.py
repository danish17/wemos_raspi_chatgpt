import os
import sys
import sounddevice as sd
import queue
import json
import time
import numpy
import serial
from vosk import Model, KaldiRecognizer
from gtts import gTTS
from pydub import AudioSegment
from pydub.playback import play

# Suppress warnings and errors
class suppress_stderr:
    def __enter__(self):
        self.old_stderr = sys.stderr
        sys.stderr = open(os.devnull, 'w')

    def __exit__(self, exc_type, exc_val, exc_tb):
        sys.stderr.close()
        sys.stderr = self.old_stderr

# Initialize the recognizer with Vosk model
model = Model("model")
recognizer = KaldiRecognizer(model, 16000)

q = queue.Queue()

# Setup serial connection to Wemos D1 Mini Lite
try:
    ser = serial.Serial('/dev/serial0', 115200, timeout=1)
    ser.flush()
except Exception as e:
    print(f"Error opening serial port: {e}")
    sys.exit()

def callback(indata, frames, time, status):
    if status:
        print(status, file=sys.stderr)
    q.put(bytes(indata))

def record_query():
    print("Listening for your query...")
    silence_start = None
    query_text = ""
    last_speech_time = time.time()
    max_silence_duration = 1  # Maximum silence duration in seconds
    min_query_duration = 1  # Minimum query duration in seconds
    energy_threshold = 100  # Adjust this value based on your microphone and environment

    while True:
        data = q.get()
        if recognizer.AcceptWaveform(data):
            result = recognizer.Result()
            text = json.loads(result)["text"]
            if text:
                print(f"Query recorded: {text}")
                query_text += text + " "
                last_speech_time = time.time()
                silence_start = None
        else:
            partial_result = recognizer.PartialResult()
            partial_text = json.loads(partial_result)["partial"]
            if partial_text:
                print(f"Partial: {partial_text}", end='\r')

        # Check for speech energy
        energy = numpy.frombuffer(data, dtype=numpy.int16).max()
        if energy > energy_threshold:
            last_speech_time = time.time()
            silence_start = None
        elif silence_start is None:
            silence_start = time.time()

        # Check for end conditions
        current_time = time.time()
        query_duration = current_time - last_speech_time
        silence_duration = current_time - silence_start if silence_start else 0

        if query_duration >= min_query_duration:
            if silence_duration >= max_silence_duration:
                print(f"\nSilence detected for {silence_duration:.1f} seconds, stopping recording.")
                break
            elif current_time - last_speech_time > 10:  # Maximum total duration
                print("\nMaximum recording duration reached, stopping recording.")
                break

    return query_text.strip()

def listen():
    print("Listening for the wake word...")
    while True:
        data = q.get()
        if recognizer.AcceptWaveform(data):
            result = recognizer.Result()
            text = json.loads(result)["text"]
            if text:
                print(text.lower())
            if "hello pixie" in text.lower():
                print("Wake word detected!")
                return True
        else:
            partial_result = recognizer.PartialResult()
            partial_text = json.loads(partial_result)["partial"]
            if "hello pixie" in partial_text.lower():
                print("Partial wake word detected!")
                return True

def clear_queue(q):
    while not q.empty():
        try:
            q.get_nowait()
        except queue.Empty:
            pass

def read_from_wemos():
    try:
        response = ""
        while True:
            if ser.in_waiting > 0:
                char = ser.read().decode('utf-8')
                if char == '\n':
                    break
                response += char
            else:
                time.sleep(0.1)
        return response
    except Exception as e:
        print(f"Error reading from Wemos: {e}")
        return None

def speak_text(text):
    tts = gTTS(text=text, lang='en')
    tts.save("response.mp3")
    sound = AudioSegment.from_mp3("response.mp3")
    play(sound)
    os.remove("response.mp3")

if __name__ == "__main__":
    with suppress_stderr():
        try:
            with sd.RawInputStream(samplerate=16000, blocksize=8000, dtype='int16', channels=1, callback=callback):
                while True:
                    if listen():
                        clear_queue(q)
                        recognizer.Reset()
                        query = record_query()
                        if query:
                            print(f"Processing query: {query}")
                            try:
                                ser.write((query + '\n').encode('utf-8'))
                                print("Query sent to Wemos")
                                
                                # Wait for and read the response from Wemos
                                print("Waiting for response from Wemos...")
                                response = read_from_wemos()
                                if response:
                                    print(f"Received response: {response}")
                                    speak_text(response)
                            except Exception as e:
                                print(f"Error communicating with Wemos: {e}")
        except KeyboardInterrupt:
            print("\nDone")
        except Exception as e:
            print(f"Error: {e}")

