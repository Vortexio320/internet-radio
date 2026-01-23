import socket
import threading
import tkinter as tk
from tkinter import scrolledtext, messagebox

SERVER_IP = '127.0.0.1'
SERVER_PORT = 8080

class RadioClient:
    def __init__(self, root):
        self.root = root
        self.root.title("Radio Intenetowe")
        self.root.geometry("500x400")
        
        self.sock = None
        self.connected = False

        #gui
        frame_conn = tk.Frame(root)
        frame_conn.pack(pady=5)
        
        tk.Label(frame_conn, text="IP:").pack(side=tk.LEFT)
        self.entry_ip = tk.Entry(frame_conn, width=15)
        self.entry_ip.insert(0, SERVER_IP)
        self.entry_ip.pack(side=tk.LEFT, padx=5)
        
        self.btn_connect = tk.Button(frame_conn, text="Połącz", command=self.connect_to_server)
        self.btn_connect.pack(side=tk.LEFT)

        self.text_area = scrolledtext.ScrolledText(root, state='disabled', height=15)
        self.text_area.pack(padx=10, pady=5, fill=tk.BOTH, expand=True)

        frame_controls = tk.Frame(root)
        frame_controls.pack(pady=10)

        self.entry_song = tk.Entry(frame_controls, width=25)
        self.entry_song.pack(side=tk.LEFT, padx=5)
        
        self.btn_add = tk.Button(frame_controls, text="Dodaj utwór", command=self.add_song, state=tk.DISABLED)
        self.btn_add.pack(side=tk.LEFT, padx=2)

        self.btn_list = tk.Button(frame_controls, text="Odśwież Listę", command=self.get_list, state=tk.DISABLED)
        self.btn_list.pack(side=tk.LEFT, padx=2)

    def log(self, message):
        """Dopisuje tekst do głównego okna w bezpieczny sposób"""
        self.text_area.config(state='normal')
        self.text_area.insert(tk.END, message + "\n")
        self.text_area.see(tk.END) #opcjonalny autoscroll
        self.text_area.config(state='disabled')

    def connect_to_server(self):
        ip = self.entry_ip.get()
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((ip, SERVER_PORT))
            self.connected = True
            
            self.log(f"Połączono z {ip}:{SERVER_PORT}")
            
            self.btn_connect.config(state=tk.DISABLED)
            self.btn_add.config(state=tk.NORMAL)
            self.btn_list.config(state=tk.NORMAL)

            listener_thread = threading.Thread(target=self.listen_to_server, daemon=True)
            listener_thread.start()

        except Exception as e:
            messagebox.showerror("Błąd", f"Nie można połączyć: {e}")

    def listen_to_server(self):
        """Wątek, który ciągle czeka na wiadomości od serwera"""
        while self.connected:
            try:
                data = self.sock.recv(1024)
                if not data:
                    break
                message = data.decode('utf-8')
                self.log(f"[SERWER]: {message}")
            except:
                break
        
        self.log("Rozłączono z serwerem.")
        self.connected = False
        self.btn_connect.config(state=tk.NORMAL)
        self.btn_add.config(state=tk.DISABLED)
        self.btn_list.config(state=tk.DISABLED)

    def send_command(self, cmd):
        if self.connected and self.sock:
            try:
                self.sock.send(cmd.encode('utf-8'))
            except Exception as e:
                self.log(f"Błąd wysyłania: {e}")

    def add_song(self):
        song_name = self.entry_song.get()
        if song_name:
            self.send_command(f"ADD {song_name}")
            self.entry_song.delete(0, tk.END)

    def get_list(self):
        self.send_command("LIST")

if __name__ == "__main__":
    root = tk.Tk()
    app = RadioClient(root)
    root.mainloop()