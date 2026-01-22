#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8080

void *handle_client(void *socket_desc) {
    int sock = *(int*)socket_desc;
    
    free(socket_desc);

    char *message = "Witaj! Jestes podlaczony do serwera wielowatkowego.\n";
    send(sock, message, strlen(message), 0);
    
    printf("Watek: Obsluzylem klienta na sockecie %d.\n", sock);

    //10 sekund sluchania radia, potem bedzie rozwiniete
    sleep(10); 

    printf("Watek: Klient rozlaczony.\n");
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

    printf("Serwer wielowatkowy gotowy na porcie %d...\n", PORT);

    while (1) {
        new_sock = malloc(sizeof(int));
        
        if ((*new_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            free(new_sock);
            continue;
        }

        printf("Main: Nowe polaczenie! Tworze watek...\n");

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Nie udalo sie utworzyc watku");
            free(new_sock);
            continue;
        }

        pthread_detach(thread_id);
    }

    return 0;
}