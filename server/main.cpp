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

using namespace std;

#define PORT 8080
#define BUFFER_SIZE 1024

vector<string> playlist;
mutex playlist_mutex;

void handle_client(int sock) {
    cout << "Watek: Klient " << sock << " podlaczony." << endl;

    char buffer[BUFFER_SIZE];
    
    string help = "Witaj! Komendy: ADD <nazwa>, LIST, EXIT\n";
    send(sock, help.c_str(), help.length(), 0);

    while (true) {
        int bytes_read = recv(sock, buffer, BUFFER_SIZE, 0);
        if (bytes_read <= 0) break;

        buffer[bytes_read] = '\0';
        
        string command(buffer);

        while (!command.empty() && (command.back() == '\r' || command.back() == '\n')) {
            command.pop_back();
        }

        cout << "Klient " << sock << " wyslal: " << command << endl;

        if (command == "LIST") {
            lock_guard<mutex> lock(playlist_mutex);
            
            string response;
            if (playlist.empty()) {
                response = "Playlista jest pusta.\n";
            } else {
                response = "--- PLAYLISTA ---\n";
                for (size_t i = 0; i < playlist.size(); ++i) {
                    response += to_string(i + 1) + ". " + playlist[i] + "\n";
                }
            }
            send(sock, response.c_str(), response.length(), 0);
        }
        else if (command.rfind("ADD ", 0) == 0) {
            string filename = command.substr(4); //czytamy od znaku 5
            
            if (!filename.empty()) {
                {
                    lock_guard<mutex> lock(playlist_mutex);
                    playlist.push_back(filename);
                    cout << "Dodano utwor: " << filename << " (razem: " << playlist.size() << ")\n";
                } 

                string msg = "OK; Dodano utwor.\n";
                send(sock, msg.c_str(), msg.length(), 0);
            }
        }
        else if (command == "EXIT") {
            break;
        }
        else {
            string msg = "ERROR; Nieznana komenda.\n";
            send(sock, msg.c_str(), msg.length(), 0);
        }
    }

    cout << "Watek: Klient " << sock << " rozlaczony." << endl;
    close(sock);
}

int main() {
    int server_fd;
    struct sockaddr_in address{}; 
    int addrlen = sizeof(address);

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    cout << "Serwer gotowy na porcie " << PORT << "..." << endl;

    while (true) {
        int new_sock;
        if ((new_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        thread client_thread(handle_client, new_sock);
        client_thread.detach(); 
    }
    
    return 0;
}