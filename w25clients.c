#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define SERVER_IP "127.0.0.1"    // Server (S1) IP address - local machine
#define PORT 6500                // S1's listening port
#define BUFFER_SIZE 2048         // Size of buffer used for communication

// Function to upload a local file to the server (S1)
void upload_file(int sockfd, char* filename, char* destination) {
    FILE *fp = fopen(filename, "rb");  // Open the file in read-binary mode
    if (fp == NULL) {
        printf("Error: Could not open file '%s'\n", filename);
        return;
    }

    // Construct the command to send to server: uploadf <filename> <destination>
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "uploadf %s %s", filename, destination);
    send(sockfd, command, strlen(command), 0);  // Send uploadf command to server

    // Wait for server to reply with "OK" before sending file
    char buffer[BUFFER_SIZE];
    int bytes = recv(sockfd, buffer, sizeof(buffer), 0);  // Read server response
    buffer[bytes] = '\0';

    // If server doesn't respond with OK, cancel upload
    if (strncmp(buffer, "OK", 2) != 0) {
        printf("Server error: %s\n", buffer);
        fclose(fp);
        return;
    }

    // Start sending file content in chunks until EOF
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(sockfd, buffer, bytes, 0);
    }

    // Notify server that file transmission is done
    send(sockfd, "EOF", 3, 0);
    fclose(fp);  // Close the local file

    // Receive final confirmation message from server
    bytes = recv(sockfd, buffer, sizeof(buffer), 0);
    buffer[bytes] = '\0';
    printf("%s\n", buffer);  // Print server’s acknowledgment
}

// Function to download a specific file from server
void download_file(int sockfd, char* filename) {
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "downlf %s", filename);  // Build command
    send(sockfd, command, strlen(command), 0);  // Send to server

    char buffer[BUFFER_SIZE];
    int bytes = recv(sockfd, buffer, sizeof(buffer), 0);  // Get server response

    // If server says NOTFOUND, file doesn’t exist
    if (bytes == 8 && strncmp(buffer, "NOTFOUND", 8) == 0) {
        printf("File '%s' not found on server.\n", filename);
        return;
    }

    // Open file locally to save contents
    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) {
        printf("Error: Could not create file '%s'\n", filename);
        return;
    }

    // Write first received chunk to file
    fwrite(buffer, 1, bytes, fp);

    // Keep receiving and writing until "EOF" received
    while ((bytes = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (bytes == 3 && strncmp(buffer, "EOF", 3) == 0) break;
        fwrite(buffer, 1, bytes, fp);
    }

    fclose(fp);  // Close the downloaded file
    printf("File '%s' downloaded successfully.\n", filename);
}

// Function to request and download a tarball based on extension (.c, .pdf, .txt)
void download_tar(int sockfd, char* extension) {
    char command[BUFFER_SIZE];
    snprintf(command, sizeof(command), "downltar %s", extension);  // Form the downltar command
    send(sockfd, command, strlen(command), 0);  // Send to server
    printf(" Sent downltar command: %s\n", command);

    char save_as[256];

    // Choose tar file name based on extension
    if (strcmp(extension, ".c") == 0)
        strcpy(save_as, "cfiles.tar");
    else if (strcmp(extension, ".pdf") == 0)
        strcpy(save_as, "pdf.tar");
    else if (strcmp(extension, ".txt") == 0)
        strcpy(save_as, "text.tar");
    else {
        printf("[ERROR] Unsupported extension: %s\n", extension);
        return;
    }

    // Save to /tmp folder for safety
    char full_path[BUFFER_SIZE];
    snprintf(full_path, sizeof(full_path), "%s/w25downloads/%s", getenv("HOME"), save_as);  // e.g., /tmp/pdf.tar

    printf(" Writing %s to %s\n", extension, full_path);

    FILE* fp = fopen(full_path, "wb");  // Open file in /tmp for writing
    if (!fp) {
        perror("[ERROR] fopen failed");
        return;
    }

    printf(" fopen path: %s\n", full_path);

    char buffer[BUFFER_SIZE];
    int bytes, total_written = 0;

    // Receive tarball contents in chunks
    while ((bytes = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (bytes == 3 && memcmp(buffer, "EOF", 3) == 0) break;  // Stop when EOF received
        fwrite(buffer, 1, bytes, fp);  // Write to local file
        total_written += bytes;
    }

    fflush(fp);              // Flush buffer to disk
    fsync(fileno(fp));       // Ensure it's physically written
    fclose(fp);              // Close file
    printf("[DEBUG] File closed successfully at: %s\n", full_path);

    // Check if file exists and show its size
    struct stat st;
    if (stat(full_path, &st) == 0) {
        printf(" Verified file EXISTS after fclose()\n");
        printf(" File size on disk: %ld bytes\n", st.st_size);
    } else {
        perror("[ERROR] stat failed after close()");
    }

    printf(" Total bytes received: %d\n", total_written);

    // Confirm to user if tar was downloaded successfully
    if (stat(full_path, &st) == 0 && st.st_size > 0) {
        printf("[Client] %s downloaded and saved at /tmp. You may verify with:\n", save_as);
        printf("         ls -lh %s\n         tar -tf %s\n", full_path, full_path);
    } else {
        printf("[ERROR] File %s not found or size = 0 bytes.\n", full_path);
    }

    printf(" Completed download_tar for %s\n", extension);
}

// Entry point of the client program
int main() {
    int sockfd;
    struct sockaddr_in server_addr;
    char input[BUFFER_SIZE];

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // Prepare server address struct
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);  // Set server port
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);  // Convert IP from text to binary

    // Connect to the server (S1)
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(1);
    }

    // Show client path info
    char cwd[BUFFER_SIZE];
    getcwd(cwd, sizeof(cwd));  // Get working directory of client
    printf("Connected to S1 at %s:%d\n", SERVER_IP, PORT);
    printf("[DEBUG] Client running in: %s\n", cwd);

    // Begin interactive command loop
    while (1) {
        printf("w25clients$ ");  // Command-line prompt
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) break;  // Exit if input fails
        input[strcspn(input, "\n")] = '\0';  // Remove newline character

        // Handle uploadf command
        if (strncmp(input, "uploadf", 7) == 0) {
            char filename[256], path[512];
            if (sscanf(input, "uploadf %s %s", filename, path) == 2) {
                upload_file(sockfd, filename, path);
            } else {
                printf("Usage: uploadf <filename> <~S1/S2/S3/S4/path>\n");
            }

        // Handle downlf command
        } else if (strncmp(input, "downlf", 6) == 0) {
            char filename[256];
            if (sscanf(input, "downlf %s", filename) == 1) {
                download_file(sockfd, filename);
            } else {
                printf("Usage: downlf <filename>\n");
            }

        // Handle downltar command
        } else if (strncmp(input, "downltar", 8) == 0) {
            char extension[10];
            if (sscanf(input, "downltar %s", extension) == 1) {
                download_tar(sockfd, extension);
            } else {
                printf("Usage: downltar <.c/.pdf/.txt>\n");
            }

        // Handle quit command
        } else if (strcmp(input, "quit") == 0) {
            break;

        // Any other command is sent directly to the server
        } else {
            send(sockfd, input, strlen(input), 0);  // Send command
            int bytes = recv(sockfd, input, sizeof(input), 0);  // Get response
            input[bytes] = '\0';
            printf("%s\n", input);  // Print response
        }
    }

    close(sockfd);  // Close the socket before exiting
    return 0;
}
