
# KTP (KGP Transport Protocol) Implementation

## Overview

This project implements a custom transport protocol (KTP) using UDP sockets and shared memory for inter-process communication. It provides reliable data transfer, flow control, and socket management.

---

## Data Structures (`ksocket.h`)

### UdpSocket
Tracks UDP socket usage:
- `udp_socket_fd`: File descriptor (valid only in creator process)
- `is_used`: Usage flag
- `addr`: Socket address for binding
- `bind_requested`: Pending bind request flag
- `is_bound`: Bound status flag
- `mutex`: Synchronization mutex

### Endpoint
Represents a remote endpoint:
- `ip`: IPv4 address
- `port`: Port number

### SenderWindow
Sender window for reliability:
- `window_size`: Max unacknowledged messages
- `base_seqnum`: Oldest unacknowledged sequence number
- `next_seqnum`: Next sequence number to send
- `sent_timestamp`: Timestamp of oldest unacknowledged message
- `is_timer_on`: Retransmission timer status

### ReceiverWindow
Receiver window for flow control:
- `window_size`: Free space in buffer
- `exp_seq_num`: Next expected sequence number

### KtpSocket
Main socket structure (shared memory):
- `is_allocated`: Usage flag
- `pid`: Creator process ID
- `udp_socket_fd`: Associated UDP socket
- `remote_endpoint`: Remote IP and port
- `send_buffer`: Outgoing message buffer
- `send_seq_nums`: Sequence numbers for messages
- `is_sent`: Sent status flags
- `send_timestamp`: Timestamps for sent messages
- `recv_buffer`: Incoming message buffer
- `recv_seq_nums`: Sequence numbers for received messages
- `swnd`: Sender window
- `rwnd`: Receiver window
- `flag_nospace`: Receiver buffer space flag
- `is_sender`: Sender flag
- `is_send_comp`: Send completion flag
- `mutex`: Synchronization mutex

### KtpMessage
Protocol message format:
- `is_data_msg`: Data message flag
- `is_ack_msg`: Acknowledgment message flag
- `seq_num`: Sequence number
- `ack_num`: Acknowledgment number
- `rwnd`: Receiver window size
- `data`: Message data

---

## Main Functions (`ksocket.c`)

- **`attach_shared_memory()`**: Attaches shared memory segments for sockets.
- **`dropMessage(float p)`**: Simulates packet loss.
- **`k_socket(domain, type, protocol)`**: Creates a new KTP socket.
- **`k_bind(ksockfd, src_ip, src_port, dest_ip, dest_port)`**: Binds a KTP socket.
- **`k_sendto(ksockfd, message, dest_ip, dest_port)`**: Queues a message for sending.
- **`k_recvfrom(ksockfd)`**: Receives a message from a KTP socket.
- **`k_close(ksockfd)`**: Closes a KTP socket.
- **`print_socket(i)`**: Debug function to print socket details.

---

## Initialization & Management (`initksocket.c`)

- **`is_process_alive(pid)`**: Checks if a process is running.
- **`garbage_collector(SM)`**: Cleans up sockets of terminated processes.
- **`process_bind_requests()`**: Handles pending bind requests.
- **`process_bind_requests_loop(arg)`**: Thread for periodic bind processing.
- **`receiver(arg)`**: Thread for incoming packet handling.
- **`sender(arg)`**: Thread for outgoing packet handling and reliability.
- **`main()`**: Initializes shared memory, sockets, threads, and starts management routines.

---

## Usage

Refer to the source files for implementation details and usage examples. The protocol is designed for reliable communication over UDP using shared memory for socket management.

---
