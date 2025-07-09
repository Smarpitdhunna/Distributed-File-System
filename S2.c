// S2.c
// Handles .pdf files sent from S1: uploadf, downlf, dispfnames, and removef

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define PORT 6501
#define BUFFER_SIZE 2048

// Recursively create folders (like mkdir -p)
void create_directories(const char* path) {
    char temp[BUFFER_SIZE];
    snprintf(temp, sizeof(temp), "%s", path);  // Copy path to a temp buffer

    char *p = temp + 1;
    while ((p = strchr(p, '/')) != NULL) {
        *p = '\0';
        mkdir(temp, 0755);  // Create intermediate folder if it doesn't exist
        *p = '/';
        p++;
    }

    mkdir(temp, 0755);  // Create the final directory
}

// Receives a file from S1 and stores it in the specified path
void receive_file(int sockfd, const char* filename, const char* dest_path) {
    // Construct the full directory path by removing ~S2 and adding HOME
    char base_path[BUFFER_SIZE];
    snprintf(base_path, sizeof(base_path), "%s/S2/%s", getenv("HOME"), dest_path + 4);

    create_directories(base_path);  // Ensure directory exists

    // Append filename to base path to get full file path
    char full_path[BUFFER_SIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, filename);

    // Open file to write
    FILE *fp = fopen(full_path, "wb");
    if (!fp) {
        perror("[S2] File open error");
        return;
    }

    send(sockfd, "OK", 2, 0);  // Tell S1 to start sending file

    char buffer[BUFFER_SIZE];
    int bytes;

    // Receive data from socket and write to file until EOF is received
    while ((bytes = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (bytes == 3 && strncmp(buffer, "EOF", 3) == 0) break;
        fwrite(buffer, 1, bytes, fp);
    }

    fclose(fp);
    printf("[S2] File '%s' saved at %s\n", filename, full_path);
}

// Sends a file (used by 'downlf')
void send_file(int sockfd, const char* filename) {
    // Construct full path to file
    char file_path[BUFFER_SIZE];
    snprintf(file_path, sizeof(file_path), "%s/S2/%s", getenv("HOME"), filename);

    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        send(sockfd, "NOTFOUND", 8, 0);  // Let S1 know file is missing
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytes;

    // Read from file and send to S1
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(sockfd, buffer, bytes, 0);
    }

    send(sockfd, "EOF", 3, 0);  // Tell S1 file is done sending
    fclose(fp);

    printf("[S2] Sent file '%s' to S1\n", filename);
}

// Handles incoming commands from S1
// Handles incoming commands from S1
void handle_client(int sockfd) {
    char buffer[BUFFER_SIZE];

    int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) return;

    buffer[bytes] = '\0';
    printf("[S2] Command received: %s\n", buffer);

    // ---- Handle uploadf ----
    if (strncmp(buffer, "uploadf", 7) == 0) {
        char filename[256], path[512];
        if (sscanf(buffer, "uploadf %s %s", filename, path) == 2) {
            receive_file(sockfd, filename, path);
        }

    // ---- Handle downlf command ----
    } else if (strncmp(buffer, "downlf", 6) == 0) {
        char filename[256];
        if (sscanf(buffer, "downlf %s", filename) == 1) {
            send_file(sockfd, filename);
        }

    // ---- Handle downltar .pdf ----
    } else if (strncmp(buffer, "downltar", 8) == 0) {
        char ext[10];
        if (sscanf(buffer, "downltar %s", ext) == 1 && strcmp(ext, ".pdf") == 0) {
            printf("[S2] Preparing pdf.tar for downltar .pdf...\n");

            // Create list of all PDF files
            char find_cmd[BUFFER_SIZE];
            snprintf(find_cmd, sizeof(find_cmd), "find %s/S2 -type f -name \"*.pdf\" > list.txt", getenv("HOME"));
            system(find_cmd);

            pid_t pid = fork();
            if (pid == 0) {
                execlp("tar", "tar", "-cf", "pdf.tar", "-T", "list.txt", NULL);
                perror("execlp failed");
                exit(1);
            } else if (pid > 0) {
                int status;
                waitpid(pid, &status, 0);
                if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
                    send(sockfd, "Tar creation failed", 19, 0);
                    return;
                }
            }

            FILE* fp = fopen("pdf.tar", "rb");
            if (!fp) {
                send(sockfd, "NOTFOUND", 8, 0);
                return;
            }

            char buffer_out[BUFFER_SIZE];
            while ((bytes = fread(buffer_out, 1, sizeof(buffer_out), fp)) > 0) {
                send(sockfd, buffer_out, bytes, 0);
            }
            send(sockfd, "EOF", 3, 0);
            fclose(fp);
            system("rm pdf.tar list.txt");

            printf("[S2] Sent pdf.tar to S1 (from downltar .pdf)\n");
        }

    // ---- Handle dispfnames ----
    } else if (strncmp(buffer, "dispfnames", 10) == 0) {
        char path[512];
        if (sscanf(buffer, "dispfnames %s", path) == 1) {
            char cmd[BUFFER_SIZE];

            // Update the path to match ~/S2/folder (or ~/S3, ~/S4 in respective servers)
            snprintf(cmd, sizeof(cmd),
                "find %s/S2/%s -type f -name \"*.pdf\" -printf \"%%f\\n\" | sort 2>/dev/null", getenv("HOME"), path);

            FILE* fp = popen(cmd, "r");
            char out[BUFFER_SIZE];

            while (fgets(out, sizeof(out), fp))
                send(sockfd, out, strlen(out), 0);

            send(sockfd, "EOF", 3, 0);
            pclose(fp);
        } else {
            send(sockfd, "Usage: dispfnames <foldername>\n", 32, 0);
        }
    // ---- Handle removef ----
    } else if (strncmp(buffer, "removef", 7) == 0) {
        char filename[256];
        if (sscanf(buffer, "removef %s", filename) == 1) {
            char cmd[BUFFER_SIZE], filepath[BUFFER_SIZE];
            snprintf(cmd, sizeof(cmd), "find %s/S2 -type f -name \"%s\" 2>/dev/null", getenv("HOME"), filename);
            FILE* fp = popen(cmd, "r");
            if (fp && fgets(filepath, sizeof(filepath), fp)) {
                filepath[strcspn(filepath, "\n")] = 0;
                if (remove(filepath) == 0) {
                    send(sockfd, "REMOVED", 7, 0);
                    printf("[S2] Removed file: %s\n", filepath);
                } else {
                    send(sockfd, "NOTFOUND", 8, 0);
                    printf("[S2] Could not remove: %s\n", filepath);
                }
            } else {
                send(sockfd, "NOTFOUND", 8, 0);
            }
            if (fp) pclose(fp);
        }
    }

    close(sockfd);
}


// Main function to start S2 server
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
    printf("[S2] Server listening on port %d...\n", PORT);

    // Loop forever to handle incoming connections
    while (1) {
        addr_size = sizeof(cli_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&cli_addr, &addr_size);
        printf("[S2] Connection from %s\n", inet_ntoa(cli_addr.sin_addr));
        handle_client(client_sock);
    }

    return 0;
}
