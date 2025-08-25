/*=====================================

Assignment 4 Submission

Name: Marati Nikhil Kumar

Roll number: 22CS10042

=====================================*/

/*
Please run the Makefile, then execute ./initksocket to initialize the shared memory. After that, run ./user1 and ./user2.
*/

#include "ksocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h> 
#include <errno.h> 

 
#define LOCAL_IP   "127.0.0.1"
#define LOCAL_PORT 6000
#define REMOTE_IP  "127.0.0.1"
#define REMOTE_PORT 5000
 
int main() {

    /* Create a KTP socket */
    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if (sockfd < 0) {
        perror("k_socket");
        exit(EXIT_FAILURE);
    }

    printf("iam assigned SM[%d]\n",sockfd);



    /* Bind the socket to local and remote addresses */
    if (k_bind(sockfd, LOCAL_IP, LOCAL_PORT, REMOTE_IP, REMOTE_PORT) < 0) {
        perror("k_bind");
        exit(EXIT_FAILURE);
    }

    print_socket(sockfd);

    FILE *output_file;

    /* Open the output file for writing */
    output_file = fopen("sample_received_1.txt", "w");
    if (output_file == NULL) {
        perror("Could not open sample_received.txt for writing");
        k_close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    char* message;
    int message_count = 0;
    int retries=0;

    while(1) {
        errno = 0;  // Reset errno before the call
        message = k_recvfrom(sockfd);
        
        if (message == NULL || strcmp(message,"-1") == 0) {
            if (errno == EBADF) {
                perror("k_recvfrom");
                exit(EXIT_FAILURE);
            }
            else if (errno == ENOMESSAGE) {
                retries++;
                if (retries == 10) {
                    break;
                }
                usleep(6000*1000); // Sleep 6s
                continue;
            }
        }

        retries = 0;
        message_count++;

        // Write the received message to the output file
        fprintf(output_file, "%s\n", message);

        printf("writing to file, Received Message: %s\n", message);
    }

    // Close the output file
    fclose(output_file);

    printf("Received %d messages\n", message_count);

    k_close(sockfd);
    
    return 0;
}
 