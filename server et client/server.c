#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include <ctype.h>   

#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"advapi32.lib")

#ifndef CALG_SHA_256
#define CALG_SHA_256 0x0000800c
#endif

#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024

typedef struct {
    SOCKET sock;
    int connected;
    char username[50];
} Client;

// --- SHA-256 ---
void sha256_hex(const char *input, char *out_hex)
{
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE hash[32];
    DWORD hashLen = 32;

    if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) return;
    if (!CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) { CryptReleaseContext(hProv,0); return; }
    CryptHashData(hHash, (BYTE*)input, (DWORD)strlen(input), 0);
    CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv,0);

    for (DWORD i=0;i<hashLen;i++) sprintf(out_hex + i*2, "%02x", hash[i]);
    out_hex[64] = '\0';
}

DWORD WINAPI alert_thread(LPVOID arg) {
    Client *clients = (Client*)arg;
    const char *alert_msg = "\033[31m[ALERTE] Ultron attaquera bientot preparez-vous !!!\033[0m\n";
    // \033[31m = rouge, \033[0m = reset
    for (;;) {
        Sleep(120000); // 600 000 ms = 10 minutes
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].connected) {
                send(clients[i].sock, alert_msg, (int)strlen(alert_msg), 0);
            }
        }
        printf("Alerte envoyee a tous les clients.\n");
    }
    return 0;
}


// --- Vérifie si l'utilisateur existe déjà ---
int user_exists(const char *username) {
    FILE *f = fopen("users.json", "r");
    if (!f) return 0;

    char line[512];
    int exists = 0;
    while (fgets(line, sizeof(line), f)) {
        char u[50];
        if (sscanf(line, " { \"username\" : \"%49[^\"]\"", u) == 1 ||
            sscanf(line, "{\"username\":\"%49[^\"]\"", u) == 1) {
            if (strcmp(u, username) == 0) {
                exists = 1;
                break;
            }
        }
    }
    fclose(f);
    return exists;
}

// --- Ajout utilisateur ---
void add_user(const char *user, const char *pass)
{
    char hashpass[65];
    sha256_hex(pass, hashpass);

    FILE *f = fopen("users.json", "r+");
    if (!f) {
        f = fopen("users.json", "w");
        if (!f) { perror("users.json"); return; }
        fprintf(f, "[\n{\"username\":\"%s\",\"password\":\"%s\"}\n]", user, hashpass);
        fclose(f);
        return;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 2) {
        fseek(f, 0, SEEK_SET);
        fprintf(f, "[\n{\"username\":\"%s\",\"password\":\"%s\"}\n]", user, hashpass);
        fclose(f);
        return;
    }

    long pos = size - 1;
    int ch;
    do {
        fseek(f, pos--, SEEK_SET);
        ch = fgetc(f);
    } while (pos >= 0 && isspace(ch));

    fseek(f, size - 1, SEEK_SET);

    if (ch != '[') {
        fseek(f, -1, SEEK_END);
        fprintf(f, ",\n");
    } else {
        fseek(f, -1, SEEK_END);
        fprintf(f, "\n");
    }

    fprintf(f, "{\"username\":\"%s\",\"password\":\"%s\"}\n]", user, hashpass);
    fflush(f);
    fclose(f);
}

// --- Vérifie user + password ---
int check_user(const char *user, const char *pass)
{
    char hashpass[65];
    sha256_hex(pass, hashpass);

    FILE *f = fopen("users.json", "r");
    if (!f) return 0;
    char line[512];
    int ok = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, user) && strstr(line, hashpass)) { ok = 1; break; }
    }
    fclose(f);
    return ok;
}

int main()
{
    WSADATA wsa;
    SOCKET server_socket, new_socket;
    struct sockaddr_in server, client;
    int max_sd, activity, i, valread, c;
    char buffer[BUFFER_SIZE];
    fd_set readfds;

    Client clients[MAX_CLIENTS];
    for (i = 0; i < MAX_CLIENTS; i++) { clients[i].sock = 0; clients[i].connected = 0; clients[i].username[0]='\0'; }

    CreateThread(NULL, 0, alert_thread, clients, 0, NULL); //Créer le Thread pour envoyer l'annonce ultron

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) { printf("Erreur WSAStartup : %d\n", WSAGetLastError()); return 1; }

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == INVALID_SOCKET) { printf("Erreur socket\n"); WSACleanup(); return 1; }

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(12345);

    if (bind(server_socket, (struct sockaddr *)&server, sizeof(server)) == SOCKET_ERROR) { printf("Bind echoue\n"); closesocket(server_socket); WSACleanup(); return 1; }

    listen(server_socket, 5);
    printf("Serveur en attente de connexion...\n");
    c = sizeof(struct sockaddr_in);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(server_socket, &readfds);
        max_sd = server_socket;

        for (i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].sock > 0) FD_SET(clients[i].sock, &readfds);
            if (clients[i].sock > max_sd) max_sd = clients[i].sock;
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
        if (activity < 0) { printf("select erreur\n"); continue; }

        if (FD_ISSET(server_socket, &readfds)) {
            new_socket = accept(server_socket, (struct sockaddr *)&client, &c);
            if (new_socket == INVALID_SOCKET) { printf("accept erreur\n"); continue; }
            printf("Nouvelle connexion: socket %d\n", new_socket);

            for (i = 0; i < MAX_CLIENTS; i++) {
                if (clients[i].sock == 0) {
                    clients[i].sock = new_socket;
                    clients[i].connected = 0;
                    clients[i].username[0] = '\0';
                    break;
                }
            }
        }

