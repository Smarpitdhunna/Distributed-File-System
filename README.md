# 📖 Distributed File System — COMP-8567

## 📑 Table of Contents

* [📖 Introduction](#-introduction)
* [🎯 Project Objectives](#-project-objectives)
* [🗂 System Architecture](#-system-architecture)
* [🖥 Project Components](#-project-components)

  * [Server Programs](#server-programs)
  * [Client Program](#client-program)
* [⚙ Functionality Overview](#-functionality-overview)

  * [Client Commands](#client-commands)
* [🔧 Setup & Installation](#-setup--installation)
* [🚀 How to Run](#-how-to-run)
* [📦 File Structure](#-file-structure)
* [🔍 Error Handling](#-error-handling)
* [📖 Learning Outcomes](#-learning-outcomes)
* [📜 Submission Checklist](#-submission-checklist)

---

## 📖 Introduction

This project implements a *Distributed File System (DFS)* using *C and socket programming on UNIX/Linux systems*. It simulates a basic multi-server environment where files of different types (.c, .pdf, .txt, .zip) are distributed across different servers while the client interacts exclusively with the main server (S1), unaware of the other servers (S2, S3, S4) managing specific file types in the background.

---

## 🎯 Project Objectives

* Apply core *Operating System* and *Socket Programming* concepts.
* Implement *client-server communication* using TCP sockets.
* Simulate distributed file management with multiple servers handling different file types.
* Provide custom file management commands via client terminal.
* Demonstrate *process forking, **file transfers, **background communication between servers, and **remote command handling*.

---

## 🗂 System Architecture

plaintext
                    +-----------------+
                    |     Client       |
                    |  (w25clients.c)  |
                    +--------+--------+
                             |
                             v
                    +-----------------+
                    |   Server S1      |
                    |    (S1.c)        |
                    +---+----+----+----+
                        |    |    |
               +--------+    |    +--------+
               v             v             v
           +-------+     +-------+     +-------+
           |  S2   |     |  S3   |     |  S4   |
           | (PDF) |     | (TXT) |     | (ZIP) |
           +-------+     +-------+     +-------+


*Key Points:*

* All *clients communicate with S1 only*.
* S1 acts as an intermediary, distributing files to S2, S3, and S4 based on file type.
* *Process forking* is used by S1 to handle simultaneous client connections.
* File transfer operations between servers occur transparently in the background.

---

## 🖥 Project Components

### Server Programs

* *S1.c* — Main server, client handler, file distributor.
* *S2.c* — Stores .pdf files.
* *S3.c* — Stores .txt files.
* *S4.c* — Stores .zip files.

### Client Program

* *w25clients.c* — Command-line client interface.

---

## ⚙ Functionality Overview

This system supports the following *client commands*:

### Client Commands

#### 📥 uploadf filename destination_path

Uploads a file from client to S1.

* .c files stay on S1.
* .pdf → S2
* .txt → S3
* .zip → S4

*Example:*

bash
w25clients$ uploadf report.pdf ~S1/reports


---

#### 📤 downlf filename

Downloads a file from the system to client’s PWD.

* S1 manages requests directly or fetches from S2, S3, S4.

*Example:*

bash
w25clients$ downlf ~S1/reports/report.pdf


---

#### 🗑 removef filename

Deletes a file from the distributed file system.

* S1 deletes .c files locally.
* Delegates deletion requests for .pdf, .txt, .zip to S2, S3, S4.

*Example:*

bash
w25clients$ removef ~S1/reports/report.pdf


---

#### 📦 downltar filetype

Downloads a tar archive of all files of a specified type.

* .c → S1 creates cfiles.tar
* .pdf → S2 creates pdf.tar
* .txt → S3 creates text.tar

*Example:*

bash
w25clients$ downltar .pdf


---

#### 📄 dispfnames pathname

Displays file names within a specified directory across S1, S2, S3, S4 (aggregated and alphabetically sorted within file type groups).

*Example:*

bash
w25clients$ dispfnames ~S1/reports/


---

## 🔧 Setup & Installation

### Pre-Requisites:

* UNIX/Linux environment
* GCC Compiler (gcc)
* Basic permissions for file creation, deletion, and socket connections

---

## 🚀 How to Run

### 1️⃣ Compile all programs:

bash
gcc -o S1 S1.c
gcc -o S2 S2.c
gcc -o S3 S3.c
gcc -o S4 S4.c
gcc -o w25clients w25clients.c


### 2️⃣ Create server directories:

bash
mkdir ~/S1 ~/S2 ~/S3 ~/S4


### 3️⃣ Run servers in separate terminals:

bash
./S2
./S3
./S4
./S1


### 4️⃣ Run client:

bash
./w25clients


---

## 📦 File Structure

plaintext
project/
│
├── S1.c
├── S2.c
├── S3.c
├── S4.c
├── w25clients.c
│
├── ~/S1/
├── ~/S2/
├── ~/S3/
├── ~/S4/
│
└── README.md


---

## 🔍 Error Handling

* Validates file type, file existence before processing.
* Validates command syntax at client-side.
* Provides appropriate error messages for invalid inputs or missing files.
* Manages connection errors and ensures socket closure.
* Deletes files after transfer from S1 to other servers (for non-.c files).
* Ensures tar creation and cleanup is handled robustly.

---

## 📖 Learning Outcomes

* Demonstrated *client-server model implementation* using sockets.
* Developed *multi-server communication* with file-type-based distribution.
* Used *forking for concurrency* to handle multiple clients.
* Applied *file system operations*: upload, download, delete, display, archive.
* Improved *error handling* and *robustness* in distributed environments.
* Gained practical exposure to *operating system concepts* like process management and inter-process communication (IPC).

---

## 📜 Submission Checklist

✅ S1.c
✅ S2.c
✅ S3.c
✅ S4.c
✅ w25clients.c
✅ README.md

---

## 📌 Notes

* All programs are to be adequately *commented*.
* Demonstration will be scheduled individually (Apr 14–16).
* MOSS tool will be used for plagiarism detection.
* Only socket-based communication is allowed; no file-sharing protocols or third-party libraries.
