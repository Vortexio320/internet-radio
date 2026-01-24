#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <atomic>
#include <csignal>

using namespace std;

// Server Configuration
#define PORT_CMD 8080
#define PORT_AUDIO 8081
#define BUFFER_SIZE 1024

vector<string> playlist;
mutex playlist_mutex;

vector<int> listeners;
mutex listeners_mutex;

// Atomic flags for thread synchronization
atomic<bool> skip_requested(false);
atomic<bool> suppress_logs(false); // Prevents log spam during client reconnections

void add_song_safe(const string& filename) {
    lock_guard<mutex> lock(playlist_mutex);
    playlist.push_back(filename);
    cout << "PLAYLIST: Dodano " << filename << ". Razem: " << playlist.size() << endl;
}

// -- DJ THREAD (AUDIO STREAMING) --
// Handles the audio pipeline: ffmpeg decoding -> broadcasting to sockets
void radio_sender_thread() {
    cout << "DJ: Startuje na porcie " << PORT_AUDIO << endl;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT_AUDIO);
    
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);

    // Background thread to accept new audio listeners
    thread([server_fd](){
        while(true) {
            int new_sock = accept(server_fd, nullptr, nullptr);
            if (new_sock >= 0) {
                lock_guard<mutex> lock(listeners_mutex);
                listeners.push_back(new_sock);
                if (!suppress_logs) {
                    cout << "AUDIO: Nowy sluchacz dolaczyl (" << new_sock << ")." << endl;
                }
            }
        }
    }).detach();

    FILE *fp = nullptr;

    while(true) {
        if (!fp) {
            string current_song;
            {
                lock_guard<mutex> lock(playlist_mutex);
                if (!playlist.empty()) {
                    current_song = playlist.front();
                    playlist.erase(playlist.begin());
                    playlist.push_back(current_song);
                } else {
                    current_song = "elevatormusic.mp3";
                }
            }
            
            cout << "DJ: Gram utwor: " << current_song << endl;

            // Decode MP3 to raw PCM (s16le, 44.1kHz, stereo) using ffmpeg pipe
            string cmd = "ffmpeg -i \"" + current_song + "\" -f s16le -ac 2 -ar 44100 - 2>/dev/null";
            fp = popen(cmd.c_str(), "r");
            
            if (!fp) {
                cerr << "DJ: Blad ffmpeg! Czekam 5s..." << endl;
                this_thread::sleep_for(chrono::seconds(5));
                continue;
            }
        }

        char buffer[4096];
        
        // --- SKIP HANDLER ---
        if (skip_requested) {
            cout << "DJ: SKIP REQUESTED! Zamykam utwor." << endl;
            pclose(fp);
            fp = nullptr;
            skip_requested = false;
            continue;
        }

        int bytes_read = fread(buffer, 1, sizeof(buffer), fp);
        
        if (bytes_read <= 0) {
            cout << "DJ: Koniec utworu." << endl;
            pclose(fp);
            fp = nullptr;
            continue;
        }

        // Broadcast buffer to all connected listeners
        {
            lock_guard<mutex> lock(listeners_mutex);
            auto it = listeners.begin();
            while (it != listeners.end()) {
                int sent = send(*it, buffer, bytes_read, MSG_NOSIGNAL);
                if (sent < 0) {
                    close(*it);
                    it = listeners.erase(it);
                    if (!suppress_logs) {
                        cout << "DJ: Sluchacz rozlaczony." << endl;
                    }
                } else {
                    ++it;
                }
            }
        }
        
        // Short sleep (5ms) to pace the stream and avoid CPU spinning
        this_thread::sleep_for(chrono::microseconds(5000));
    }
}

// Funkcja zamieniająca dziwne znaki na bezpieczne "_"
string sanitize_filename(string filename) {
    string safe_name = filename;
    for (char &c : safe_name) {
        // Zostawiamy litery, cyfry, kropkę i minus
        if (!isalnum(c) && c != '.' && c != '-') {
            c = '_'; // Reszta (spacje, $, nawiasy) zamienia się w _
        }
    }
    return safe_name;
}