for (i = 0; i < MAX_CLIENTS; i++) {
    SOCKET sd = clients[i].sock;
    if (sd > 0 && FD_ISSET(sd, &readfds)) {
        valread = recv(sd, buffer, BUFFER_SIZE-1, 0);
            if (valread <= 0 || strncmp(buffer, "quit", 4) == 0) {
                if (valread > 0 && strncmp(buffer, "quit", 4) == 0)
                    send(sd, "Deconnexion OK\n", 15, 0);

                printf("Client '%s' (socket %d) deconnecte\n", clients[i].username, clients[i].sock);
                closesocket(clients[i].sock);
                clients[i].sock = 0;
                clients[i].connected = 0;
                clients[i].username[0] = '\0';
                continue;
            } else {
            buffer[valread] = '\0';

            // --- Traitement des commandes INSCRIPTION/CONNEXION ---
            if (strncmp(buffer, "INSCRIPTION:", 12) == 0) {
                char u[50], p[50];
                if (sscanf(buffer + 12, "%49[^:]:%49s", u, p) == 2) {
                    if (user_exists(u)) {
                        send(sd, "Erreur: l'utilisateur existe deja\n", 34, 0);
                        printf("Tentative inscription: utilisateur %s existe deja\n", u);
                    } else {
                        add_user(u, p);
                        send(sd, "Inscription OK\n", 15, 0);
                        printf("Nouvel utilisateur cree: %s\n", u);
                    }
                } else {
                    send(sd, "Erreur format INSCRIPTION\n", 27, 0);
                }
                continue;
            } 
            else if (strncmp(buffer, "CONNEXION:", 10) == 0) {
                char u[50], p[50];

                if (sscanf(buffer+10, "%49[^:]:%49s", u, p) == 2) {
                    if (check_user(u, p)) {
                        clients[i].connected = 1;
                        strcpy(clients[i].username, u);
                        send(sd, "Connexion OK\n", 13, 0);
                        printf("Connexion reussie pour %s\n", u);
                    } else {
                        clients[i].connected = 0;
                        send(sd, "Echec connexion\n", 16, 0);
                        printf("Connexion echouee pour %s\n", u);
                    }
                } else {
                    send(sd, "Erreur format CONNEXION\n", 25, 0);
                }
                continue; // commande traitée, passer au suivant
            }

            // --- Si le client est connecté, tout le reste est chat ---
            if (clients[i].connected && buffer[0] != '\0') {
                // Messages globaux : msgall:texte
                if (strncmp(buffer, "msgall:", 7) == 0) {
                    char *msg = buffer + 7;
                    char finalmsg[1024];
                    sprintf(finalmsg, "[%s -> ALL] %s\n", clients[i].username, msg);
                    printf("%s", finalmsg);
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j].connected && clients[j].sock != clients[i].sock) {
                            send(clients[j].sock, finalmsg, strlen(finalmsg), 0);
                        }
                    }
                }
                // Messages privés : msg:dest:texte
                else if (strncmp(buffer, "msg:", 4) == 0) {
                    char dest[50], msg[512];
                    if (sscanf(buffer + 4, "%49[^:]:%511[^\n]", dest, msg) == 2) {
                        int found = 0;
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (clients[j].connected && strcmp(clients[j].username, dest) == 0) {
                                char fullmsg[1024];
                                sprintf(fullmsg, "[prive de %s] %s\n", clients[i].username, msg);
                                send(clients[j].sock, fullmsg, strlen(fullmsg), 0);
                                printf("[%s -> %s] %s\n", clients[i].username, dest, msg);
                                found = 1;
                                break;
                            }
                        }
                        if (!found) {
                            char err[] = "Erreur: utilisateur non trouve\n";
                            send(clients[i].sock, err, strlen(err), 0);
                        }
                    }
                }
                // Message normal (ou log)
                else {
                    printf("[%s] Message: %s\n", clients[i].username, buffer);
                }
            }
            }
        } 
    }
}

    closesocket(server_socket);
    WSACleanup();
    return 0;
}
