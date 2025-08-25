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

#define LOCAL_IP   "127.0.0.1"
#define LOCAL_PORT 5000
#define REMOTE_IP  "127.0.0.1"
#define REMOTE_PORT 6000

#define MAX_MESSAGE_SIZE 512

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


    FILE *file;
    char message[MAX_MESSAGE_SIZE];

    /* Open the sample.txt file for reading */
    file = fopen("sample.txt", "r");
    if (file == NULL) {
        perror("Could not open sample.txt");
        k_close(sockfd);
        exit(EXIT_FAILURE);
    }


    /* Read each line from the file and send it */
    int count = 0;
    while (fgets(message, MAX_MESSAGE_SIZE, file) && count < 20) {  // Limit to 200 messages for testing
        // Remove newline character if present
        size_t len = strlen(message);
        if (len > 0 && message[len - 1] == '\n') {
            message[len - 1] = '\0';
        }
        
        printf("Sending message %d: %s\n", count + 1, message);
        
        if (k_sendto(sockfd, message, REMOTE_IP, REMOTE_PORT) == -1) {
            perror("k_sendto");
            break;
        }
        
        count++;
        usleep(500000);  // Sleep 500ms between messages
    }

    printf("Sent %d messages from file\n", count);

    /* Close the file */
    fclose(file);

    k_close(sockfd);
    return 0;
}
