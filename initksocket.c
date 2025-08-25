/*=====================================

Assignment 4 Submission

Name: Marati Nikhil Kumar

Roll number: 22CS10042

=====================================*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <signal.h>
#include <errno.h>
#include "ksocket.h"

#define SHM_KEY 1255  // Shared memory key
#define UDP_SHM_KEY 1256  // Shared memory key for UDP sockets

// Function to check if a process is still alive
int is_process_alive(pid_t pid) {
    return kill(pid, 0) == 0 || errno != ESRCH;
}

// Garbage collector function
void garbage_collector(KtpSocket *SM) {
    while (1) {
        sleep(5); // Run every 5 seconds

        for (int i = 0; i < MAX_KSOCKETS; i++) {
            if (SM[i].is_allocated && !is_process_alive(SM[i].pid)) {
                printf("Garbage collector: Cleaning up socket %d (PID %d terminated).\n", i, SM[i].pid);
                
                // Free up the UDP socket
                for (int j = 0; j < MAX_USERS; j++) {
                    if (UDP_SOCKETS[j].udp_socket_fd == SM[i].udp_socket_fd) {
                        UDP_SOCKETS[j].is_used = false;
                        break;
                    }
                }
                
                SM[i].is_allocated = false;
                //also clean up the buffers
            }
        }
    }
}

void process_bind_requests() {
    for (int i = 0; i < MAX_USERS; i++) {
        // Lock the mutex before checking and modifying UDP socket data
        pthread_mutex_lock(&UDP_SOCKETS[i].mutex);
        
        // Check if binding is requested but not yet completed
        if (UDP_SOCKETS[i].bind_requested && !UDP_SOCKETS[i].is_bound) {
            
            // Perform the actual bind (still under mutex protection)
            if (bind(UDP_SOCKETS[i].udp_socket_fd, 
                    (struct sockaddr *)&UDP_SOCKETS[i].addr, 
                    sizeof(UDP_SOCKETS[i].addr)) == -1) {
                perror("Init process: bind failed in process_bind_requests");
                // Keep bind_requested true so we can retry
                pthread_mutex_unlock(&UDP_SOCKETS[i].mutex);
                continue;
            }
            
            // Mark as bound and clear request flag (still under mutex protection)
            UDP_SOCKETS[i].is_bound = true;
            UDP_SOCKETS[i].bind_requested = false;
            printf("Init process: Successfully bound UDP socket %d\n", i);
        }
        
        // Unlock the mutex after we're done with this socket
        pthread_mutex_unlock(&UDP_SOCKETS[i].mutex);
    }
}

// Add this function before main
void *process_bind_requests_loop(void *arg) {
    while (1) {
        process_bind_requests();
        usleep(10000); // Sleep 10ms between checks
    }
    return NULL;
}

/*receiver function for thread R */
void* receiver(void* arg) {
    struct timeval timeout;
    fd_set read_fds;
    int max_fd = 0;
    KtpMessage msg;
    
    while (1) {
        // Reset file descriptor set
        FD_ZERO(&read_fds);
        max_fd = 0;
        
        // Add all active UDP sockets to the fd set
        for (int i = 0; i < MAX_KSOCKETS; i++) {
            // Lock mutex to check allocation status
            pthread_mutex_lock(&SM[i].mutex);
            bool is_allocated = SM[i].is_allocated;
            int udp_fd = SM[i].udp_socket_fd;
            pthread_mutex_unlock(&SM[i].mutex);

            if (is_allocated && udp_fd >= 0) {
                // Check if the file descriptor is valid using fcntl
                int flags = fcntl(udp_fd, F_GETFL);
                if (flags == -1 && errno == EBADF) {
                    fprintf(stderr, "Socket fd %d is invalid!\n", udp_fd);
                    continue;  // Skip this socket if it's invalid
                }
                FD_SET(udp_fd, &read_fds);
                if (udp_fd > max_fd) {
                    max_fd = udp_fd;
                }
            }
        }
        
        // Set timeout for select
        timeout.tv_sec = 0;
        timeout.tv_usec = TIMEOUT * 1000;  // Convert ms to us
        
        // Wait for activity on any socket
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        // if (activity < 0) {
        //     perror("select error");
        //     continue;
        // }

        if (activity < 0) {
            fprintf(stderr, "select error: %s (errno: %d)\n", strerror(errno), errno);
            continue;
        }
        
        // Timeout occurred - check for nospace flag
        if (activity == 0) {
            for (int i = 0; i < MAX_KSOCKETS; i++) {
                // Lock mutex before checking socket status
                pthread_mutex_lock(&SM[i].mutex);
                
                if (SM[i].is_allocated && SM[i].flag_nospace) {
                    // Count empty slots in receive buffer
                    int empty_slots = 0;
                    for (int j = 0; j < 10; j++) {
                        if (SM[i].recv_seq_nums[j]==-1) {
                            empty_slots++;
                        }
                    }
                    printf("\n\nsocket %d: heyyyyy empty_slots: %d\n\n", i,empty_slots);
                    
                    // If space is now available, send duplicate ACK
                    if (empty_slots > 0) {
                        // Gather needed data under lock
                        int exp_seq_num = SM[i].rwnd.exp_seq_num;
                        char remote_ip[16];
                        strncpy(remote_ip, SM[i].remote_endpoint.ip, sizeof(remote_ip));
                        int remote_port = SM[i].remote_endpoint.port;
                        int udp_socket_fd = SM[i].udp_socket_fd;
                        
                        // Unlock before network I/O
                        pthread_mutex_unlock(&SM[i].mutex);

                        printf("socket %d: Sending duplicate ACK for %d,aws: %d\n", i, exp_seq_num - 1, empty_slots);
                        
                        // Send duplicate ACK (network operations don't need mutex)
                        KtpMessage ack_msg;
                        ack_msg.is_data_msg=false;
                        ack_msg.is_ack_msg=true;
                        ack_msg.seq_num = exp_seq_num - 1; // Last ACKed sequence
                        ack_msg.rwnd = empty_slots;
                        
                        struct sockaddr_in dest_addr;
                        memset(&dest_addr, 0, sizeof(dest_addr));
                        dest_addr.sin_family = AF_INET;
                        dest_addr.sin_port = htons(remote_port);
                        inet_pton(AF_INET,remote_ip, &dest_addr.sin_addr);
                        
                        sendto(udp_socket_fd, &ack_msg, sizeof(ack_msg), 0,
                               (struct sockaddr*)&dest_addr, sizeof(dest_addr));
                        
                    } else {
                        // Unlock if we don't send an ACK
                        pthread_mutex_unlock(&SM[i].mutex);
                    }
                } else {
                    // Unlock if not allocated or no nospace flag
                    pthread_mutex_unlock(&SM[i].mutex);
                }
            }
            continue;
        }
        
        // Check which socket has data
        for (int i = 0; i < MAX_KSOCKETS; i++) {
            pthread_mutex_lock(&SM[i].mutex);
            bool is_allocated = SM[i].is_allocated;
            int udp_socket_fd = SM[i].udp_socket_fd;
            pthread_mutex_unlock(&SM[i].mutex);
            
            if (is_allocated && FD_ISSET(SM[i].udp_socket_fd, &read_fds)) {
                struct sockaddr_in src_addr;
                socklen_t src_len = sizeof(src_addr);
                
                // Receive the message (network operations don't need mutex)
                memset(&msg, 0, sizeof(msg));
                int bytes = recvfrom(udp_socket_fd, &msg, sizeof(msg), 0,
                                     (struct sockaddr*)&src_addr, &src_len);
                
                if(dropMessage(DROP_PROBABILITY)){
                    if(msg.is_data_msg) printf("socket %d: Dropped data message\n",i);
                    else printf("socket %d: Dropped ack message\n",i);
                    continue;
                }

                if (bytes < 0) {
                    continue;
                }
                
                
                // Lock mutex before processing the received message
                pthread_mutex_lock(&SM[i].mutex);
                
                
                if (msg.is_data_msg) {
                    //as we received a message, means sender has been notified about the space
                    //don't unset nospace flag for duplicates of previous messages
                    if(SM[i].flag_nospace && msg.seq_num >= SM[i].rwnd.exp_seq_num) {
                        printf("socket %d: flag nospace is unset,w_size: %d\n", i,SM[i].rwnd.window_size);
                        SM[i].flag_nospace=false;
                    }
                    
                    // Received a data message
                    // Find appropriate slot in receive buffer
                    int slot = msg.seq_num - SM[i].rwnd.exp_seq_num;
                    
                    // Check if message is within window and also drops any duplicate data messages
                    if (slot>=0 && slot < SM[i].rwnd.window_size) {
                        printf("socket %d: got %s, %d\n",i,msg.data,msg.seq_num);

                        bool is_Already=false;

                        // Check if message is duplicate
                        for(int j=0;j<10;j++){
                            if(SM[i].recv_seq_nums[j]==msg.seq_num){
                                is_Already=true;
                                printf("    Duplicate message %s, %d\n",msg.data,msg.seq_num);
                                break;
                            }
                        }
                        
                        // Store message in receive buffer in first empty slot if not duplicate
                        if(!is_Already  && !SM[i].flag_nospace) {
                            for(int j=0;j<10;j++){
                                if(SM[i].recv_seq_nums[j]==-1){
                                    printf("\n\nwrting into recv_buffer %s, %d\n\n",msg.data,msg.seq_num);
                                    strcpy(SM[i].recv_buffer[j],msg.data);
                                    //storing seq number of that message
                                    SM[i].recv_seq_nums[j]=msg.seq_num;
                                    break;
                                }
                            }

                            // If received message is the expected one, calculate new expected sequence number
                            if (msg.seq_num == SM[i].rwnd.exp_seq_num) {
                                int new_exp_seq_num = SM[i].rwnd.exp_seq_num + 1;
                                while (true) {
                                    bool found = false;
                                    for (int j = 0; j < 10; j++) {
                                        if (SM[i].recv_seq_nums[j] == new_exp_seq_num) {
                                            new_exp_seq_num++;
                                            found = true;
                                            break;
                                        }
                                    }
                                    if (!found) {
                                        break;
                                    }
                                }
                                SM[i].rwnd.exp_seq_num = new_exp_seq_num;
                            
                            }

                        } 
                    }

                    // Count available slots
                    int empty_slots = 0;
                    for (int j = 0; j < 10; j++) {
                        if (SM[i].recv_seq_nums[j] == -1) {
                            empty_slots++;
                        }
                    }
                    
                    // Update rwnd size
                    SM[i].rwnd.window_size = empty_slots;
                    
                    // Set nospace flag if buffer is full
                    if (empty_slots == 0) {
                        SM[i].flag_nospace = true;
                        printf("socket %d: No space flag is set\n", i);
                    }

                    //sending ack in case of prev duplicate messages or expected message
                    if(slot<=0) {
                        struct timeval current_time;
                        gettimeofday(&current_time, NULL);

                        // Capture needed data under mutex lock
                        int exp_seq_num = SM[i].rwnd.exp_seq_num;
                        int window_size = SM[i].rwnd.window_size;
                        int udp_socket_fd = SM[i].udp_socket_fd;
                        
                        printf("socket %d: sent ack %d, aws: %d,exp_seq_num: %d, current_time: %ld.%06ld\n",
                               i, msg.seq_num, SM[i].rwnd.window_size,SM[i].rwnd.exp_seq_num, current_time.tv_sec, current_time.tv_usec);
                        
                        // Unlock before network I/O
                        pthread_mutex_unlock(&SM[i].mutex);

                        // Send ACK
                        KtpMessage ack_msg;
                        ack_msg.is_data_msg = false;
                        ack_msg.is_ack_msg = true;
                        ack_msg.seq_num = -1;
                        ack_msg.ack_num = exp_seq_num - 1;
                        ack_msg.rwnd = window_size;
                        
                        sendto(udp_socket_fd, &ack_msg, sizeof(ack_msg), 0,
                        (struct sockaddr*)&src_addr, src_len);
                    } else {
                        // Unlock if we don't send an ACK
                        pthread_mutex_unlock(&SM[i].mutex);
                    }
                
                } else if (msg.is_ack_msg) {
                    // Received an ACK message
                    // Check if this is a duplicate ACK
                    if (msg.ack_num<SM[i].swnd.base_seqnum) {
                        printf("socket %d: duplicate ack %d received, aws: %d\n",i,msg.ack_num,msg.rwnd);
                        // Just update the sender window size
                        SM[i].swnd.window_size = msg.rwnd;
                        pthread_mutex_unlock(&SM[i].mutex);
                    } else {
                        printf("socket %d: got ack %d, aws: %d\n",i,msg.ack_num,msg.rwnd);

                        // Update sender window and remove acknowledged messages
                        for (int j = 0; j < MAX_SIZE; j++) {
                            if (SM[i].send_seq_nums[j] > 0 && SM[i].send_seq_nums[j] <= msg.ack_num) {
                                // Clear the acknowledged message
                                SM[i].send_buffer[j][0] = '\0';
                                SM[i].send_seq_nums[j] = -1;
                            }
                        }
                        
                        // Update window size
                        SM[i].swnd.window_size = msg.rwnd;
                        
                        //updating base seq num
                        SM[i].swnd.base_seqnum = (msg.ack_num+1)%(256);
                        
                        //turn off timer if receiver has no space
                        if(SM[i].swnd.window_size==0) {
                            SM[i].swnd.sent_timestamp.tv_sec=0;
                            SM[i].swnd.sent_timestamp.tv_usec=0;
                            SM[i].swnd.is_timer_on=false;
                            printf("socket %d: No space at receiver,timer set off\n",i);
                        }

                        //update the timer, first find whether base_seqnum is in window or not
                        if(SM[i].swnd.base_seqnum==SM[i].swnd.next_seqnum){
                            SM[i].swnd.sent_timestamp.tv_sec=0;
                            SM[i].swnd.sent_timestamp.tv_usec=0;
                            SM[i].swnd.is_timer_on=false;
                            printf("No message in window, timer is off\n");
                            SM[i].is_send_comp=true;
                            pthread_mutex_unlock(&SM[i].mutex);
                        } else {
                            //find the message with base_seqnum and update sent_timestamp with iots send_timestamp[j]
                            for(int j=0;j<MAX_SIZE;j++){
                                if(SM[i].send_seq_nums[j]==SM[i].swnd.base_seqnum){
                                    SM[i].swnd.sent_timestamp=SM[i].send_timestamp[j];
                                    SM[i].swnd.is_timer_on=true;
                                    break;
                                }
                            }
                            pthread_mutex_unlock(&SM[i].mutex);
                        }

                    }
                } else {
                    // Unknown message type - unlock mutex
                    pthread_mutex_unlock(&SM[i].mutex);
                }
            }
        }
    }
    
    return NULL;
}


