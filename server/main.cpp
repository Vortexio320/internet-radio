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
#include <set>

using namespace std;

// Server Configuration
#define PORT_CMD 8080
#define PORT_AUDIO 8081
#define BUFFER_SIZE 1024

// --- GLOBAL STATE & SYNCHRONIZATION ---

// Playlist management
vector<string> playlist;
mutex playlist_mutex;

// Audio listeners (Streaming sockets)
vector<int> listeners;
mutex listeners_mutex;

// Command clients (Chat, Control sockets)
vector<int> cmd_clients;
mutex cmd_clients_mutex;

// Voting system for SKIP
set<int> skip_voters;
mutex skip_vote_mutex;

// Atomic flags for thread synchronization
atomic<bool> skip_requested(false);
atomic<bool> suppress_logs(false);

// Thread-safe addition of songs to the playlist
void add_song_safe(const string& filename) {
    lock_guard<mutex> lock(playlist_mutex);
    playlist.push_back(filename);
    cout << "PLAYLIST: Dodano " << filename << ". Razem: " << playlist.size() << endl;
}

// --- AUDIO STREAMING THREAD (THE DJ) ---
// Responsible for:
// 1. Managing the audio socket server.
// 2. Decoding MP3s using ffmpeg.
// 3. Broadcasting raw PCM data to all listeners.
void radio_sender_thread() {
    cout << "DJ: Startuje na porcie " << PORT_AUDIO << endl;

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    // Allow socket descriptor reuse immediately after restart
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

    // Main Audio Loop
    while(true) {
        // If no file is playing, pick the next one
        if (!fp) {
            string current_song;
            {
                lock_guard<mutex> lock(playlist_mutex);
                if (!playlist.empty()) {
                    current_song = playlist.front();
                    playlist.erase(playlist.begin());
                    playlist.push_back(current_song); // Rotate playlist
                } else {
                    current_song = "elevatormusic.mp3";
                }
            }
            
            cout << "DJ: Gram utwor: " << current_song << endl;

            {
                string msg = "CURRENT " + current_song + "\n";
                lock_guard<mutex> lock(cmd_clients_mutex);
                for (int client_sock : cmd_clients) {
                    send(client_sock, msg.c_str(), msg.length(), MSG_NOSIGNAL);
                }
            }

            // Decode MP3 to raw PCM (s16le, 44.1kHz, stereo) using ffmpeg pipe
            string cmd = "ffmpeg -i \"" + current_song + "\" -f s16le -ac 2 -ar 44100 - 2>/dev/null";
            //debug line
            //string cmd = "ffmpeg -i \"" + current_song + "\" -f s16le -ac 2 -ar 44100 -";
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

        // Broadcast audio chunk to all listeners
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
        this_thread::sleep_for(chrono::microseconds(22000));
    }
}

//cleaning the filename to bypass errors
string sanitize_filename(string filename) {
    string safe_name = filename;
    for (char &c : safe_name) {
        if (!isalnum(c) && c != '.' && c != '-') {
            c = '_';
        }
    }
    return safe_name;
}

// Broadcasts a message to all connected command clients (Chat & System info)
void broadcast_msg(string message) {
    lock_guard<mutex> lock(cmd_clients_mutex);
    if (message.back() != '\n') message += "\n";
    
    for (int client_sock : cmd_clients) {
        send(client_sock, message.c_str(), message.length(), MSG_NOSIGNAL);
    }
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
        // MSG: Chat functionality
        else if (command.rfind("MSG ", 0) == 0) {
            string content = command.substr(4);
            string chat_packet = "CHAT [Klient " + to_string(sock) + "]: " + content;
            broadcast_msg(chat_packet);
            cout << "CHAT: " << content << endl;
        }
        
        // --- VOTE SKIP SECTION ---
        else if (command == "SKIP") {
            lock_guard<mutex> lock(skip_vote_mutex);
            
            if (skip_voters.count(sock)) {
                string info = "CHAT [SYSTEM]: Juz zaglosowales na SKIP!";
                send(sock, info.c_str(), info.length(), MSG_NOSIGNAL); // Wyslij tylko do niego
            } else {
                skip_voters.insert(sock);
                
                // Calculate required votes (Simple majority: 50% + 1)
                int total_users = 0;
                {
                    lock_guard<mutex> c_lock(cmd_clients_mutex);
                    total_users = cmd_clients.size();
                }
                
                int votes_needed = (total_users / 2) + 1;
                int current_votes = skip_voters.size();

                string vote_info = "CHAT [SYSTEM]: Glos na SKIP (" + to_string(current_votes) + "/" + to_string(votes_needed) + ")";
                broadcast_msg(vote_info);

                if (current_votes >= votes_needed) {
                    broadcast_msg("CHAT [SYSTEM]: Glosowanie zakonczone! Zmieniam utwor...");
                    
                    skip_voters.clear();
                    
                    skip_requested = true;
                    suppress_logs = true;
                    thread([](){
                        this_thread::sleep_for(chrono::seconds(2));
                        suppress_logs = false;
                    }).detach();
                    
                    send(sock, "OK; Skipping...\n", 16, 0);
                    cout << "CMD: VOTE SKIP SUKCES." << endl;
                }
            }
        }
        else if (command == "EXIT") {
            break;
        }
    }
    {
        lock_guard<mutex> lock(cmd_clients_mutex);
        cmd_clients.erase(remove(cmd_clients.begin(), cmd_clients.end(), sock), cmd_clients.end());
    }
    {
        lock_guard<mutex> lock(skip_vote_mutex);
        skip_voters.erase(sock);
    }
    close(sock);
    cout << "CMD: Klient rozlaczony." << endl;
}

//cleaning server folder when terminating program (CTRL + C)
void signal_handler(int signum) {
    cout << "\n[SYSTEM] Otrzymano sygnał wyjścia (Ctrl+C). Sprzątanie pozostalych plikow muzycznych..." << endl;

    lock_guard<mutex> lock(playlist_mutex);
    for (const auto& song : playlist) {
        if (song == "elevatormusic.mp3") {
            continue;
        }

        if (remove(song.c_str()) == 0) {
            cout << "[CLEANUP] Usunieto plik: " << song << endl;
        } else {
            cerr << "[CLEANUP] Blad usuwania (moze juz nie istnieje): " << song << endl;
        }
    }

    cout << "[SYSTEM] Serwer bezpiecznie zamkniety." << endl;
    exit(signum);
}



int main() {
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    // Start the Audio Thread
    thread dj_thread(radio_sender_thread);
    dj_thread.detach();

    // Setup Command Socket
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
            {
                lock_guard<mutex> lock(cmd_clients_mutex);
                cmd_clients.push_back(new_sock);
            }
            // Handle each client in a separate thread
            thread t(handle_client, new_sock);
            t.detach();
        }
    }
    return 0;
}