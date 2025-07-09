// S4.c
// Handles .zip files sent from S1.
// Stores them in ~/S4/... and returns them on downlf request.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define PORT 6503
#define BUFFER_SIZE 2048

// Create folder structure recursively (like mkdir -p)
void create_directories(const char* path) {
    char temp[BUFFER_SIZE];
    snprintf(temp, sizeof(temp), "%s", path);
    char* p = temp + 1;

    while ((p = strchr(p, '/')) != NULL) {
        *p = '\0';
        mkdir(temp, 0755);  // create intermediate folder
        *p = '/';
        p++;
    }

    mkdir(temp, 0755);  // create final directory
}

// Receive a .zip file from S1 and store it under ~/S4/
void receive_file(int sockfd, const char* filename, const char* dest_path) {
    char base_path[BUFFER_SIZE];

    // Strip "~S4" and append relative path to $HOME/S4/
    snprintf(base_path, sizeof(base_path), "%s/S4/%s", getenv("HOME"), dest_path + 4);
    create_directories(base_path);  // ensure destination path exists

    // Create full file path
    char full_path[BUFFER_SIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, filename);

    FILE* fp = fopen(full_path, "wb");
    if (!fp) {
        perror("[S4] File open error");
        return;
    }

    send(sockfd, "OK", 2, 0);  // Confirm to S1 that we’re ready to receive

    char buffer[BUFFER_SIZE];
    int bytes;

    // Receive file data until EOF marker
    while ((bytes = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (bytes == 3 && strncmp(buffer, "EOF", 3) == 0) break;
        fwrite(buffer, 1, bytes, fp);
    }

    fclose(fp);
    printf("[S4] File '%s' saved at %s\n", filename, full_path);
}

// Sends requested .zip file to S1
void send_file(int sockfd, const char* filename) {
    char file_path[BUFFER_SIZE];
    snprintf(file_path, sizeof(file_path), "%s/S4/%s", getenv("HOME"), filename);

    FILE* fp = fopen(file_path, "rb");
    if (!fp) {
        send(sockfd, "NOTFOUND", 8, 0);  // file not found
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytes;

    // Send file content in chunks
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(sockfd, buffer, bytes, 0);
    }

    send(sockfd, "EOF", 3, 0);  // End-of-file marker
    fclose(fp);

    printf("[S4] Sent file '%s' to S1\n", filename);
}

// Process commands from S1
void handle_client(int sockfd) {
    char buffer[BUFFER_SIZE];
    int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) return;

    buffer[bytes] = '\0';
    printf("[S4] Command received: %s\n", buffer);

    // --- Handle uploadf command ---
    if (strncmp(buffer, "uploadf", 7) == 0) {
        char filename[256], path[512];
        if (sscanf(buffer, "uploadf %s %s", filename, path) == 2) {
            receive_file(sockfd, filename, path);
        }

    // --- Handle downlf command ---
    } else if (strncmp(buffer, "downlf", 6) == 0) {
        char filename[256];
        if (sscanf(buffer, "downlf %s", filename) == 1) {
            send_file(sockfd, filename);
        }

    // --- Handle dispfnames (list all .zip files in ~/S4) ---
    } else if (strncmp(buffer, "dispfnames", 11) == 0) {
        char cmd[BUFFER_SIZE];
        snprintf(cmd, sizeof(cmd), "find %s/S4 -type f -name \"*.zip\" 2>/dev/null", getenv("HOME"));
        FILE* fp = popen(cmd, "r");
        char out[BUFFER_SIZE];

        while (fgets(out, sizeof(out), fp)) {
            send(sockfd, out, strlen(out), 0);
        }

        send(sockfd, "EOF", 3, 0);  // mark end of list
        pclose(fp);

    // --- Handle removef (delete .zip file dynamically) ---
    } else if (strncmp(buffer, "removef", 7) == 0) {
        char filename[256];
        if (sscanf(buffer, "removef %s", filename) == 1) {
            char cmd[BUFFER_SIZE], filepath[BUFFER_SIZE];

            // Dynamically locate file using find
            snprintf(cmd, sizeof(cmd), "find %s/S4 -type f -name \"%s\" 2>/dev/null", getenv("HOME"), filename);
            FILE* fp = popen(cmd, "r");

            if (fp && fgets(filepath, sizeof(filepath), fp)) {
                filepath[strcspn(filepath, "\n")] = 0; // trim newline

                if (remove(filepath) == 0) {
                    send(sockfd, "REMOVED", 7, 0);
                    printf("[S4] Removed file: %s\n", filepath);
                } else {
                    send(sockfd, "NOTFOUND", 8, 0);
                    printf("[S4] Failed to remove: %s\n", filepath);
                }
            } else {
                send(sockfd, "NOTFOUND", 8, 0);
                printf("[S4] Could not find file: %s\n", filename);
            }

            if (fp) pclose(fp);
        }
    }

    close(sockfd);  // Done with this client
}

// Start server and accept connections from S1
int main() {
    int server_sock, client_sock;
    struct sockaddr_in addr, cli_addr;
    socklen_t addr_size;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);

    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_sock, 5);
    printf("[S4] Server listening on port %d...\n", PORT);

    while (1) {
        addr_size = sizeof(cli_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&cli_addr, &addr_size);
        printf("[S4] Connection from %s\n", inet_ntoa(cli_addr.sin_addr));
        handle_client(client_sock);
    }

    return 0;
}