/*sender function for thread S*/
void* sender(void* arg) {
    struct timeval current_time;
    
    while (1) {
        // Sleep for T/2
        usleep(TIMEOUT/2);
        
        // Get current time
        gettimeofday(&current_time, NULL);
        
        // Check all active KTP sockets
        for (int i = 0; i < MAX_KSOCKETS; i++) {
            // Lock mutex to check allocation status
            pthread_mutex_lock(&SM[i].mutex);
            
            bool is_allocated = SM[i].is_allocated;
            if (!is_allocated) {
                pthread_mutex_unlock(&SM[i].mutex);
                continue;
            }

            // Calculate elapsed time since last transmission
            long elapsed_ms = (current_time.tv_sec - SM[i].swnd.sent_timestamp.tv_sec) * 1000 + 
                                (current_time.tv_usec - SM[i].swnd.sent_timestamp.tv_usec) / 1000;
            
            // Check for timeout (T ms)
            if (SM[i].swnd.is_timer_on && elapsed_ms >= TIMEOUT && SM[i].swnd.sent_timestamp.tv_sec > 0) {
                printf("socket %d: Timeout, due to msg seqnum: %d\n", i, SM[i].swnd.base_seqnum);

                // Gather information needed for retransmission under lock protection
                char remote_ip[16];
                strncpy(remote_ip, SM[i].remote_endpoint.ip, sizeof(remote_ip));
                int remote_port = SM[i].remote_endpoint.port;
                int socket_fd = SM[i].udp_socket_fd;
                int base_seqnum = SM[i].swnd.base_seqnum;
                int window_size = SM[i].swnd.window_size;

                // For each message, check if it needs retransmission and prepare its data
                struct {
                    bool needs_retransmit;
                    int seq_num;
                    char data[MESSAGE_SIZE];
                } messages_to_retransmit[MAX_SIZE];
                
                // Initialize retransmission array
                memset(messages_to_retransmit, 0, sizeof(messages_to_retransmit));
                
                // Identify messages that need retransmission
                for (int j = 0; j < MAX_SIZE; j++) {
                    if (SM[i].send_seq_nums[j] >= 0 && 
                        SM[i].send_seq_nums[j] < base_seqnum + window_size && 
                        SM[i].is_sent[j]) {
                        
                        messages_to_retransmit[j].needs_retransmit = true;
                        messages_to_retransmit[j].seq_num = SM[i].send_seq_nums[j];
                        strncpy(messages_to_retransmit[j].data, SM[i].send_buffer[j], MESSAGE_SIZE);
                        
                        // Update timestamps under lock
                        SM[i].send_timestamp[j] = current_time;
                    }
                }

                // Update the sent timestamp
                SM[i].swnd.sent_timestamp = current_time;
                SM[i].swnd.is_timer_on = true;
                
                // Now unlock before network operations
                pthread_mutex_unlock(&SM[i].mutex);

                // Retransmit all messages in the current window
                struct sockaddr_in dest_addr;
                memset(&dest_addr, 0, sizeof(dest_addr));
                dest_addr.sin_family = AF_INET;
                dest_addr.sin_port = htons(remote_port);
                inet_pton(AF_INET, remote_ip, &dest_addr.sin_addr);

                bool isretransmitted=false;

                // Retransmit all messages in the current window
                for (int j = 0; j < MAX_SIZE; j++) {
                    if (messages_to_retransmit[j].needs_retransmit) {
                        isretransmitted=true;
                        printf("    retransmit %s, %d\n", 
                               messages_to_retransmit[j].data, 
                               messages_to_retransmit[j].seq_num);
                        
                        // Create a data message
                        KtpMessage msg;
                        memset(&msg, 0, sizeof(msg));
                        msg.is_data_msg = true;
                        msg.is_ack_msg = false;
                        msg.seq_num = messages_to_retransmit[j].seq_num;
                        strncpy(msg.data, messages_to_retransmit[j].data, MESSAGE_SIZE);
                        
                        sendto(socket_fd, &msg, sizeof(msg), 0,
                               (struct sockaddr*)&dest_addr, sizeof(dest_addr));
                    }
                }

                if(!isretransmitted) printf("no message to retransmit\n");
            }

            
            // Check for new messages to send

            // Gather information needed for sending under lock protection
            char remote_ip[16];
            strncpy(remote_ip, SM[i].remote_endpoint.ip, sizeof(remote_ip));
            int remote_port = SM[i].remote_endpoint.port;
            int socket_fd = SM[i].udp_socket_fd;
            int base_seqnum = SM[i].swnd.base_seqnum;
            int window_size = SM[i].swnd.window_size;
            
            // Array to track which messages need to be sent
            struct {
                bool needs_send;
                int seq_num;
                char data[MESSAGE_SIZE];
                bool is_base_seq;  // Is this the base sequence number?
            } messages_to_send[MAX_SIZE];
            
            // Initialize send array
            memset(messages_to_send, 0, sizeof(messages_to_send));
            
            // Identify messages that need to be sent
            for (int j = 0; j < MAX_SIZE; j++) {
                if (SM[i].send_seq_nums[j] >= base_seqnum && 
                    SM[i].send_seq_nums[j] < base_seqnum + window_size &&
                    !SM[i].is_sent[j]) {
                    
                    messages_to_send[j].needs_send = true;
                    messages_to_send[j].seq_num = SM[i].send_seq_nums[j];
                    strncpy(messages_to_send[j].data, SM[i].send_buffer[j], MESSAGE_SIZE);
                    messages_to_send[j].is_base_seq = (SM[i].send_seq_nums[j] == base_seqnum);
                    
                    // Update timestamps and flags under lock
                    SM[i].send_timestamp[j] = current_time;
                    SM[i].is_sent[j] = true;
                    
                    // Update timer if this is the first message in the window
                    if (messages_to_send[j].is_base_seq) {
                        SM[i].swnd.sent_timestamp = current_time;
                        SM[i].swnd.is_timer_on = true;
                    }
                }
            }
            
            // Now unlock before network operations
            pthread_mutex_unlock(&SM[i].mutex);
            

            // Send all identified messages
            for (int j = 0; j < MAX_SIZE; j++) {
                if (messages_to_send[j].needs_send) {
                    printf("socket %d: sent %s, %d\n", i,
                           messages_to_send[j].data,
                           messages_to_send[j].seq_num);
                    
                    // Create a data message
                    KtpMessage msg;
                    memset(&msg, 0, sizeof(msg));
                    msg.is_data_msg = true;
                    msg.is_ack_msg = false;
                    msg.seq_num = messages_to_send[j].seq_num;
                    strncpy(msg.data, messages_to_send[j].data, MESSAGE_SIZE);

                    struct sockaddr_in dest_addr;
                    memset(&dest_addr, 0, sizeof(dest_addr));
                    dest_addr.sin_family = AF_INET;
                    dest_addr.sin_port = htons(remote_port);
                    inet_pton(AF_INET, remote_ip, &dest_addr.sin_addr);

                    sendto(socket_fd, &msg, sizeof(msg), 0,
                           (struct sockaddr*)&dest_addr, sizeof(dest_addr));
                }
            }
        }
    }
    return NULL;
}

