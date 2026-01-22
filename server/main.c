#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080
#define BUFFER_SIZE 1024
#define MAX_SONGS 100
#define MAX_FILENAME 100

char playlist[MAX_SONGS][MAX_FILENAME];
int song_count = 0;

pthread_mutex_t playlist_mutex = PTHREAD_MUTEX_INITIALIZER;



void *handle_client(void *socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);

    char buffer[BUFFER_SIZE];
    int bytes_read;

    printf("Watek: Klient %d podlaczony. Czekam na komendy.\n", sock);
    
    //instrukcja dla klienta
    char *help = "Witaj! Komendy: ADD <nazwa>, LIST, EXIT\n";
    send(sock, help, strlen(help), 0);

    //petla do komunikacji z klientem
    while ((bytes_read = recv(sock, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[bytes_read] = '\0';
        buffer[strcspn(buffer, "\r\n")] = 0;
        printf("Klient %d wyslal: %s\n", sock, buffer);

        if (strncmp(buffer, "LIST", 4) == 0) {
            pthread_mutex_lock(&playlist_mutex);
            
            char response[BUFFER_SIZE * 4] = "";
            if (song_count == 0) {
                strcat(response, "Playlista jest pusta.\n");
            } else {
                strcat(response, "--- PLAYLISTA ---\n");
                for (int i = 0; i < song_count; i++) {
                    char line[MAX_FILENAME + 32];
                    snprintf(line, sizeof(line), "%d. %s\n", i + 1, playlist[i]);
                    strcat(response, line);
                }
            }
            
            pthread_mutex_unlock(&playlist_mutex);

            send(sock, response, strlen(response), 0);
        }
        else if (strncmp(buffer, "ADD ", 4) == 0) {
            char *filename = buffer + 4; //nazwa pliku od 5 znaku linii
            
            if (strlen(filename) > 0) {
                pthread_mutex_lock(&playlist_mutex);
                
                if (song_count < MAX_SONGS) {
                    strncpy(playlist[song_count], filename, MAX_FILENAME);
                    song_count++;
                    char *msg = "OK; Dodano utwor.\n";
                    send(sock, msg, strlen(msg), 0);
                    printf("Dodano utwor: %s (razem: %d)\n", filename, song_count);
                } else {
                    char *msg = "ERROR; Kolejka pelna.\n";
                    send(sock, msg, strlen(msg), 0);
                }

                pthread_mutex_unlock(&playlist_mutex);
            }
        }
        else if (strcmp(buffer, "EXIT") == 0) {
            break;
        }
        else {
            char *msg = "ERROR; Nieznana komenda.\n";
            send(sock, msg, strlen(msg), 0);
        }
    }

    printf("Watek: Klient %d rozlaczony.\n", sock);
    close(sock);
    return NULL;
}

int main() {
    int server_fd, *new_sock;
    struct sockaddr_in address;
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

    printf("Serwer (Kolejka + Mutex) gotowy na porcie %d...\n", PORT);

    while (1) {
        new_sock = malloc(sizeof(int));
        if ((*new_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            free(new_sock);
            continue;
        }

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Thread create failed");
            free(new_sock);
            continue;
        }
        pthread_detach(thread_id);
    }
    
    pthread_mutex_destroy(&playlist_mutex); //just in case
    return 0;
}