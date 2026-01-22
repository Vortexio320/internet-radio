#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

#define PORT 8080
#define BUFFER_SIZE 1024

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    char *hello = "Witaj! Tu Twoj serwer radiowy.\n";

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Blad tworzenia gniazda");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Blad bindowania");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 3) < 0) {
        perror("Blad listen");
        exit(EXIT_FAILURE);
    }

    printf("Serwer nasluchuje na porcie %d...\n", PORT);

    if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
        perror("Blad accept");
        exit(EXIT_FAILURE);
    }

    printf("Nowe polaczenie nawiazane!\n");

    // 6. Wyslanie powitania
    send(new_socket, hello, strlen(hello), 0);
    printf("Powitanie wyslane.\n");

    // 7. Zamkniecie polaczenia
    close(new_socket);
    close(server_fd);

    return 0;
}