// SPENCER SHAW     JSSHAW 
// ADAM COPELAND    AKCOPEL
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

#define NANO    1000000000
#define MICRO   1000000
#define MILLI   1000
#define CHARMAX 127


// Saves necessary info for the threads
struct host {
    struct addrinfo* address;
    int sock;
    char* message;
    struct options* settings;
    struct timespec* sendTimes;
    struct timespec* recvTimes;
    struct timespec* rttTimes;
    double* stats;
};

// Global host for the signal to use
struct host nodeInfo;
// Tracks the number of packets that got sent (for interrupted summary)
int totalsent;
// Mutexes and conditions
pthread_mutex_t mutexSend;
pthread_cond_t condSend;

// Saves user settings
struct options {
    int noprint;
    int server;
    int pingcount;
    double pinginterval;
    char portnum[5];
    int pingsize;
};


// Returns the difference between a stop and start time
void timespec_diff(struct timespec *start, struct timespec *stop, struct timespec *result) {
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + NANO;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }
    return;
}
// Converts timespec to milliseconds of total tole [Should only use with difference, not abs time]
int timespec_to_millisecond (struct timespec* time) {
    int milliseconds = 0;
    milliseconds += time->tv_nsec / MICRO;
    milliseconds += time->tv_sec * MILLI;
    return milliseconds;
}

// Adds a time expressed as a double in seconds to the timespec
void timespec_addtime(double interval, struct timespec *spec) {
    // Convert interval
    long long ns = (int)(interval*NANO);
    // Add to current nsecs
    ns += spec->tv_nsec;
    // Get whole seconds
    int seconds = ns / NANO;
    // Get remainder
    ns = ns % NANO;
    // Save in given timespec
    spec->tv_sec += seconds;
    spec->tv_nsec = ns;
    return;
}

void* send_msg(void* arg) {
    struct host* sender = (struct host*) arg;
    struct timespec tp;
    time_t start_time = 0;
    double abs_ping;

    // Send the string
    for (int i = 0; i < sender->settings->pingcount; i++) {
        pthread_mutex_lock(&mutexSend);

        // Timing stuff
        clock_gettime(CLOCK_REALTIME, &tp);
        timespec_addtime(sender->settings->pinginterval, &tp);

        // Use the first char as numbering
        int blocknum = 0;
        char seqchar = (i % CHARMAX)+1;

        sender->message[0] = seqchar;

        int pingStringLen = strlen(sender->message);
        
        sender->sendTimes[i] = tp;
        
        // Perform the timed wait
        int rc = 0;
        while(rc == 0)
            rc = pthread_cond_timedwait(&condSend, &mutexSend, &tp);

        ssize_t numBytes = sendto(sender->sock, sender->message, pingStringLen, 0,
            sender->address->ai_addr, sender->address->ai_addrlen);
        // Update last sent
        totalsent = i+1;
        if (numBytes < 0)
            DieWithSystemMessage("sendto() failed");
        else if (numBytes != pingStringLen)
            DieWithUserMessage("sendto() error", "sent unexpected number of bytes");
        
        pthread_mutex_unlock(&mutexSend);
    }

    pthread_exit(NULL);
}

void* recv_msg(void* arg) {
    struct host* receiver = (struct host*) arg;
    int pingStringLen = strlen(receiver->message);
    struct timespec tp;
    int curchar;
    int prevchar;
    int blocknum;

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

        // Signal sender that receive is complete
        pthread_cond_signal(&condSend); 

        // Verify reception from expected source
        if (!SockAddrsEqual(receiver->address->ai_addr, (struct sockaddr *) &fromAddr))
            DieWithUserMessage("recvfrom()", "received a packet from unknown source");

        // Check sequence number
        int curchar = buffer[0];
        curchar += blocknum*CHARMAX; // Add the current number of blocks
        if (curchar < prevchar) {
            curchar += CHARMAX; // Add a block this loop too
            blocknum++; // Increment the block number
        }
        else {
            prevchar = curchar; // Update prevchar
        }
        
        int seqdiff = curchar - (i+1); // Find difference, accounting for the seqnum starting at 1 instead of 0
        if (seqdiff != 0) {
            for (int l = 0; l < seqdiff; l++) {
                // Check noprint flag
                if (receiver->settings->noprint == 0) {
                    printf("\t\t%d\t[PACKET LOST]\n", i+l+1); // Print the echoed string
                }
                receiver->recvTimes[i+l].tv_sec = -1; // Invalidate recieve time
            }
            i = curchar - 1; // Update i to current packet num, -1 to account to numbering start point
        }

        // Timing stuff
        clock_gettime(CLOCK_REALTIME, &tp);
        receiver->recvTimes[i] = tp;
        buffer[pingStringLen] = '\0';     // Null-terminate received data

        // Calculate RTT
        struct timespec diff;
        timespec_diff(&receiver->sendTimes[i], &receiver->recvTimes[i], &diff);
        // Save diff
        receiver->rttTimes[i].tv_sec = diff.tv_sec;
        receiver->rttTimes[i].tv_nsec = diff.tv_nsec;

        // Convert to microseconds
        int microdiff = diff.tv_nsec / MILLI;
        double millidiff = (double)microdiff / MILLI;
        
        // Update maximum
        if (millidiff > receiver->stats[0])
            receiver->stats[0] = millidiff;
        // Update minimum
        if (millidiff < receiver->stats[1])
            receiver->stats[1] = millidiff;

        // Print (if not disabled)
        if (receiver->settings->noprint == 0) {
            printf("\t\t%d\t%ld\t", i+1, numBytes); // Print the echoed string
            printf("%0.3lf\n", millidiff);
        }
    }

    pthread_exit(NULL);
}

