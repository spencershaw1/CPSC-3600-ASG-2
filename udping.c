#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/socket.h>
#include <pthread.h>
#include <unistd.h>
#include <netdb.h>
#include <time.h>
#include "Practical.h"

void sig_handler() {
    printf("Summary goes here.\n");
    exit(0);
}

struct host {
    struct addrinfo* address;
    int sock;
    char* message;
    struct options* settings;
    struct timespec* sendTimes;
    struct timespec* recvTimes;
    struct timespec* rttTimes;
};

struct options {
    int noprint;
    int server;
    int pingcount;
    double pinginterval;
    char portnum[5];
    int pingsize;
};

void* send_msg(void* arg) {
    struct host* sender = (struct host*) arg;

    struct timespec tp;
    

    // Send the string
    for (int i = 0; i < sender->settings->pingcount; i++) {
        // Timing stuff
        clock_gettime(CLOCK_REALTIME, &tp);
        sender->sendTimes[i] = tp;

        int pingStringLen = strlen(sender->message);
        ssize_t numBytes = sendto(sender->sock, sender->message, pingStringLen, 0,
            sender->address->ai_addr, sender->address->ai_addrlen);
        if (numBytes < 0)
            DieWithSystemMessage("sendto() failed");
        else if (numBytes != pingStringLen)
            DieWithUserMessage("sendto() error", "sent unexpected number of bytes");
    }
    
    pthread_exit(NULL);
}

