/*=====================================

Assignment 4 Submission

Name: Marati Nikhil Kumar

Roll number: 22CS10042

=====================================*/

#include "ksocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define SHM_KEY 1255  // Use the same key as in init.c
#define UDP_SHM_KEY 1256  // Shared memory key for UDP sockets


KtpSocket* SM=NULL;
pthread_t R,S;
UdpSocket* UDP_SOCKETS=NULL;

int dropMessage(float p) {
    float random_value = (float)rand() / RAND_MAX;  // Generate a random number between 0 and 1
    return (random_value < p) ? 1 : 0;
}

void attach_shared_memory() {
    // Attach to KtpSocket shared memory if not already attached
    if (SM == NULL) {
        int shmid = shmget(SHM_KEY, MAX_KSOCKETS * sizeof(KtpSocket), 0666);
        if (shmid == -1) {
            perror("shmget for KtpSocket failed");
            exit(1);
        }

        SM = (KtpSocket*) shmat(shmid, NULL, 0);
        if (SM == (void*)-1) {
            perror("shmat for KtpSocket failed");
            exit(1);
        }
    }

    // Attach to UDP_SOCKETS shared memory if not already attached
    if (UDP_SOCKETS == NULL) {
        int udp_shmid = shmget(UDP_SHM_KEY, MAX_USERS * sizeof(UdpSocket), 0666);
        if (udp_shmid == -1) {
            perror("shmget for UDP_SOCKETS failed");
            exit(1);
        }

        UDP_SOCKETS = (UdpSocket*) shmat(udp_shmid, NULL, 0);
        if (UDP_SOCKETS == (void*)-1) {
            perror("shmat for UDP_SOCKETS failed");
            exit(1);
        }
    }
}

int k_socket(int domain, int type, int protocol) {
    attach_shared_memory(); // Ensure shared memory is attached

    if (type != SOCK_KTP) {
        errno = EINVALID; // Invalid argument
        return -1;
    }
    
    // Find an available slot in SM
    int i;
    for (i = 0; i < MAX_KSOCKETS; i++) {
        //turn on mutex for that socket
        pthread_mutex_lock(&SM[i].mutex);
        if (!SM[i].is_allocated) {
            // Mark the entry as allocated
            SM[i].is_allocated = true;
            //turn off mutex for that socket
            pthread_mutex_unlock(&SM[i].mutex);
            break;
        } else {
            //turn off mutex for that socket
            pthread_mutex_unlock(&SM[i].mutex);
        }
    }

    if (i == MAX_KSOCKETS) {
        errno = ENOSPACE; // No space left in shared memory
        return -1;
    }

    // Find an available UDP socket
    int udp_index = -1;
    for (int j = 0; j < MAX_USERS; j++) {
        //turn on mutex for that socket
        pthread_mutex_lock(&UDP_SOCKETS[j].mutex);
        if (!UDP_SOCKETS[j].is_used) {
            // Mark the UDP socket as used
            udp_index = j;
            UDP_SOCKETS[udp_index].is_used = true;
            //turn off mutex for that socket
            pthread_mutex_unlock(&UDP_SOCKETS[j].mutex);
            break;
        } else {
            //turn off mutex for that socket
            pthread_mutex_unlock(&UDP_SOCKETS[j].mutex);
        }
    }
    
    if (udp_index == -1) {
        pthread_mutex_lock(&SM[i].mutex);
        SM[i].is_allocated = false;
        pthread_mutex_unlock(&SM[i].mutex);
        errno = ENOSPACE; // No UDP sockets available
        return -1;
    }

    printf("k_Socket: using SM[%d] with UDP socket %d (fd %d)\n", i, udp_index, 
           UDP_SOCKETS[udp_index].udp_socket_fd);

    
    //turn on mutex for that socket
    pthread_mutex_lock(&SM[i].mutex);

    // Initialize the KtpSocket entry in SM
    SM[i].pid = getpid();
    SM[i].udp_socket_fd = UDP_SOCKETS[udp_index].udp_socket_fd;

  // Clear the sender buffer (2D array): set each row's first byte to '\0'.
    for (int j = 0; j < MAX_SIZE; j++) {
        SM[i].send_seq_nums[j]=-1;
        SM[i].is_sent[j]=false;
        SM[i].send_timestamp[j].tv_sec = 0;
        SM[i].send_timestamp[j].tv_usec = 0;
    }

    // Initialize the 2D receive buffer: mark all slots as empty (first byte '\0')
    for (int j = 0; j < 10; j++) {
        SM[i].recv_seq_nums[j]=-1;
    }


    
    // Initialize sender window
    SM[i].swnd.window_size = 10;
    SM[i].swnd.base_seqnum = 1;
    SM[i].swnd.next_seqnum = 1;
    SM[i].swnd.sent_timestamp.tv_sec = 0;
    SM[i].swnd.sent_timestamp.tv_usec = 0;
    SM[i].swnd.is_timer_on = false;
    
    // Initialize the receiver window
    SM[i].rwnd.window_size = 10;
    SM[i].rwnd.exp_seq_num = 1;

    SM[i].flag_nospace=false;
    SM[i].is_sender=false;
    SM[i].is_send_comp=false;
    
    //turn off mutex for that socket
    pthread_mutex_unlock(&SM[i].mutex);

    return i; // Return the index as the KTP socket descriptor
}


