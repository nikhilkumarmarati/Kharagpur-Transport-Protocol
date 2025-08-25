/*=====================================

Assignment 4 Submission

Name: Marati Nikhil Kumar

Roll number: 22CS10042

=====================================*/

#ifndef KSOCKET_H
#define KSOCKET_H

#include <stdbool.h>
#include <netinet/in.h>  // For network address structures (if needed)
#include <sys/time.h>
#include <math.h>
#include <pthread.h>
#include <fcntl.h>


// Maximum values (adjust as necessary)
#define MAX_KSOCKETS 100      // Maximum active KTP sockets
#define MAX_BUFFER_SIZE 1024  // Size of send/receive buffers
#define MAX_SIZE 1024    // Maximum number of unacknowledged messages
#define MESSAGE_SIZE 512      // Fixed size of each message (in bytes)
#define MAX_USERS 5          // Maximum number of concurrent users (UDP sockets)

#define SOCK_KTP 9999  // Define a unique socket type for KTP
#define ENOTBOUND 2001 // Error code for unbound socket
#define ENOSPACE  2002 // Error code for no space in buffer
#define ENOMESSAGE 2003 // Error code for no message available
#define EINVALID  2004 // Error code for invalid argument

#define TIMEOUT 5000      // Timeout in milliseconds
#define DROP_PROBABILITY 0.5 // p

// Structure to track UDP socket usage
typedef struct {
    int udp_socket_fd;    // This will only be valid in the creating process
    bool is_used;         // Indicates if this UDP socket is currently in use
    struct sockaddr_in addr; // Store address for binding
    bool bind_requested;   // Flag to indicate binding is requested
    bool is_bound;         // Flag to indicate if socket is already bound
    pthread_mutex_t mutex; // For synchronizing access to this udpsocket
} UdpSocket;

// Structure representing a remote endpoint (IP address and port)
typedef struct {
    char ip[16];  // Enough to store an IPv4 address (xxx.xxx.xxx.xxx + null terminator)
    int port;
} Endpoint;

// Structure representing the sender window
typedef struct {
    int window_size;   // Current sender window size (max number of unacknowledged messages)
    int base_seqnum;   // Sequence number of the oldest unacknowledged message
    int next_seqnum;   // Sequence number of the next message to be sent
    struct timeval  sent_timestamp;  // Timestamp of the oldest unacknowledged message
    bool is_timer_on ; // Timer is on or off
} SenderWindow;


// Structure representing the receiver window
typedef struct {
    int window_size;  // Current receiver window size (free space in the receiver buffer)
    int exp_seq_num; // The sequence number the receiver expects next
} ReceiverWindow;

// KTP socket structure stored in shared memory
typedef struct {
    bool is_allocated;         // Indicates if this slot is currently in use
    int pid;                   // Process ID of the creator of this socket
    int udp_socket_fd;         // UDP socket descriptor mapped to this KTP socket

    Endpoint remote_endpoint;  // IP address and port of the remote endpoint 

    char send_buffer[MAX_SIZE][MESSAGE_SIZE]; // Buffer for outgoing messages
    int send_seq_nums[MAX_SIZE];    // Array holding the sequence number for each message; -1 indicates an empty slot
    bool is_sent[MAX_SIZE];                   // Flag to indicate whether the message has been sent
    struct timeval send_timestamp[MAX_SIZE];  // Timestamp of each sent message

    // The receiver buffer is now a 2D array: each row holds one message (of MESSAGE_SIZE bytes)
    char recv_buffer[10][MESSAGE_SIZE];
    int recv_seq_nums[10];   // Array holding the sequence number for each received message; -1 indicates an empty slot
    
    SenderWindow swnd;         // Sender window (for handling reliability and retransmissions)
    ReceiverWindow rwnd;       // Receiver window (for handling flow control)

    bool flag_nospace;  // Flag to indicate that there is no space in the receiver buffer
    bool is_sender;  // Flag to indicate that the receive operation is complete
    bool is_send_comp;  // Flag to indicate that the send operation is complete


    pthread_mutex_t mutex;  // For synchronizing access to this socket
} KtpSocket;

/*Message Format*/
typedef struct {
    bool is_data_msg;
    bool is_ack_msg;
    int seq_num;         // Sequence number of the message
    int ack_num;         // Acknowledgment number
    int rwnd;           // Receiver window size
    char data[MESSAGE_SIZE];  // Actual message data
} KtpMessage;

/**shared memory SM**/
extern KtpSocket* SM;

// Global array of UDP sockets
extern UdpSocket* UDP_SOCKETS;

/*define thread R*/
extern pthread_t R;

/*define thread S*/
extern pthread_t S;


int k_socket(int domain, int type, int protocol);

int k_bind(int ksockfd, const char *src_ip, int src_port, const char *dest_ip, int dest_port);

int k_sendto(int ksockfd, const char *message, const char *dest_ip, int dest_port);

void* k_recvfrom(int ksockfd);

int k_close(int ksockfd);

void print_socket(int i);

int dropMessage(float p);

void* receiver(void* arg);

void* sender(void* arg);

#endif // KSOCKET_H



