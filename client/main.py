import socket
import threading
import tkinter as tk
from tkinter import scrolledtext, messagebox, filedialog
import pyaudio
import os
import time
import audioop

# Configuration
SERVER_IP = '127.0.0.1' 
CMD_PORT = 8080
AUDIO_PORT = 8081

# Audio parameters
CHUNK = 4096
FORMAT = pyaudio.paInt16
CHANNELS = 2
RATE = 44100

class RadioClient:
    def __init__(self, root):
        self.root = root
        self.root.title("Radio Studenckie - Klient (Fixed Upload)")
        self.root.geometry("750x500")
        
        self.cmd_sock = None
        self.connected = False
        self.audio_running = False
        
        # Upload synchronization flag
        self.upload_ready_event = threading.Event()

        frame_conn = tk.Frame(root)
        frame_conn.pack(pady=5)
        tk.Label(frame_conn, text="IP:").pack(side=tk.LEFT)
        self.entry_ip = tk.Entry(frame_conn, width=15)
        self.entry_ip.insert(0, SERVER_IP)
        self.entry_ip.pack(side=tk.LEFT, padx=5)
        self.btn_connect = tk.Button(frame_conn, text="Połącz (START)", command=self.connect_all)
        self.btn_connect.pack(side=tk.LEFT)

        self.text_area = scrolledtext.ScrolledText(root, state='disabled', height=15)
        self.text_area.pack(padx=10, pady=5, fill=tk.BOTH, expand=True)

        frame_controls = tk.Frame(root)
        frame_controls.pack(pady=10)
        self.entry_song = tk.Entry(frame_controls, width=20)
        self.entry_song.pack(side=tk.LEFT, padx=5)
        
        self.btn_add = tk.Button(frame_controls, text="Dodaj nazwę", command=self.add_song, state=tk.DISABLED)
        self.btn_add.pack(side=tk.LEFT, padx=2)
        
        self.btn_list = tk.Button(frame_controls, text="Lista", command=self.get_list, state=tk.DISABLED)
        self.btn_list.pack(side=tk.LEFT, padx=2)

        self.btn_upload = tk.Button(frame_controls, text="Upload MP3", command=self.upload_file, state=tk.DISABLED)
        self.btn_upload.pack(side=tk.LEFT, padx=2)

        self.btn_skip = tk.Button(frame_controls, text="SKIP >>", command=self.skip_song, state=tk.DISABLED, bg="#ffcccc")
        self.btn_skip.pack(side=tk.LEFT, padx=10)

        tk.Label(frame_controls, text="Vol:").pack(side=tk.LEFT, padx=(10, 2))
        self.volume_scale = tk.Scale(frame_controls, from_=0, to=100, orient=tk.HORIZONTAL, resolution=1)
        self.volume_scale.set(100) #Default: Full Volume
        self.volume_scale.pack(side=tk.LEFT)

    def log(self, msg):
        self.text_area.config(state='normal')
        self.text_area.insert(tk.END, msg + "\n")
        self.text_area.see(tk.END)
        self.text_area.config(state='disabled')

    def connect_all(self):
        ip = self.entry_ip.get()
        try:
            self.cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.cmd_sock.connect((ip, CMD_PORT))
            self.connected = True
            
            # Start listening thread
            threading.Thread(target=self.listen_cmd, daemon=True).start()
            
            # Start audio
            self.start_audio_stream(ip)
            
            self.log(f"Połączono z serwerem {ip}")
            self.btn_connect.config(state=tk.DISABLED)
            self.btn_add.config(state=tk.NORMAL)
            self.btn_list.config(state=tk.NORMAL)
            self.btn_upload.config(state=tk.NORMAL)
            self.btn_skip.config(state=tk.NORMAL)
            
        except Exception as e:
            messagebox.showerror("Błąd", f"Nie można połączyć: {e}")

    def listen_cmd(self):
        while self.connected:
            try:
                # This thread reads EVERYTHING coming from the server
                data = self.cmd_sock.recv(1024)
                if not data: break
                
                msg = data.decode('utf-8', errors='ignore').strip()
                
                if "READY" in msg:
                    # Signal upload thread that sending is allowed
                    self.upload_ready_event.set()
                else:
                    self.log(f"[SERWER]: {msg}")
            except: break

    def start_audio_stream(self, ip):
        self.audio_running = True
        threading.Thread(target=self.audio_worker, args=(ip,), daemon=True).start()

    def audio_worker(self, ip):
        try:
            p = pyaudio.PyAudio()
            stream = p.open(format=FORMAT, channels=CHANNELS, rate=RATE, output=True, frames_per_buffer=CHUNK)
            audio_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            audio_sock.connect((ip, AUDIO_PORT))
            
            # Pre-buffering, also implemented volume slider
            initial_buffer = []
            for _ in range(10):
                data = audio_sock.recv(CHUNK)
                if not data: break

                try:
                    vol = self.volume_scale.get() / 100.0
                    data = audioop.mul(data, 2, vol)
                except: pass

                initial_buffer.append(data)
            for data in initial_buffer: stream.write(data)
            
            while self.audio_running:
                data = audio_sock.recv(CHUNK)
                if not data: break

                try:
                    vol_level = self.volume_scale.get() / 100.0
                    data = audioop.mul(data, 2, vol_level)
                except Exception:
                    pass

                stream.write(data)
        except Exception as e:
            pass
        finally:
            try:
                stream.stop_stream()
                stream.close()
                p.terminate()
                audio_sock.close()
            except: pass

    def send_command(self, cmd):
        if self.cmd_sock: self.cmd_sock.send(cmd.encode('utf-8'))

    def add_song(self):
        name = self.entry_song.get()
        if name:
            self.send_command(f"ADD {name}")
            self.entry_song.delete(0, tk.END)

    def get_list(self):
        self.send_command("LIST")

    def skip_song(self):
        self.send_command("SKIP")
        
        # Buffer flush: Stop current audio thread to cut off old song
        self.audio_running = False 
        
        # Restart audio after short pause (500ms) to allow connection close
        self.root.after(500, lambda: self.start_audio_stream(self.entry_ip.get()))

    def upload_file(self):
        filepath = filedialog.askopenfilename(filetypes=[("MP3 files", "*.mp3")])
        if not filepath: return
        threading.Thread(target=self._upload_thread_logic, args=(filepath,), daemon=True).start()

    def _upload_thread_logic(self, filepath):
        filename = os.path.basename(filepath)
        filesize = os.path.getsize(filepath)
        self.log(f"Rozpoczynam upload: {filename}...")
        self.btn_upload.config(state=tk.DISABLED)

        try:
            self.upload_ready_event.clear()

            header = f"UPLOAD {filesize} {filename}"
            self.cmd_sock.send(header.encode('utf-8'))
            
            if not self.upload_ready_event.wait(timeout=5.0):
                self.log("Błąd: Serwer nie odpowiedział READY (Timeout)")
                self.btn_upload.config(state=tk.NORMAL)
                return
            
            self.log("Serwer gotowy. Wysyłanie danych...")

            with open(filepath, "rb") as f:
                while True:
                    chunk = f.read(4096)
                    if not chunk: break
                    self.cmd_sock.send(chunk)
                    
            self.log(f"SUKCES: Wysłano {filename}")
            
        except Exception as e:
            self.log(f"Błąd uploadu: {e}")
        finally:
            self.btn_upload.config(state=tk.NORMAL)

if __name__ == "__main__":
    root = tk.Tk()
    app = RadioClient(root)
    root.mainloop()