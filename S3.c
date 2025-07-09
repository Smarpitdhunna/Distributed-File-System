// S3.c
// This server handles all .txt files forwarded by S1.
// It supports:
// - Receiving and saving uploaded .txt files (uploadf)
// - Sending a requested file or a tarball (downlf, downltar)
// - Listing all .txt files stored here (dispfnames)
// - Removing a specific .txt file (removef)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/wait.h>

#define PORT 6502               // Port where S3 listens
#define BUFFER_SIZE 2048        // Size for data buffers

// --------------------------------------------------
// Creates folder hierarchy under ~/S3 before saving file
// e.g., ~/S3/folder1/folder2 will be created as needed

void create_directories(const char* path) {
    char temp[BUFFER_SIZE];
    snprintf(temp, sizeof(temp), "%s", path);  // Copy original path to modify
    char *p = temp + 1;

    // Walk through slashes to create nested folders
    while ((p = strchr(p, '/')) != NULL) {
        *p = '\0';              // Temporarily terminate path here
        mkdir(temp, 0755);      // Try to create this part
        *p = '/';               // Restore slash
        p++;                    // Move pointer forward
    }

    mkdir(temp, 0755);          // Final mkdir for the whole path
}

// --------------------------------------------------
// Receives a file from S1 and saves it under ~/S3/...
// The dest_path includes folder hierarchy