void* recv_msg(void* arg) {
    
    struct host* receiver = (struct host*) arg;
    int pingStringLen = strlen(receiver->message);
    struct timespec tp;

    // Receive a response
    for (int i = 0; i < receiver->settings->pingcount; i++) {
        struct sockaddr_storage fromAddr; // Source address of server
        // Set length of from address structure (in-out parameter)
        socklen_t fromAddrLen = sizeof(fromAddr);
        char buffer[MAXSTRINGLENGTH + 1]; // I/O buffer
        ssize_t numBytes = recvfrom(receiver->sock, buffer, MAXSTRINGLENGTH, 0,
            (struct sockaddr *) &fromAddr, &fromAddrLen);
        if (numBytes < 0)
            DieWithSystemMessage("recvfrom() failed");
        else if (numBytes != pingStringLen)
            DieWithUserMessage("recvfrom() error", "received unexpected number of bytes");

        // Verify reception from expected source
        if (!SockAddrsEqual(receiver->address->ai_addr, (struct sockaddr *) &fromAddr))
            DieWithUserMessage("recvfrom()", "received a packet from unknown source");

        // Timing stuff
        clock_gettime(CLOCK_REALTIME, &tp);
        receiver->recvTimes[i] = tp;

        buffer[pingStringLen] = '\0';     // Null-terminate received data

        // Calculate RTT
        struct timespec diff = {.tv_sec = receiver->recvTimes->tv_sec - receiver->sendTimes->tv_sec, .tv_nsec =
            receiver->recvTimes->tv_nsec - receiver->sendTimes->tv_nsec};
        if (diff.tv_nsec < 0) {
            diff.tv_nsec += 1000000000;
            diff.tv_sec--;
        }
        
        

        // Print (if not disabled)
        if (receiver->settings->noprint == 0) {
            printf("Received: %s ", buffer); // Print the echoed string
            printf("[Time (micros): %ld]\n", (diff.tv_nsec /1000));
        }
    }

    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    int opt;

    // Create settings struct
    struct options settings;
    // Set default options
    settings.noprint = 0;
    settings.server = 0;
    settings.pingcount = 0x7fffffff;
    settings.pinginterval = 1.0;
    strcpy(settings.portnum, "33333");
    settings.pingsize = 12;

    // Set a new signal handler
    signal(SIGINT, sig_handler);

    // Get options and arguments
    while ((opt = getopt(argc, argv, "Snc:i:p:s:")) != -1) {
        switch (opt) {
        case 'n':
            settings.noprint = 1;
            break;
        case 'S':
            settings.server = 1;
            break;
        case 'c':
            settings.pingcount = atoi(optarg);
            break;
        case 'i':
            settings.pinginterval = strtod(optarg, NULL);
            break;
        case 'p':
            strcpy(settings.portnum, optarg);
            break;
        case 's':
            settings.pingsize = atoi(optarg);
            break;
        default: /* '?' */
            fprintf(stderr, "Usage: %s [-t nsecs] [-n] name\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    // Check if options arguments are missing
    if ((!settings.server) && optind >= argc) {
        fprintf(stderr, "Expected argument after options\n");
        exit(EXIT_FAILURE);
    }

    // Create socket
    //int sock = socket(AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP);

    // Run the Server Code
    if (settings.server == 1) {
        // Construct the server address structure
        struct addrinfo addrCriteria;                   // Criteria for address
        memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
        addrCriteria.ai_family = AF_UNSPEC;             // Any address family
        addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
        addrCriteria.ai_socktype = SOCK_DGRAM;          // Only datagram socket
        addrCriteria.ai_protocol = IPPROTO_UDP;         // Only UDP socket

        struct addrinfo *servAddr; // List of server addresses
        int rtnVal = getaddrinfo(NULL, settings.portnum, &addrCriteria, &servAddr);
        if (rtnVal != 0)
            DieWithUserMessage("getaddrinfo() failed", gai_strerror(rtnVal));

        // Create socket for incoming connections
        int sock = socket(servAddr->ai_family, servAddr->ai_socktype,
            servAddr->ai_protocol);
        if (sock < 0)
            DieWithSystemMessage("socket() failed");

        // Bind to the local address
        if (bind(sock, servAddr->ai_addr, servAddr->ai_addrlen) < 0)
            DieWithSystemMessage("bind() failed");

        // Free address list allocated by getaddrinfo()
        freeaddrinfo(servAddr);

        for (;;) { // Run forever
            struct sockaddr_storage clntAddr; // Client address
            // Set Length of client address structure (in-out parameter)
            socklen_t clntAddrLen = sizeof(clntAddr);

            // Block until receive message from a client
            char buffer[MAXSTRINGLENGTH]; // I/O buffer
            // Size of received message
            ssize_t numBytesRcvd = recvfrom(sock, buffer, MAXSTRINGLENGTH, 0,
                (struct sockaddr *) &clntAddr, &clntAddrLen);
            if (numBytesRcvd < 0)
            DieWithSystemMessage("recvfrom() failed");

            fputs("Handling client ", stdout);
            PrintSocketAddress((struct sockaddr *) &clntAddr, stdout);
            fputc('\n', stdout);

            // Send received datagram back to the client
            ssize_t numBytesSent = sendto(sock, buffer, numBytesRcvd, 0,
                (struct sockaddr *) &clntAddr, sizeof(clntAddr));
            if (numBytesSent < 0)
            DieWithSystemMessage("sendto() failed)");
            else if (numBytesSent != numBytesRcvd)
            DieWithUserMessage("sendto()", "sent unexpected number of bytes");
        }
    }
    // Otherwise run as client
    else {
        // Print settings message
        printf("Count     %15d\nSize      %15d\nInterval  %15.3lf\nPort      %15s\nServer_ip %15s\n\n",
            settings.pingcount, settings.pingsize, settings.pinginterval, settings.portnum, argv[optind]);

        struct addrinfo addrCriteria;                   // Criteria for address match
        memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
        addrCriteria.ai_family = AF_UNSPEC;             // Any address family
        // For the following fields, a zero value means "don't care"
        addrCriteria.ai_socktype = SOCK_DGRAM;          // Only datagram sockets
        addrCriteria.ai_protocol = IPPROTO_UDP;         // Only UDP protocol

        // Get address(es)
        struct addrinfo *servAddr; // List of server addresses
        int rtnVal = getaddrinfo(argv[optind], settings.portnum, &addrCriteria, &servAddr);
        if (rtnVal != 0)
            DieWithUserMessage("getaddrinfo() failed", gai_strerror(rtnVal));
        
        // Build the string
        char* pingString = malloc(settings.pingsize);
        memset(pingString, 'A', settings.pingsize);

        // Send the string to the server

        // Create a datagram/UDP socket
        int sock = socket(servAddr->ai_family, servAddr->ai_socktype,
            servAddr->ai_protocol); // Socket descriptor for client
        if (sock < 0)
            DieWithSystemMessage("Asocket() failed");

        // Create thread arguments
        struct host nodeInfo;
        nodeInfo.address = servAddr;
        nodeInfo.message = pingString;
        nodeInfo.sock = sock;
        nodeInfo.settings = &settings;

        // Create time tables
        struct timespec sendTable[nodeInfo.settings->pingcount];
        struct timespec recvTable[nodeInfo.settings->pingcount];
        struct timespec rttTable[nodeInfo.settings->pingcount];
        nodeInfo.recvTimes = recvTable;
        nodeInfo.sendTimes = sendTable;
        nodeInfo.recvTimes = rttTable;


        // Start and close threads
        pthread_t tids[2];
        pthread_create(&tids[1], NULL, &send_msg, &nodeInfo);
        pthread_create(&tids[2], NULL, &recv_msg, &nodeInfo);

        pthread_join(tids[1], NULL);
        pthread_join(tids[2], NULL);

        // If the prints were suppressed, print a line of asterisks
        if (nodeInfo.settings->noprint) {
            printf("**********\n\n");
        }

        freeaddrinfo(servAddr);
        close(sock);


        exit(0);
    }

    exit(EXIT_SUCCESS);
}