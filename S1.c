// S1.c
// Acts as the main server in the distributed file system.
// Handles commands: uploadf, downlf, dispfnames, removef, downltar.
// Routes files based on extension: .c (S1), .pdf (S2), .txt (S3), .zip (S4).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

// ----------------------------
// Configuration Constants
// ----------------------------
#define PORT 6500
#define BUFFER_SIZE 2048

// Port assignments for S2, S3, S4
#define S2_PORT 6501
#define S3_PORT 6502
#define S4_PORT 6503

// Localhost IPs
#define S2_IP "127.0.0.1"
#define S3_IP "127.0.0.1"
#define S4_IP "127.0.0.1"

// ----------------------------
// File mapping structure
// Used to track uploaded file names and their paths
// so that S1 knows where they are stored across S2/S3/S4
// ----------------------------
struct FileMap {
    char filename[256];            // Only the name of the file
    char relative_path[512];       // Full relative path to retrieve later
};

struct FileMap file_map[100];      // Static array to store 100 files max
int file_map_count = 0;            // Number of currently stored file mappings

// Save uploaded file path for lookup later
void save_file_path(const char* filename, const char* dest) {
    strcpy(file_map[file_map_count].filename, filename);
    // dest + 4 skips the "~S2", "~S3" prefix and starts after it (e.g., folder1/file.txt)
    snprintf(file_map[file_map_count].relative_path, sizeof(file_map[file_map_count].relative_path),
             "%s/%s", dest + 4, filename);
    file_map_count++;
}

// Search for a file's relative path in the mapping
const char* get_relative_path(const char* filename) {
    for (int i = 0; i < file_map_count; i++) {
        if (strcmp(file_map[i].filename, filename) == 0)
            return file_map[i].relative_path;
    }
    return NULL;
}

// ----------------------------
// Send a file to secondary server (S2/S3/S4)
// Used when user uploads a .pdf/.txt/.zip file
// ----------------------------
void send_to_secondary_server(const char* ip, int port, const char* filepath, const char* filename, const char* dest) {
    int sockfd;
    struct sockaddr_in addr;
    char buffer[BUFFER_SIZE];

    FILE* fp = fopen(filepath, "rb");
    if (!fp) return;  // File failed to open

    // Create socket and prepare connection details
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    // Connect to target server
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fclose(fp);
        return;
    }

    // Send upload command to S2/S3/S4
    snprintf(buffer, sizeof(buffer), "uploadf %s %s", filename, dest);
    send(sockfd, buffer, strlen(buffer), 0);
    recv(sockfd, buffer, sizeof(buffer), 0);  // Wait for "OK"

    // Send file contents in chunks
    int bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
        send(sockfd, buffer, bytes, 0);

    // Signal end of file
    send(sockfd, "EOF", 3, 0);
    fclose(fp);
    close(sockfd);

    // Remove local file after forwarding
    remove(filepath);
}

// ----------------------------
// Requesting file back from S2/S3/S4 (.pdf/.txt/.zip)
// Used in both downlf and downltar
// ----------------------------
void request_file_from_secondary(const char* ip, int port, const char* rel_path, const char* save_as) {
    int sockfd;
    struct sockaddr_in addr;
    char buffer[BUFFER_SIZE];

    FILE* fp = fopen(save_as, "wb");
    if (!fp) return;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        fclose(fp);
        return;
    }

    // Request the file
    snprintf(buffer, sizeof(buffer), "downlf %s", rel_path);
    send(sockfd, buffer, strlen(buffer), 0);

    // Receive file contents and write to local
    int bytes;
    while ((bytes = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
        if (bytes == 3 && strncmp(buffer, "EOF", 3) == 0) break;
        fwrite(buffer, 1, bytes, fp);
    }

    fclose(fp);
    close(sockfd);
}

// ----------------------------
// Send a local .c file directly from ~/S1 to client
// ----------------------------
void send_local_file(int client_sock, const char* filename) {
    char path[BUFFER_SIZE];
    const char* rel_path = get_relative_path(filename);
    if (!rel_path) {
    send(client_sock, "NOTFOUND", 8, 0);
    return;
}
snprintf(path, sizeof(path), "%s/S1/%s", getenv("HOME"), rel_path);


    FILE* fp = fopen(path, "rb");
    if (!fp) {
        send(client_sock, "NOTFOUND", 8, 0);
        return;
    }

    char buffer[BUFFER_SIZE];
    int bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
        send(client_sock, buffer, bytes, 0);

    send(client_sock, "EOF", 3, 0);
    fclose(fp);
}