// --- COMMAND HANDLER ---
// Processes control commands (LIST, ADD, UPLOAD, SKIP)
void handle_client(int sock) {
    cout << "CMD: Klient " << sock << " polaczony." << endl;
    char buffer[BUFFER_SIZE];

    while (true) {
        int bytes_read = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_read <= 0) break;

        buffer[bytes_read] = '\0';
        string command(buffer);
        // Remove trailing newlines
        while (!command.empty() && (command.back() == '\r' || command.back() == '\n')) {
            command.pop_back();
        }

        if (command == "LIST") {
            lock_guard<mutex> lock(playlist_mutex);
            string response;
            if (playlist.empty()) response = "Playlista jest pusta.\n";
            else {
                response = "--- PLAYLISTA ---\n";
                for (size_t i = 0; i < playlist.size(); ++i) {
                    response += to_string(i + 1) + ". " + playlist[i] + "\n";
                }
            }
            send(sock, response.c_str(), response.length(), 0);
        }
        // --- ADD SECTION ---
        else if (command.rfind("ADD ", 0) == 0) {
            string filename = command.substr(4);
            if (!filename.empty()) {
                add_song_safe(filename);
                send(sock, "OK\n", 3, 0);
            }
        }
        // --- UPLOAD SECTION ---
        else if (command.rfind("UPLOAD ", 0) == 0) {
            size_t size_pos = 7;
            size_t space_pos = command.find(' ', size_pos);
            
            if (space_pos != string::npos) {
                int filesize = stoi(command.substr(size_pos, space_pos - size_pos));
                string original_filename = command.substr(space_pos + 1);
                string filename = sanitize_filename(original_filename);
                cout << "UPLOAD START: " << filename << " (" << filesize << " bajtow)" << endl;
                // Critical: Send READY to synchronize with client before data transfer
                send(sock, "READY\n", 6, 0);
                
                FILE *f = fopen(filename.c_str(), "wb");
                if (f) {
                    char file_buf[4096];
                    int total_received = 0;
                    while (total_received < filesize) {
                        int r = recv(sock, file_buf, min((int)sizeof(file_buf), filesize - total_received), 0);
                        if (r <= 0) break;
                        fwrite(file_buf, 1, r, f);
                        total_received += r;
                    }
                    fclose(f);
                    add_song_safe(filename);
                    send(sock, "OK\n", 3, 0);
                    cout << "UPLOAD END: " << filename << endl;
                } else {
                    send(sock, "ERROR\n", 6, 0);
                }
            }
        }
        // --- SKIP SECTION ---
        else if (command == "SKIP") {
            skip_requested = true;
            suppress_logs = true;
            thread([](){
                this_thread::sleep_for(chrono::seconds(2));
                suppress_logs = false;
            }).detach();
            

            send(sock, "OK; Skipping...\n", 16, 0);
            cout << "CMD: Zazadano SKIP." << endl;
        }
        else if (command == "EXIT") {
            break;
        }
    }
    close(sock);
    cout << "CMD: Klient rozlaczony." << endl;
}

// Funkcja wywoływana, gdy wciśniesz Ctrl+C
void signal_handler(int signum) {
    cout << "\n[SYSTEM] Otrzymano sygnał wyjścia (Ctrl+C). Sprzątanie pozostalych plikow muzycznych..." << endl;

    lock_guard<mutex> lock(playlist_mutex);
    for (const auto& song : playlist) {
        // Zabezpieczenie: Nie usuwaj plików systemowych/startowych
        if (song == "elevatormusic.mp3" || song == "test.mp3") {
            continue;
        }

        // remove() zwraca 0 jeśli sukces
        if (remove(song.c_str()) == 0) {
            cout << "[CLEANUP] Usunieto plik: " << song << endl;
        } else {
            cerr << "[CLEANUP] Blad usuwania (moze juz nie istnieje): " << song << endl;
        }
    }

    cout << "[SYSTEM] Serwer bezpiecznie zamkniety." << endl;
    exit(signum); // Wyjście z programu
}



int main() {
    signal(SIGINT, signal_handler);
    thread dj_thread(radio_sender_thread);
    dj_thread.detach();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT_CMD);
    
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);
    
    cout << "SERWER READY (CMD: " << PORT_CMD << ", AUDIO: " << PORT_AUDIO << ")" << endl;

    while (true) {
        int new_sock = accept(server_fd, nullptr, nullptr);
        if (new_sock >= 0) {
            thread t(handle_client, new_sock);
            t.detach();
        }
    }
    return 0;
}