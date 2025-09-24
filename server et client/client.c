#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <winsock2.h>
#include <ctype.h>

#pragma comment(lib,"ws2_32.lib") // pour MSVC

// Fonction pour demander un choix valide entre min et max
int demander_choix(int min, int max) {
    char buffer[16];
    int choix;
    while (1) {
        if (!fgets(buffer, sizeof(buffer), stdin)) continue;
        buffer[strcspn(buffer, "\n")] = 0;
        int valide = 1;
        for (int i = 0; buffer[i]; i++) {
            if (!isdigit(buffer[i])) { valide = 0; break; }
        }
        if (!valide) { printf("Erreur: saisie invalide. Reessayez : "); continue; }
        choix = atoi(buffer);
        if (choix < min || choix > max) { printf("Erreur: choisissez entre %d et %d. Ressayez : ", min, max); continue; }
        return choix;
    }
}

int main() {
    WSADATA wsa;
    struct sockaddr_in server;
    char message[1024], username[50], password[50];
    int choix;

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Erreur Winsock: %d\n", WSAGetLastError());
        return 1;
    }

    // Configuration du serveur
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr("127.0.0.1");
    server.sin_port = htons(12345);

    while (1) {
        printf("\n===== MENU =====\n");
        printf("1. Connexion\n");
        printf("2. Inscription\n");
        printf("3. Quitter\n");
        printf("Choisissez une option : ");
        choix = demander_choix(1,3);

        if (choix == 3) {
            printf("Au revoir !\n");
            break;
        }

        printf("Nom d'utilisateur : ");
        fgets(username, sizeof(username), stdin);
        username[strcspn(username, "\n")] = 0;

        printf("Mot de passe : ");
        fgets(password, sizeof(password), stdin);
        password[strcspn(password, "\n")] = 0;

        // --- Création d'une nouvelle socket pour cette session ---
        SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
        if (s == INVALID_SOCKET) {
            printf("Erreur socket: %d\n", WSAGetLastError());
            continue;
        }

        if (connect(s, (struct sockaddr *)&server, sizeof(server)) < 0) {
            printf("Impossible de se connecter au serveur\n");
            closesocket(s);
            continue;
        }

        if (choix == 1)
            sprintf(message, "CONNEXION:%s:%s", username, password);
        else
            sprintf(message, "INSCRIPTION:%s:%s", username, password);

        send(s, message, strlen(message), 0);

        char resp[512];
        int r = recv(s, resp, sizeof(resp)-1, 0);
        if (r > 0) {
            resp[r] = '\0';
            printf("Serveur: %s\n", resp);

            if (strncmp(resp,"Connexion OK",12)==0) {
                printf("=== Mode chat, tapez 'quit' pour sortir ===\n");
                while (1) {
                    char text[512];
                    printf("> ");
                    fgets(text, sizeof(text), stdin);
                    text[strcspn(text, "\n")] = 0;

                    if (strcmp(text,"quit")==0) {
                        send(s, "quit", 4, 0);
                        int rchat = recv(s, resp, sizeof(resp)-1, 0);
                        if (rchat > 0) {
                            resp[rchat] = '\0';
                            printf("%s\n", resp); // affichera "Deconnexion OK"
                        }
                        break;
                    }

                    send(s, text, strlen(text), 0);
                }
            }
        }

        closesocket(s); // <- fermer la socket après chaque session
    }

    WSACleanup();
    return 0;
}