// ----------------------------
// Send list of all files from S1, S2, S3, S4
// Sends back consolidated list to client
// ----------------------------
void handle_dispfnames(int client_sock, const char* pathname) {
    char msg[BUFFER_SIZE * 8] = "";
    char buffer[BUFFER_SIZE];

    // Collect .c files from ~/S1/pathname
    snprintf(buffer, sizeof(buffer), "find %s/S1/%s -type f -name \"*.c\" -printf \"%%f\\n\" | sort", getenv("HOME"), pathname);
    FILE* fp = popen(buffer, "r");
    if (fp) {
        while (fgets(buffer, sizeof(buffer), fp)) {
            strcat(msg, buffer);
        }
        pclose(fp);
    }

    // Define secondary servers and file extensions
    struct {
        char* ip;
        int port;
        char* ext;
    } servers[] = {
        {S2_IP, S2_PORT, ".pdf"},
        {S3_IP, S3_PORT, ".txt"},
        {S4_IP, S4_PORT, ".zip"}
    };

    // For each of S2/S3/S4, connect and request dispfnames <pathname>
    for (int i = 0; i < 3; i++) {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(servers[i].port);
        inet_pton(AF_INET, servers[i].ip, &addr.sin_addr);

        if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            // Send the correct dispfnames request to secondary server
            char cmd[BUFFER_SIZE];
            snprintf(cmd, sizeof(cmd), "dispfnames %s", pathname);
            send(sockfd, cmd, strlen(cmd), 0);

            // Append each line received to final msg
            while (1) {
                int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
                if (bytes <= 0) break;
                buffer[bytes] = '\0';
                if (strncmp(buffer, "EOF", 3) == 0) break;
                strcat(msg, buffer);
            }

            close(sockfd);
        }
    }

    // Send combined list to the client
    send(client_sock, msg, strlen(msg), 0);
}


// ----------------------------
// Handle removef command from client
// Deletes a file from the appropriate server based on file extension
// ----------------------------
void forward_removef_to_secondary(const char* ip, int port, const char* filename, int client_sock) {
    int sockfd;
    struct sockaddr_in addr;
    char buffer[BUFFER_SIZE];

    // Create a socket and set up connection parameters
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    // Try connecting to the appropriate secondary server
    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        // If connection fails, tell the client the file was not found
        send(client_sock, "NOTFOUND", 8, 0);
        return;
    }

    // Send removef command to the server
    snprintf(buffer, sizeof(buffer), "removef %s", filename);
    send(sockfd, buffer, strlen(buffer), 0);

    // Wait for confirmation message (success or error)
    int bytes = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        send(client_sock, buffer, bytes, 0);  // Forward message to client
    } else {
        send(client_sock, "NOTFOUND", 8, 0);  // File not found or error
    }

    close(sockfd);  // Close connection to secondary server
}

// Check file extension and decide which server handles the deletion
void handle_removef(const char* filename, int client_sock) {
    char* ext = strrchr(filename, '.');  // Extract file extension
    if (!ext) {
        send(client_sock, "NOTFOUND", 8, 0);
        return;
    }

    // Handle .c file deletion locally (S1)
    if (strcmp(ext, ".c") == 0) {
        char path[BUFFER_SIZE];
        const char* rel_path = get_relative_path(filename);
    if (!rel_path) {
    send(client_sock, "File not found in S1.", 22, 0);
    return;
    }
snprintf(path, sizeof(path), "%s/S1/%s", getenv("HOME"), rel_path);

        if (remove(path) == 0)
            send(client_sock, "File removed from S1.", 22, 0);
        else
            send(client_sock, "File not found in S1.", 22, 0);
    }
    // Forward .pdf deletion to S2
    else if (strcmp(ext, ".pdf") == 0)
        forward_removef_to_secondary(S2_IP, S2_PORT, filename, client_sock);
    // Forward .txt deletion to S3
    else if (strcmp(ext, ".txt") == 0)
        forward_removef_to_secondary(S3_IP, S3_PORT, filename, client_sock);
    // Forward .zip deletion to S4
    else if (strcmp(ext, ".zip") == 0)
        forward_removef_to_secondary(S4_IP, S4_PORT, filename, client_sock);
    else
        send(client_sock, "Unsupported file type.", 23, 0);
}

// Helper function to create intermediate directories like mkdir -p
void create_directories(const char* path) {
    char temp[BUFFER_SIZE];
    snprintf(temp, sizeof(temp), "%s", path);
    char *p = temp + 1;
    while ((p = strchr(p, '/')) != NULL) {
        *p = '\0';
        mkdir(temp, 0755);
        *p = '/';
        p++;
    }
    mkdir(temp, 0755);   //this will create directory in S1 if missing
}