// Function to bind a KTP socket
int k_bind(int ksockfd, const char *src_ip, int src_port, const char *dest_ip, int dest_port) {
    if (ksockfd < 0 || ksockfd >= MAX_KSOCKETS || !SM[ksockfd].is_allocated) {
        errno = EBADF; // Invalid file descriptor
        return -1;
    }

    printf("k_bind: Using socket fd %d\n", SM[ksockfd].udp_socket_fd);

    // Find which UDP socket this KTP socket is using
    int udp_index = -1;
    for (int i = 0; i < MAX_USERS; i++) {
        if (UDP_SOCKETS[i].udp_socket_fd == SM[ksockfd].udp_socket_fd) {
            udp_index = i;
            break;
        }
    }

    if (udp_index == -1) {
        fprintf(stderr, "k_bind: Could not find UDP socket index\n");
        errno = EBADF;
        return -1;
    }

    //turn on mutex for that udpsocket
    pthread_mutex_lock(&UDP_SOCKETS[udp_index].mutex);
    // Prepare address structure
    memset(&UDP_SOCKETS[udp_index].addr, 0, sizeof(UDP_SOCKETS[udp_index].addr));
    UDP_SOCKETS[udp_index].addr.sin_family = AF_INET;
    UDP_SOCKETS[udp_index].addr.sin_port = htons(src_port);
    
    if (inet_pton(AF_INET, src_ip, &UDP_SOCKETS[udp_index].addr.sin_addr) <= 0) {
        perror("k_bind: inet_pton failed");
        errno = EINVALID; // Invalid address
        //turn off mutex for that udpsocket
        pthread_mutex_unlock(&UDP_SOCKETS[udp_index].mutex);
        return -1;
    }

    //turn off mutex for that udpsocket
    pthread_mutex_unlock(&UDP_SOCKETS[udp_index].mutex);

    //turn on mutex for that socket
    pthread_mutex_lock(&SM[ksockfd].mutex);

    // Store the destination address in SM
    strncpy(SM[ksockfd].remote_endpoint.ip, dest_ip, sizeof(SM[ksockfd].remote_endpoint.ip) - 1);
    SM[ksockfd].remote_endpoint.ip[sizeof(SM[ksockfd].remote_endpoint.ip) - 1] = '\0';
    SM[ksockfd].remote_endpoint.port = dest_port;

    //turn off mutex for that socket
    pthread_mutex_unlock(&SM[ksockfd].mutex);

    //turn on mutex for that udpsocket
    pthread_mutex_lock(&UDP_SOCKETS[udp_index].mutex);
    
    // Set flags to request binding
    UDP_SOCKETS[udp_index].bind_requested = true;
    UDP_SOCKETS[udp_index].is_bound = false;

    //turn off mutex for that udpsocket
    pthread_mutex_unlock(&UDP_SOCKETS[udp_index].mutex);
    

    // Wait for binding to complete (with timeout)
    int retries = 0;
    bool is_bound = false;
    
    while (retries < 100) {
        pthread_mutex_lock(&UDP_SOCKETS[udp_index].mutex);
        is_bound = UDP_SOCKETS[udp_index].is_bound;
        pthread_mutex_unlock(&UDP_SOCKETS[udp_index].mutex);
        
        if (is_bound) break;
        
        usleep(10000); // Sleep 10ms
        retries++;
    }
    
    if (!is_bound) {
        fprintf(stderr, "k_bind: Binding timed out\n");
        errno = ETIMEDOUT;
        return -1;
    }

    printf("k_bind: Successfully bound socket %d to %s:%d\n", 
           ksockfd, src_ip, src_port);
    return 0; // Success
}