int main() {

    // Create shared memory for UDP sockets first
    int udp_shmid = shmget(UDP_SHM_KEY, MAX_USERS * sizeof(UdpSocket), IPC_CREAT | 0666);
    if (udp_shmid == -1) {
        fprintf(stderr, "shmget for UDP sockets failed: %s (Error Code: %d)\n", strerror(errno), errno);
        exit(1);
    }

    // Attach UDP sockets shared memory
    UDP_SOCKETS = (UdpSocket *)shmat(udp_shmid, NULL, 0);
    if (UDP_SOCKETS == (void *)-1) {
        perror("shmat for UDP sockets failed");
        exit(1);
    }

    // Create the fixed UDP sockets first
    for (int i = 0; i < MAX_USERS; i++) {
        int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_fd == -1) {
            perror("Failed to create UDP socket");
            exit(1);
        }

        // Store the socket fd and mark as available         
        UDP_SOCKETS[i].udp_socket_fd = udp_fd;
        UDP_SOCKETS[i].is_used = false;
        UDP_SOCKETS[i].bind_requested = false;
        UDP_SOCKETS[i].is_bound = false;

        // Initialize mutex
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&UDP_SOCKETS[i].mutex, &attr);
        pthread_mutexattr_destroy(&attr);
        
        printf("Created UDP socket %d with fd %d\n", i, udp_fd);
    }

    // Create shared memory segment
    int shmid = shmget(SHM_KEY, MAX_KSOCKETS * sizeof(KtpSocket), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }

    // Attach shared memory
    SM = (KtpSocket *)shmat(shmid, NULL, 0);
    if (SM == (void *)-1) {
        perror("shmat failed");
        exit(1);
    }

    // printf("Shared memory address: %p\n", (void*)SM);

    // Initialize shared memory (mark all sockets as unallocated)
    for (int i = 0; i < MAX_KSOCKETS; i++) {
        SM[i].is_allocated = false;
        //initalise mutex
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&SM[i].mutex, &attr);
        pthread_mutexattr_destroy(&attr);
    }

    printf("Init process: Shared memory initialized.\n");

    /*initalise thread R*/
    pthread_create(&R, NULL, receiver, NULL);

    /*initialise thread S*/
    pthread_create(&S, NULL, sender, NULL);

    // Create a thread for processing bind requests
    pthread_t bind_thread;
    pthread_create(&bind_thread, NULL, (void *(*)(void *))process_bind_requests_loop, NULL);


    // Start garbage collection loop
    garbage_collector(SM);

    //destroy mutex
    for (int i = 0; i < MAX_KSOCKETS; i++) {
        pthread_mutex_destroy(&SM[i].mutex);
    }

    for (int i = 0; i < MAX_USERS; i++) {
        pthread_mutex_destroy(&UDP_SOCKETS[i].mutex);
    }

    // Detach shared memory (this line won't be reached due to infinite loop)
    shmdt(SM);

    // Close all UDP sockets (also won't be reached)
    for (int i = 0; i < MAX_USERS; i++) {
        close(UDP_SOCKETS[i].udp_socket_fd);
    }
    return 0;
}