// ----------------------------
// Handling downltar: Create or request tarball based on file type
// ----------------------------
void handle_downltar(const char* cmdline, int client_sock) {
    printf(" handle_downltar called: %s\n", cmdline);

    char ext[16];
    if (sscanf(cmdline, "downltar %s", ext) != 1) {
        send(client_sock, "Invalid command format", 23, 0);
        return;
    }

    // Handling .c tarball locally
    if (strcmp(ext, ".c") == 0) {
        // To Generate list of .c files in ~/S1
        char find_cmd[BUFFER_SIZE], tar_cmd[BUFFER_SIZE];
        snprintf(find_cmd, sizeof(find_cmd), "find %s/S1 -type f -name \"*.c\" > files_to_tar.txt", getenv("HOME"));
        system(find_cmd);  // Save list of .c files to a text file

        printf(" Creating cfiles.tar using list from files_to_tar.txt\n");

        // Using fork-exec to create tar file from file list
        pid_t pid = fork();
        if (pid == 0) {
            execlp("tar", "tar", "-cf", "cfiles.tar", "-T", "files_to_tar.txt", NULL);
            perror("execlp failed");
            exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);  // Waiting for tar process
            if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
                send(client_sock, "Error creating tarball", 23, 0);
                return;
            }
        } else {
            perror("fork failed");
            send(client_sock, "Tar process failed", 18, 0);
            return;
        }

        // Open and send cfiles.tar to client
        FILE* fp = fopen("cfiles.tar", "rb");
        if (!fp) {
            send(client_sock, "NOTFOUND", 8, 0);
            return;
        }

        char buffer[BUFFER_SIZE];
        int bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
            send(client_sock, buffer, bytes, 0);
        send(client_sock, "EOF", 3, 0);
        fclose(fp);

        // Clean up temporary files
        system("rm files_to_tar.txt cfiles.tar");
        printf(" Sent cfiles.tar to client\n");
    }
    // Handle .pdf tarball: request from S2
    else if (strcmp(ext, ".pdf") == 0) {
        printf(" Requesting pdf.tar from S2\n");
        request_file_from_secondary(S2_IP, S2_PORT, "pdf.tar", "pdf.tar");

        FILE* fp = fopen("pdf.tar", "rb");
        if (!fp) {
            send(client_sock, "NOTFOUND", 8, 0);
            return;
        }

        char buffer[BUFFER_SIZE];
        int bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
            send(client_sock, buffer, bytes, 0);
        send(client_sock, "EOF", 3, 0);
        fclose(fp);
        remove("pdf.tar");  // Delete after sending

        printf(" Forwarded pdf.tar to client\n");
    }
    // Handle .txt tarball: request from S3
    else if (strcmp(ext, ".txt") == 0) {
        printf(" Requesting text.tar from S3\n");
        request_file_from_secondary(S3_IP, S3_PORT, "text.tar", "text.tar");

        FILE* fp = fopen("text.tar", "rb");
        if (!fp) {
            send(client_sock, "NOTFOUND", 8, 0);
            return;
        }

        char buffer[BUFFER_SIZE];
        int bytes;
        while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0)
            send(client_sock, buffer, bytes, 0);
        send(client_sock, "EOF", 3, 0);
        fclose(fp);
        remove("text.tar");

        printf(" Forwarded text.tar to client\n");
    }
    // Reject .zip filetype for downltar
    else if (strcmp(ext, ".zip") == 0) {
        send(client_sock, "Zip files not supported", 24, 0);
        printf(" Unsupported extension: .zip\n");
    }
    // Any other extension is invalid
    else {
        send(client_sock, "Unsupported extension", 22, 0);
        printf(" Invalid extension received: %s\n", ext);
    }
}