// k_sendto() now places the message into the 2D send_buffer and updates the sender window.
// (For simplicity, the actual UDP transmission with a header is assumed to be handled by thread S.)
int k_sendto(int ksockfd, const char *message, const char *dest_ip, int dest_port) {
    // Validate socket descriptor first
    if (ksockfd < 0 || ksockfd >= MAX_KSOCKETS) {
        errno = EBADF;
        return -1;
    }

    // Lock the socket mutex before checking allocation status and accessing socket data
    pthread_mutex_lock(&SM[ksockfd].mutex);
    
    // Check if socket is allocated
    if (!SM[ksockfd].is_allocated) {
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        errno = EBADF;
        return -1;
    }

    //mark it as sender
    if(!SM[ksockfd].is_sender) {
        SM[ksockfd].is_sender = true;
    }

    // Check destination match
    if (strcmp(SM[ksockfd].remote_endpoint.ip, dest_ip) != 0 ||
        SM[ksockfd].remote_endpoint.port != dest_port) {
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        errno = ENOTBOUND;
        return -1;
    }

    //next sequence number
    int next_seq_num = SM[ksockfd].swnd.next_seqnum;
    

    // Check if this sequence number is already in use in the current window
    bool seq_num_in_use = false;
    int retries = 0;
    const int max_retries = 50; // Maximum number of retries before giving up
    
    while (retries < max_retries) {
        seq_num_in_use = false;
        for (int j = 0; j < MAX_SIZE; j++) {
            if (SM[ksockfd].send_seq_nums[j] == next_seq_num) {
                seq_num_in_use = true;
                break;
            }
        }
        
        if (!seq_num_in_use) {
            break; // Sequence number is available
        }
        
        // Release mutex while waiting
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        
        // Wait a bit for sequence numbers to be freed up
        usleep(10000); // 10ms
        retries++;
        
        // Re-lock mutex to check again
        pthread_mutex_lock(&SM[ksockfd].mutex);
    }
    
    // If the sequence number is still in use after max retries, return error
    if (seq_num_in_use) {
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        errno = EAGAIN; // Try again later
        return -1;
    }

    // Find an empty slot in the sender buffer.
    int i;
    for (i = 0; i < MAX_SIZE; i++) {
        /*change this condition to check in better way*/
        if (SM[ksockfd].send_seq_nums[i] == -1) {
            break;
        }
    }

    // Check if buffer is full
    if (i == MAX_SIZE) {
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        errno = ENOSPACE; // No space left in the sender buffer
        return -1;
    }

    // Copy the message into the sender buffer.
    strncpy(SM[ksockfd].send_buffer[i], message, MESSAGE_SIZE);
    //assign seq number to that message
    SM[ksockfd].send_seq_nums[i] = SM[ksockfd].swnd.next_seqnum;
    SM[ksockfd].swnd.next_seqnum = (SM[ksockfd].swnd.next_seqnum+1)%(256);
    // Mark the message as not sent
    SM[ksockfd].is_sent[i] = false;

    printf("assigned seq num %d to message %s\n",SM[ksockfd].send_seq_nums[i],SM[ksockfd].send_buffer[i]);

    //indicating that send operation is not complete
    if(SM[ksockfd].is_send_comp) {
        SM[ksockfd].is_send_comp = false;
    }

    // Unlock the mutex before returning
    pthread_mutex_unlock(&SM[ksockfd].mutex);
    
    return 0; // Success
    
}


void* k_recvfrom(int ksockfd) {
    if (ksockfd < 0 || ksockfd >= MAX_KSOCKETS || !SM[ksockfd].is_allocated) {
         errno = EBADF;
         return NULL;
    }

    // Lock the mutex before accessing shared socket data
    pthread_mutex_lock(&SM[ksockfd].mutex);
    
    // Check allocation status under mutex protection
    if (!SM[ksockfd].is_allocated) {
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        errno = EBADF;
        return NULL;
    }

    // Find the min non-negative seqnum message index in the receive buffer.
    int i;
    int min_seq_num = 257;
    for (int j = 0; j < 10; j++) {
        if (SM[ksockfd].recv_seq_nums[j] >= 0) {
            if (SM[ksockfd].recv_seq_nums[j] < min_seq_num) {
                min_seq_num = SM[ksockfd].recv_seq_nums[j];
                i = j;
            }
        }
    }

    // No message found or not within expected sequence
    if (min_seq_num == 257 || min_seq_num > SM[ksockfd].rwnd.exp_seq_num) {
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        errno = ENOMESSAGE;
        return "-1"; // No message available
    }



    struct timeval current_time;
    gettimeofday(&current_time, NULL);
    
    char *message = (char *)malloc(MESSAGE_SIZE);
    strncpy(message, SM[ksockfd].recv_buffer[i], MESSAGE_SIZE);

    // printf("k_recvfrom: %s, %d, exp_seqnum: %d , current_time: %ld.%06ld\n",message,SM[ksockfd].recv_seq_nums[i],SM[ksockfd].rwnd.exp_seq_num,current_time.tv_sec, current_time.tv_usec);

    // Copy the message to a new buffer and clear the slot in the receive buffer.
    SM[ksockfd].recv_buffer[i][0] = '\0';
    SM[ksockfd].recv_seq_nums[i] = -1;
    // SM[ksockfd].rwnd.exp_seq_num = (SM[ksockfd].rwnd.exp_seq_num+1)%(256);

    // Unlock the mutex before returning
    pthread_mutex_unlock(&SM[ksockfd].mutex);

    return message;
    
}


