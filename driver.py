import os
import sys
import sounddevice as sd
import queue
import json
import time
import numpy as np
import serial
from vosk import Model, KaldiRecognizer
import pygame
import asyncio
import edge_tts
import threading

# Suppress warnings and errors
class suppress_stderr:
    def __enter__(self):
        self.old_stderr = sys.stderr
        sys.stderr = open(os.devnull, 'w')

    def __exit__(self, exc_type, exc_val, exc_tb):
        sys.stderr.close()
        sys.stderr = self.old_stderr

# Initialize two recognizers: one for wake word, one for queries
wake_word_model = Model("vosk-model-small-en-us-0.15")
wake_word_recognizer = KaldiRecognizer(wake_word_model, 16000)
wake_word_recognizer.SetWords(True)

query_model = Model("vosk-model-small-en-us-0.15")
query_recognizer = KaldiRecognizer(query_model, 16000)

q = queue.Queue()

# Setup serial connection to Wemos D1 Mini Lite
try:
    ser = serial.Serial('/dev/serial0', 115200, timeout=1)
    ser.flush()
except Exception as e:
    print(f"Error opening serial port: {e}")
    sys.exit()

# Initialize pygame mixer
pygame.mixer.init()

# Global flag for interruption
interrupt_flag = threading.Event()

def play_beep():
    pygame.mixer.music.load("beep.mp3")
    pygame.mixer.music.play()
    while pygame.mixer.music.get_busy():
        pygame.time.Clock().tick(10)

def callback(indata, frames, time, status):
    if status:
        print(status, file=sys.stderr)
    q.put(bytes(indata))

def detect_wake_word(data):
    if wake_word_recognizer.AcceptWaveform(data):
        result = json.loads(wake_word_recognizer.Result())
        if "hello pixie" in result.get("text", "").lower():
            return True
    return False

def record_query():
    print("Listening for your query...")
    silence_start = None
    query_text = ""
    last_speech_time = time.time()
    max_silence_duration = 1
    min_query_duration = 1
    energy_threshold = 100

    while True:
        data = q.get()
        
        # Check for wake word (interrupt)
        if detect_wake_word(data):
            print("Interrupt detected!")
            return "INTERRUPT"

        if query_recognizer.AcceptWaveform(data):
            result = json.loads(query_recognizer.Result())
            text = result.get("text", "")
            if text:
                print(f"Query recorded: {text}")
                query_text += text + " "
                last_speech_time = time.time()
                silence_start = None
        else:
            partial_result = json.loads(query_recognizer.PartialResult())
            partial_text = partial_result.get("partial", "")
            if partial_text:
                print(f"Partial: {partial_text}", end='\r')

        energy = np.frombuffer(data, dtype=np.int16).max()
        if energy > energy_threshold:
            last_speech_time = time.time()
            silence_start = None
        elif silence_start is None:
            silence_start = time.time()

        current_time = time.time()
        query_duration = current_time - last_speech_time
        silence_duration = current_time - silence_start if silence_start else 0

        if query_duration >= min_query_duration:
            if silence_duration >= max_silence_duration:
                print(f"\nSilence detected for {silence_duration:.1f} seconds, stopping recording.")
                break
            elif current_time - last_speech_time > 10:
                print("\nMaximum recording duration reached, stopping recording.")
                break

    return query_text.strip()

def listen():
    print("Listening for the wake word...")
    while True:
        data = q.get()
        if detect_wake_word(data):
            print("Wake word detected!")
            play_beep()
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

async def speak_text(text):
    communicate = edge_tts.Communicate(text, "en-US-AriaNeural")
    await communicate.save("response.mp3")
    
    pygame.mixer.init()
    pygame.mixer.music.load("response.mp3")
    pygame.mixer.music.play()
    
    while pygame.mixer.music.get_busy():
        pygame.time.Clock().tick(10)
        if interrupt_flag.is_set():
            pygame.mixer.music.stop()
            break
    
    os.remove("response.mp3")

def check_for_wake_word():
    global interrupt_flag
    while pygame.mixer.music.get_busy() and not interrupt_flag.is_set():
        try:
            data = q.get(timeout=0.1)
            if detect_wake_word(data):
                print("Wake word detected during playback!")
                interrupt_flag.set()
                return True
        except queue.Empty:
            pass
    return False

if __name__ == "__main__":
    with suppress_stderr():
        try:
            with sd.RawInputStream(samplerate=16000, blocksize=8000, dtype='int16', channels=1, callback=callback):
                while True:
                    if listen():
                        clear_queue(q)
                        wake_word_recognizer.Reset()
                        query_recognizer.Reset()
                        query = record_query()
                        if query == "INTERRUPT":
                            print("Interrupted. Listening for new wake word.")
                            continue
                        if query:
                            print(f"Processing query: {query}")
                            try:
                                ser.write((query + '\n').encode('utf-8'))
                                print("Query sent to Wemos")
                                
                                print("Waiting for response from Wemos...")
                                response = read_from_wemos()
                                if response:
                                    print(f"Received response: {response}")
                                    
                                    # Reset the interrupt flag
                                    interrupt_flag.clear()
                                    
                                    # Start a thread to check for wake word during playback
                                    wake_word_thread = threading.Thread(target=check_for_wake_word)
                                    wake_word_thread.start()
                                    
                                    # Play the response
                                    asyncio.run(speak_text(response))
                                    
                                    # Wait for the wake word thread to finish
                                    wake_word_thread.join()
                                    
                                    if interrupt_flag.is_set():
                                        print("Playback interrupted. Listening for new query.")
                                        continue
                            except Exception as e:
                                print(f"Error communicating with Wemos: {e}")
        except KeyboardInterrupt:
            print("\nDone")
        except Exception as e:
            print(f"Error: {e}")
