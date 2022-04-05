#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>

void sig_handler() {
    printf("Summary goes here.\n");
    exit(0);
}

int main(int argc, char *argv[]) {
    int opt;
    int pingcount, portnum, pingsize;
    double pinginterval;

    // Flag ints
    int noprint = 0;
    int server = 0;
    // Set default values
    pingcount = 0x7fffffff;
    pinginterval = 1.0;
    portnum = 33333;
    pingsize = 12;

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
            portnum = atoi(optarg);
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
    if (optind >= argc) {
        fprintf(stderr, "Expected argument after options\n");
        exit(EXIT_FAILURE);
    }

    printf("Count     %15d\nSize      %15d\nInterval  %15.3lf\nPort      %15d\nServer_ip %15s\n",
            pingcount, pingsize, pinginterval, portnum, argv[optind]);

    // Create socket
    int sock = socket(AF_UNSPEC, SOCK_DGRAM, IPPROTO_UDP);

    exit(EXIT_SUCCESS);
}