// Function to close a KTP socket
int k_close(int ksockfd) {
    if (ksockfd < 0 || ksockfd >= MAX_KSOCKETS || !SM[ksockfd].is_allocated) {
        errno = EBADF; // Invalid file descriptor
        return -1;
    }

    // Lock the mutex before checking allocation and modifying socket
    pthread_mutex_lock(&SM[ksockfd].mutex);
    
    // Check if socket is allocated
    if (!SM[ksockfd].is_allocated) {
        pthread_mutex_unlock(&SM[ksockfd].mutex);
        errno = EBADF;
        return -1;
    }

    pthread_mutex_unlock(&SM[ksockfd].mutex);

    bool is_sender;
    pthread_mutex_lock(&SM[ksockfd].mutex);
    is_sender = SM[ksockfd].is_sender;
    pthread_mutex_unlock(&SM[ksockfd].mutex);

    // if it is a sender
    if(is_sender) {

        printf("Waiting for all messages to be acknowledged...\n");

        while(1) {

            while(1) {
                pthread_mutex_lock(&SM[ksockfd].mutex);
                if(SM[ksockfd].is_send_comp) {
                    pthread_mutex_unlock(&SM[ksockfd].mutex);
                    break;
                } else {
                    pthread_mutex_unlock(&SM[ksockfd].mutex);
                }
            }

            int retries = 0;

            bool is_send_comp;
            do {

                pthread_mutex_lock(&SM[ksockfd].mutex);
                is_send_comp = SM[ksockfd].is_send_comp;
                pthread_mutex_unlock(&SM[ksockfd].mutex);

                if(retries > 100) {
                    break;
                }
                usleep(10000); // Sleep 10ms
                retries++;
            } while(is_send_comp);

            if(retries > 100) {
                break;
            }
        }
    }



    pthread_mutex_lock(&SM[ksockfd].mutex);

    printf("Closing KTP socket SM[%d]\n", ksockfd);

    // Get the UDP socket fd before clearing the memory
    int udp_fd = SM[ksockfd].udp_socket_fd;

    // Clear the KTP socket data (but preserve the mutex)
    pthread_mutex_t mutex_copy = SM[ksockfd].mutex;  // Save mutex
    memset(&SM[ksockfd], 0, sizeof(KtpSocket));
    SM[ksockfd].mutex = mutex_copy;  // Restore mutex
    
    // Mark the entry as free
    SM[ksockfd].is_allocated = false;
    
    // Unlock the mutex after modifying the KTP socket
    pthread_mutex_unlock(&SM[ksockfd].mutex);
    
    
    // Mark the UDP socket as available again (with proper locking)
    for (int i = 0; i < MAX_USERS; i++) {
        pthread_mutex_lock(&UDP_SOCKETS[i].mutex);
        if (UDP_SOCKETS[i].udp_socket_fd == udp_fd) {
            UDP_SOCKETS[i].is_used = false;
            pthread_mutex_unlock(&UDP_SOCKETS[i].mutex);
            break;
        }
        pthread_mutex_unlock(&UDP_SOCKETS[i].mutex);
    }

    return 0; // Success
}


// Function to print the details of a KTP socket for debugging
void print_socket(int i) {
    if (i < 0 || i >= MAX_KSOCKETS || !SM[i].is_allocated) {
        printf("Invalid or unallocated socket index: %d\n", i);
        return;
    }

    printf("KTP Socket SM[%d]:\n", i);
    printf("  Process ID: %d\n", SM[i].pid);
    printf("  UDP Socket FD: %d\n", SM[i].udp_socket_fd);
    printf("  Remote Endpoint: %s:%d\n", SM[i].remote_endpoint.ip, SM[i].remote_endpoint.port);
/*    
    printf("  Sender Window Size: %d\n", SM[i].swnd.window_size);
    printf("  Receiver Window Size: %d\n", SM[i].rwnd.window_size);

    // Print send buffer messages
    printf("  Send Buffer Messages:\n");
    for (int j = 0; j < MAX_WINDOW_SIZE; j++) {
        if (strlen(SM[i].send_buffer[j]) > 0) {
            printf("    [%d]: %s\n", j, SM[i].send_buffer[j]);
        }
    }

    // Print receive buffer messages
    printf("  Receive Buffer Messages:\n");
    for (int j = 0; j < MAX_WINDOW_SIZE; j++) {
        if (strlen(SM[i].recv_buffer[j]) > 0) {
            printf("    [%d]: %s\n", j, SM[i].recv_buffer[j]);
        }
    }
*/
}