// ----------------------------
// Function: prcclient
// Main handler for individual client (runs in child process)
// ----------------------------
void prcclient(int client_sock) {
    char buffer[BUFFER_SIZE];

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;

        buffer[bytes] = '\0';
        printf("[S1] Command received: %s\n", buffer);

        // Handle uploadf command
        if (strncmp(buffer, "uploadf", 7) == 0) {
            char filename[256], dest[512];
            if (sscanf(buffer, "uploadf %s %s", filename, dest) == 2) {
                char* ext = strrchr(filename, '.');
                if (!ext) {
                    send(client_sock, "Invalid extension", 17, 0);
                    continue;
                }

                // Save uploaded file temporarily to S1 folder
                char path[BUFFER_SIZE];
                snprintf(path, sizeof(path), "%s/S1/%s", getenv("HOME"), dest + 4);  // Skip ~S1
                create_directories(path);  // Ensure directory structure is made

                char fullpath[BUFFER_SIZE];
                snprintf(fullpath, sizeof(fullpath), "%s/%s", path, filename);

                FILE* fp = fopen(fullpath, "wb");
                if (!fp) continue;


                send(client_sock, "OK", 2, 0);

                // Receive file data from client and write to disk
                while ((bytes = recv(client_sock, buffer, sizeof(buffer), 0)) > 0) {
                    if (bytes == 3 && strncmp(buffer, "EOF", 3) == 0) break;
                    fwrite(buffer, 1, bytes, fp);
                }
                fclose(fp);

                // Store path mapping for retrieval later
                save_file_path(filename, dest);

                // Forward to secondary server if it's not a .c file
                if (strcmp(ext, ".pdf") == 0)
                    send_to_secondary_server(S2_IP, S2_PORT, fullpath, filename, dest);
                else if (strcmp(ext, ".txt") == 0)
                    send_to_secondary_server(S3_IP, S3_PORT, fullpath, filename, dest);
                else if (strcmp(ext, ".zip") == 0)
                    send_to_secondary_server(S4_IP, S4_PORT, fullpath, filename, dest);

                char msg[256];
                snprintf(msg, sizeof(msg), "File '%s' saved at %s", filename, fullpath);
                send(client_sock, msg, strlen(msg), 0);
            }
        }
        // Handle downlf command (download individual file)
        else if (strncmp(buffer, "downlf", 6) == 0) {
            char filename[256];
            if (sscanf(buffer, "downlf %s", filename) == 1) {
                char* ext = strrchr(filename, '.');
                const char* rel_path = get_relative_path(filename);
                if (!ext || (!rel_path && strcmp(ext, ".c") != 0)) {
                    send(client_sock, "NOTFOUND", 8, 0);
                    return;
                }

                if (strcmp(ext, ".c") == 0) {
                    send_local_file(client_sock, filename);
                } else {
                    // Determine which server to connect to for sending file
                    const char* ip = NULL;
                    int port = 0;

                    if (strcmp(ext, ".pdf") == 0) {
                        ip = S2_IP; port = S2_PORT;
                    } else if (strcmp(ext, ".txt") == 0) {
                        ip = S3_IP; port = S3_PORT;
                    } else if (strcmp(ext, ".zip") == 0) {
                        ip = S4_IP; port = S4_PORT;
                    } else {
                        send(client_sock, "Unsupported file type", 22, 0);
                        return;
                    }

                    // Forward request and stream back result
                    int sockfd;
                    struct sockaddr_in addr;
                    char buffer[BUFFER_SIZE];

                    sockfd = socket(AF_INET, SOCK_STREAM, 0);
                    addr.sin_family = AF_INET;
                    addr.sin_port = htons(port);
                    inet_pton(AF_INET, ip, &addr.sin_addr);

                    if (connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                        send(client_sock, "NOTFOUND", 8, 0);
                        return;
                    }

                    snprintf(buffer, sizeof(buffer), "downlf %s", rel_path);
                    send(sockfd, buffer, strlen(buffer), 0);

                    int bytes;
                    while ((bytes = recv(sockfd, buffer, sizeof(buffer), 0)) > 0) {
                        if (bytes == 3 && strncmp(buffer, "EOF", 3) == 0)
                            break;
                        send(client_sock, buffer, bytes, 0);
                    }

                    send(client_sock, "EOF", 3, 0);
                    close(sockfd);
                }
            }
        }

        // Handle removef
        else if (strncmp(buffer, "removef", 7) == 0) {
            char filename[256];
            if (sscanf(buffer, "removef %s", filename) == 1)
                handle_removef(filename, client_sock);
        }
        // Handle dispfnames
        else if (strncmp(buffer, "dispfnames", 10) == 0) {
            char pathname[512];
            if (sscanf(buffer, "dispfnames %s", pathname) == 1) {
                handle_dispfnames(client_sock, pathname);
            } else {
                send(client_sock, "Usage: dispfnames <pathname>\n", 30, 0);
            }
        }

        // Handle downltar
        else if (strncmp(buffer, "downltar", 8) == 0) {
            handle_downltar(buffer, client_sock);
        }
    }

    close(client_sock);  // Close client connection
    exit(0);             // Exit child process
}

// ----------------------------
// Main function of S1
// ----------------------------
int main() {
    int server_sock, client_sock;
    struct sockaddr_in addr, cli_addr;
    socklen_t addr_size;

    signal(SIGCHLD, SIG_IGN);  // Prevent zombie child processes

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_sock, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_sock, 10);

    printf("[S1] Server listening on port %d...\n", PORT);

    while (1) {
        addr_size = sizeof(cli_addr);
        client_sock = accept(server_sock, (struct sockaddr*)&cli_addr, &addr_size);
        printf("[S1] Connected to client: %s\n", inet_ntoa(cli_addr.sin_addr));

        // Handle each client in a separate child process
        if (fork() == 0) {
            close(server_sock);       // Child does not need the server socket
            prcclient(client_sock);   // Handle client request
        } else {
            close(client_sock);       // Parent does not need the client socket
        }
    }

    return 0;
}