void receive_file(int sockfd, const char* filename, const char* dest_path) {
    char base_path[BUFFER_SIZE];

    // Skip "~S3" and append path to ~/S3
    snprintf(base_path, sizeof(base_path), "%s/S3/%s", getenv("HOME"), dest_path + 4);
    create_directories(base_path);  // Ensure directory exists

    // Construct full file path
    char full_path[BUFFER_SIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", base_path, filename);

    FILE *fp = fopen(full_path, "wb");  // Open file for writing
    if (!fp) {
        perror("[S3] File open error");
        return;
    }

    send(sockfd, "OK", 2, 0);  // Acknowledge ready to receive

    char buffer[BUFFER_SIZE];
    int bytes;

    // Read chunks from socket and write to file
    while ((bytes = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (bytes == 3 && strncmp(buffer, "EOF", 3) == 0) break;  // Check for end of file
        fwrite(buffer, 1, bytes, fp);  // Write to file
    }

    fclose(fp);
    printf("[S3] File '%s' saved at %s\n", filename, full_path);
}

// --------------------------------------------------
// Sends a requested .txt file to S1 for download

void send_file(int sockfd, const char* filename) {
    char file_path[BUFFER_SIZE];
    snprintf(file_path, sizeof(file_path), "%s/S3/%s", getenv("HOME"), filename);  // Build path

    FILE *fp = fopen(file_path, "rb");  // Open requested file
    if (!fp) {
        send(sockfd, "NOTFOUND", 8, 0);  // File not found
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytes;

    // Read from file and send over socket
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
        send(sockfd, buffer, bytes, 0);

    send(sockfd, "EOF", 3, 0);  // Signal end of file
    fclose(fp);

    printf("[S3] Sent file '%s' to S1\n", filename);
}

// --------------------------------------------------
// Creates a tarball (text.tar) of all .txt files in ~/S3
// Then sends it to S1

void send_text_tar(int sockfd) {
    printf("[S3] Preparing text.tar for download...\n");

    // Generate list.txt containing all .txt files
    char cmd[BUFFER_SIZE];
    snprintf(cmd, sizeof(cmd), "find %s/S3 -type f -name \"*.txt\" > list.txt", getenv("HOME"));
    system(cmd);

    // Fork to run tar creation
    pid_t pid = fork();
    if (pid == 0) {
        // In child process: create tarball from list
        execlp("tar", "tar", "-cf", "text.tar", "-T", "list.txt", NULL);
        perror("execlp failed");
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);  // Parent waits for tar to finish

        // If tar creation failed, notify S1
        if (access("text.tar", F_OK) != 0) {
            send(sockfd, "NOTFOUND", 8, 0);
            printf("[S3] Tar creation failed.\n");
            return;
        }
    }

    // Open the tarball for sending
    FILE* fp = fopen("text.tar", "rb");
    if (!fp) {
        send(sockfd, "NOTFOUND", 8, 0);
        return;
    }

    char outbuf[BUFFER_SIZE];
    int bytes;

    // Send tarball in chunks
    while ((bytes = fread(outbuf, 1, sizeof(outbuf), fp)) > 0)
        send(sockfd, outbuf, bytes, 0);

    send(sockfd, "EOF", 3, 0);  // End of file signal
    fclose(fp);

    // Clean up temporary files
    remove("text.tar");
    remove("list.txt");

    printf("[S3] Sent text.tar to S1\n");
}

// --------------------------------------------------
// This function is triggered when S1 connects.
// It reads the command and routes to the appropriate function.

void handle_client(int sockfd) {
    char buffer[BUFFER_SIZE];
    int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);  // Read command from S1
    if (bytes <= 0) return;

    buffer[bytes] = '\0';  // Null terminate
    printf("[S3] Command received: %s\n", buffer);

    // Upload command from S1
    if (strncmp(buffer, "uploadf", 7) == 0) {
        char filename[256], path[512];
        if (sscanf(buffer, "uploadf %s %s", filename, path) == 2) {
            receive_file(sockfd, filename, path);
        }

    // Download command for single file or tar
    } else if (strncmp(buffer, "downlf", 6) == 0) {
        char filename[256];
        if (sscanf(buffer, "downlf %s", filename) == 1) {
            if (strcmp(filename, "text.tar") == 0)
                send_text_tar(sockfd);  // Send tarball
            else
                send_file(sockfd, filename);  // Send individual file
        }

    // Request for tarball download
    } else if (strncmp(buffer, "downltar", 8) == 0) {
        char ext[16];
        if (sscanf(buffer, "downltar %s", ext) == 1) {
            if (strcmp(ext, ".txt") == 0)
                send_text_tar(sockfd);
            else
                send(sockfd, "Unsupported extension", 22, 0);
        }

    // Request to display all stored .txt files
    } else if (strncmp(buffer, "dispfnames", 10) == 0) {
        char path[512];
        if (sscanf(buffer, "dispfnames %s", path) == 1) {
            char cmd[BUFFER_SIZE];

            // Build a path-aware find command that lists only .txt files in the given subpath
            snprintf(cmd, sizeof(cmd),
                "find %s/S3/%s -type f -name \"*.txt\" -printf \"%%f\\n\" | sort 2>/dev/null",
                getenv("HOME"), path);

            FILE* fp = popen(cmd, "r");
            char out[BUFFER_SIZE];

            // Send each filename line by line to S1
            while (fgets(out, sizeof(out), fp)) {
                send(sockfd, out, strlen(out), 0);
            }

            send(sockfd, "EOF", 3, 0);  // End-of-list signal
            pclose(fp);
        } else {
            send(sockfd, "Usage: dispfnames <foldername>\n", 32, 0);
        }

    // File delete command
    } else if (strncmp(buffer, "removef", 7) == 0) {
        char filename[256];
        if (sscanf(buffer, "removef %s", filename) == 1) {
            char cmd[BUFFER_SIZE], filepath[BUFFER_SIZE];
            // Locate the file using find
            snprintf(cmd, sizeof(cmd), "find %s/S3 -type f -name \"%s\" 2>/dev/null", getenv("HOME"), filename);
            FILE* fp = popen(cmd, "r");

            // If found, attempt to remove
            if (fp && fgets(filepath, sizeof(filepath), fp)) {
                filepath[strcspn(filepath, "\n")] = 0;  // Remove newline char

                if (remove(filepath) == 0) {
                    send(sockfd, "REMOVED", 7, 0);
                    printf("[S3] Removed file: %s\n", filepath);
                } else {
                    send(sockfd, "NOTFOUND", 8, 0);
                }
            } else {
                send(sockfd, "NOTFOUND", 8, 0);
            }

            if (fp) pclose(fp);
        }
    }

    close(sockfd);  // Close connection after handling
}

// --------------------------------------------------
// Main server loop that runs forever
// Accepts client connections (from S1) and spawns handler

int main() {
    int server_sock, client_sock;
    struct sockaddr_in addr, cli_addr;
    socklen_t addr_size;

    server_sock = socket(AF_INET, SOCK_STREAM, 0);  // Create TCP socket
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;  // Accept any incoming IP

    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));  // Bind to port
    listen(server_sock, 5);  // Start listening
    printf("[S3] Server listening on port %d...\n", PORT);

    // Infinite loop: wait for S1 to connect
    while (1) {
        addr_size = sizeof(cli_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&cli_addr, &addr_size);  // Accept new connection
        printf("[S3] Connection from %s\n", inet_ntoa(cli_addr.sin_addr));
        handle_client(client_sock);  // Process the command
    }

    return 0;
}