// Calculates the summary, prints, and closes
void sig_handler() {
    int pktcnt = 0; // Tracks number of valid packets
    int finpkt = 0; // Tracks the final valid packet
    double pktloss = 0; // Tracks the proportion of lost packets
    double diffsum = 0; // Tracks the sum of RTTs
    double avgtime = 0; // Tracks the average of RTTs
    // Measure packetloss and add valid times
    for (int i = 0; i < nodeInfo.settings->pingcount; i++) {
        if (nodeInfo.recvTimes[i].tv_sec != -1) {
            // Count the valid packet
            pktcnt++;
            // Add to sum of RTT [NOTE: Currently assumes <1 sec RTT]
            diffsum += nodeInfo.rttTimes[i].tv_nsec;
            // Update the latest valid packet
            finpkt = i;
        }
    }
    // Calculate packet loss
    pktloss = 1.0 - ((double)pktcnt / totalsent);
    // Calculate average RTT [NOTE: Currently assumes <1 sec RTT]
    avgtime = ((double)diffsum / pktcnt) / MICRO;
    
    struct timespec totaltime;
    timespec_diff(&nodeInfo.sendTimes[0], &nodeInfo.recvTimes[finpkt], &totaltime);
    int totalms = timespec_to_millisecond(&totaltime);

    // Print Summary
    printf("\n%d packets transmitted, %d received, %0.0lf%% packet loss, ", totalsent, pktcnt, pktloss*100);
    printf("time %d ms\n", totalms);
    printf("rtt min/avg/max = %0.3lf/%0.3lf/%0.3lf msec\n", nodeInfo.stats[1], avgtime, nodeInfo.stats[0]);
    exit(0);
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

    if (!settings.server)
        signal(SIGINT, sig_handler);

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
        fprintf(stderr, "Count     %15d\nSize      %15d\nInterval  %15.3lf\nPort      %15s\nServer_ip %15s\n\n",
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
        memset(pingString, 'A', sizeof(char) * settings.pingsize); 
        pingString[settings.pingsize] = '\0';
        printf(" "); // formatting

        // Send the string to the server

        // Create a datagram/UDP socket
        int sock = socket(servAddr->ai_family, servAddr->ai_socktype,
            servAddr->ai_protocol); // Socket descriptor for client
        if (sock < 0)
            DieWithSystemMessage("Asocket() failed");

        // Create thread arguments
        nodeInfo.address = servAddr;
        nodeInfo.message = pingString;
        nodeInfo.message[settings.pingsize] = '\0';
        nodeInfo.sock = sock;
        nodeInfo.settings = &settings;
        
        // Create time tables
        struct timespec *sendTable, *recvTable, *rttTable;
        sendTable = (struct timespec*)malloc(nodeInfo.settings->pingcount * sizeof(struct timespec));
        recvTable = (struct timespec*)malloc(nodeInfo.settings->pingcount * sizeof(struct timespec));
        rttTable = (struct timespec*)malloc(nodeInfo.settings->pingcount * sizeof(struct timespec));
        double timestat[2] = {0, MICRO};
        nodeInfo.recvTimes = recvTable;
        nodeInfo.sendTimes = sendTable;
        nodeInfo.rttTimes = rttTable;
        nodeInfo.stats = timestat;

        // Fill recvTime with invalid info for error checking
        for (int i = 0; i < nodeInfo.settings->pingcount; i++) {
            nodeInfo.recvTimes[i].tv_sec = -1;
        }

        // Initialize mutexes and conditions
        pthread_mutex_init(&mutexSend, NULL);
        pthread_cond_init(&condSend, NULL);

        // Start and close threads
        pthread_t tids[2];
        if(pthread_create(&tids[1], NULL, &send_msg, &nodeInfo) != 0)
            fprintf(stderr, "Failed to create thread.\n");
        if(pthread_create(&tids[2], NULL, &recv_msg, &nodeInfo) != 0)
            fprintf(stderr, "Failed to create thread.\n");

        if(pthread_join(tids[1], NULL) != 0)
            fprintf(stderr, "Failed to join thread.\n");
        if(pthread_join(tids[2], NULL) != 0)
            fprintf(stderr, "Failed to join thread.\n");

        // If the prints were suppressed, print a line of asterisks
        if (nodeInfo.settings->noprint) {
            printf("**********\n");
        }

        raise(SIGINT);

        // destroy allocated 
        pthread_mutex_destroy(&mutexSend);
        pthread_cond_destroy(&condSend);
        free(sendTable);
        free(recvTable);
        free(rttTable);
        freeaddrinfo(servAddr);
        close(sock);

        exit(0);
    }

    exit(EXIT_SUCCESS);
}