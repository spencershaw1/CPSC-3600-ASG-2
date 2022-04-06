#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include "Practical.h"

void sig_handler() {
    printf("Summary goes here.\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    int opt;

    // Flag ints
    int noprint = 0;
    int server = 0;
    // Set default values
    int pingcount = 0x7fffffff;
    double pinginterval = 1.0;
    char portnum[] = "33333";
    int pingsize = 12;

    // Set a new signal handler
    signal(SIGINT, sig_handler);

    // Get options and arguments
    while ((opt = getopt(argc, argv, "Snc:i:p:s:")) != -1) {
        switch (opt) {
        case 'n':
            noprint = 1;
            break;
        case 'S':
            server = 1;
            break;
        case 'c':
            pingcount = atoi(optarg);
            break;
        case 'i':
            pinginterval = strtod(optarg, NULL);
            break;
        case 'p':
            strcpy(optarg, portnum);
            break;
        case 's':
            pingsize = atoi(optarg);
            break;
        default: /* '?' */
            fprintf(stderr, "Usage: %s [-t nsecs] [-n] name\n",
                    argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    // Check if options arguments are missing
    if ((!server) && optind >= argc) {
        fprintf(stderr, "Expected argument after options\n");
        exit(EXIT_FAILURE);
    }

    printf("Count     %15d\nSize      %15d\nInterval  %15.3lf\nPort      %15s\nServer_ip %15s\n",
            pingcount, pingsize, pinginterval, portnum, argv[optind]);

    // Create socket
    int sock = socket(AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP);

    // Run the Server Code
    if (server == 1) {
        // Construct the server address structure
        struct addrinfo addrCriteria;                   // Criteria for address
        memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
        addrCriteria.ai_family = AF_UNSPEC;             // Any address family
        addrCriteria.ai_flags = AI_PASSIVE;             // Accept on any address/port
        addrCriteria.ai_socktype = SOCK_DGRAM;          // Only datagram socket
        addrCriteria.ai_protocol = IPPROTO_UDP;         // Only UDP socket

        struct addrinfo *servAddr; // List of server addresses
        int rtnVal = getaddrinfo(NULL, portnum, &addrCriteria, &servAddr);
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
        struct addrinfo addrCriteria;                   // Criteria for address match
        memset(&addrCriteria, 0, sizeof(addrCriteria)); // Zero out structure
        addrCriteria.ai_family = AF_UNSPEC;             // Any address family
        // For the following fields, a zero value means "don't care"
        addrCriteria.ai_socktype = SOCK_DGRAM;          // Only datagram sockets
        addrCriteria.ai_protocol = IPPROTO_UDP;         // Only UDP protocol

        // Get address(es)
        struct addrinfo *servAddr; // List of server addresses
        int rtnVal = getaddrinfo(argv[optind], portnum, &addrCriteria, &servAddr);
        if (rtnVal != 0)
            DieWithUserMessage("getaddrinfo() failed", gai_strerror(rtnVal));
        
        // Build the string
        char* pingString = malloc(pingsize);
        memset(pingString, 'A', pingsize);
        int pingStringLen = strlen(pingString);
        // Send the string to the server
        for (int i = 0; i < pingcount; i++) {

            // Create a datagram/UDP socket
            int sock = socket(servAddr->ai_family, servAddr->ai_socktype,
                servAddr->ai_protocol); // Socket descriptor for client
            if (sock < 0)
                DieWithSystemMessage("Asocket() failed");

            ssize_t numBytes = sendto(sock, pingString, pingStringLen, 0,
                servAddr->ai_addr, servAddr->ai_addrlen);
            if (numBytes < 0)
                DieWithSystemMessage("sendto() failed");
            else if (numBytes != pingStringLen)
                DieWithUserMessage("sendto() error", "sent unexpected number of bytes");

            // Receive a response

            struct sockaddr_storage fromAddr; // Source address of server
            // Set length of from address structure (in-out parameter)
            socklen_t fromAddrLen = sizeof(fromAddr);
            char buffer[MAXSTRINGLENGTH + 1]; // I/O buffer
            numBytes = recvfrom(sock, buffer, MAXSTRINGLENGTH, 0,
                (struct sockaddr *) &fromAddr, &fromAddrLen);
            if (numBytes < 0)
                DieWithSystemMessage("recvfrom() failed");
            else if (numBytes != pingStringLen)
                DieWithUserMessage("recvfrom() error", "received unexpected number of bytes");

            // Verify reception from expected source
            if (!SockAddrsEqual(servAddr->ai_addr, (struct sockaddr *) &fromAddr))
                DieWithUserMessage("recvfrom()", "received a packet from unknown source");

            buffer[pingStringLen] = '\0';     // Null-terminate received data

            // Print (if not disabled)
            if (!noprint) {
                printf("Received: %s\n", buffer); // Print the echoed string
            }
            
        }

        freeaddrinfo(servAddr);
        close(sock);
        exit(0);
    }

    exit(EXIT_SUCCESS);
}