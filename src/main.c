#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdalign.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <glib.h>

#include "../include/globals.h"
#include "../include/kv_store.h"
#include "../include/network.h"
#include "../include/raft_core.h"
#include "../include/log_file.h"
#include "../include/spsc.h"
#include "../include/client_ops.h"

cluster_t cluster_state;
client_handeling_t clients;
raft_node_t raft_state;
logfile_t logfile;
kv_store_t disk_map;

spsc_queue_t _1_to_3;
spsc_queue_t _2_to_3;
spsc_queue_t _2_to_6;
spsc_queue_t _3_to_4;
spsc_queue_t _3_to_5;

void init_channels() {
    int32_t res;

    res = spsc_queue_init(&_1_to_3, sizeof(_1_to_3_t));
    assert(res == 0);

    res = spsc_queue_init(&_2_to_3, sizeof(_2_to_3_t));
    assert(res == 0);

    res = spsc_queue_init(&_2_to_6, sizeof(_2_to_6_t));
    assert(res == 0);

    res = spsc_queue_init(&_3_to_4, sizeof(cmd_buffer_t*));
    assert(res == 0);

    res = spsc_queue_init(&_3_to_5, sizeof(cmd_buffer_t*));
    assert(res == 0);

}

void parse_ip_addr(cluster_t* cluster, uint8_t self_id) {
    // --- Initialize cluster struct ---
    cluster->self_id = self_id; // Store self_id early
    cluster->num_nodes = 0;

    // Initialize sock_fds to -1 (indicating not yet created/connected)
    for (int i = 0; i < MAX_NODES; ++i) {
        cluster->node_fds[i] = -1;
    }

    // Zero out node data initially
    memset(cluster->nodes, 0, sizeof(cluster->nodes));


    // --- Open and read config file ---
    FILE* config_fp = fopen("cluster.txt", "r");
	assert(config_fp != NULL);

    uint64_t length = fsizeof(config_fp);
	assert(length > 0);

    // Allocate buffer (+1 for null terminator)
    // Use size_t for memory sizes and check malloc result
    size_t buffer_size = (size_t)length + 1;
    char* buffer = (char*) malloc(buffer_size);
	assert(buffer != NULL);

    // Read file content
    size_t bytes_read = fread(buffer, 1, length, config_fp);
	assert(bytes_read == length);

    buffer[length] = '\0'; 
    fclose(config_fp);


    char* line;
    char* saveptr;
    uint64_t node_index = 0;

    // Use strtok_r to split buffer into lines by newline
    line = strtok_r(buffer, "\n\r", &saveptr);

    while (line != NULL && node_index < MAX_NODES) {
        // Skip empty lines that might result from strtok_r
        if (strlen(line) == 0) {
            line = strtok_r(NULL, "\n\r", &saveptr);
            continue;
        }

        // Find the colon separating IP and port
        char* colon = strchr(line, ':');
        if (colon == NULL) {
            fprintf(stderr, "Warning: Invalid line format in cluster.txt (missing ':'): %s. Skipping.\n", line);
            line = strtok_r(NULL, "\n\r", &saveptr);
            continue;
        }

        // --- Extract IP Address ---
        int ip_len = colon - line;
        if (ip_len >= P_ADDR_LEN) {
            fprintf(stderr, "Warning: IP address too long in cluster.txt: %.*s. Skipping.\n", ip_len, line);
            line = strtok_r(NULL, "\n\r", &saveptr);
            continue;
        }
        if (ip_len == 0) {
             fprintf(stderr, "Warning: Missing IP address before colon in cluster.txt: %s. Skipping.\n", line);
             line = strtok_r(NULL, "\n\r", &saveptr);
             continue;
        }
        // Copy IP string
        strncpy(cluster->nodes[node_index].presentation_ip, line, ip_len);
        cluster->nodes[node_index].presentation_ip[ip_len] = '\0';


        // --- Extract Port ---
        char* port_str = colon + 1;
        if (*port_str == '\0') { // Check if port string is empty
            fprintf(stderr, "Warning: Missing port number after colon in cluster.txt: %s. Skipping.\n", line);
            line = strtok_r(NULL, "\n\r", &saveptr);
            continue;
        }

        char *endptr;
        unsigned long port_ul = strtoul(port_str, &endptr, 10);

        // Check for conversion errors
        if (*endptr != '\0' || port_ul == 0 || port_ul > 65535) {
            fprintf(stderr, "Warning: Invalid port number in cluster.txt '%s': %s. Skipping.\n", port_str, line);
            fprintf(stderr, " Reason: Invalid format or out of range (1-65535)\n");
            line = strtok_r(NULL, "\n\r", &saveptr);
            continue;
        }
        cluster->nodes[node_index].presentation_port = (uint16_t)port_ul;


        // --- Populate sockaddr_in structure ---
        struct sockaddr_in* addr = &cluster->nodes[node_index].addr;
        memset(addr, 0, sizeof(struct sockaddr_in));
        addr->sin_family = AF_INET;
        addr->sin_port = htons(cluster->nodes[node_index].presentation_port); // Convert port to network byte order

        // Convert presentation IP string to network binary format
        int pton_ret = inet_pton(AF_INET, cluster->nodes[node_index].presentation_ip, &addr->sin_addr);
		assert(pton_ret > 0);

        node_index++;
        line = strtok_r(NULL, "\n\r", &saveptr);
    }

	assert(node_index <= MAX_NODES);
    cluster->num_nodes = node_index;

	assert(cluster->self_id < cluster->num_nodes);

    printf("Initialized cluster with %u nodes. Self ID: %u\n", cluster->num_nodes, cluster->self_id);

	free(buffer);
}

// Creates a TCP connection with all the other nodes in the system
void init_cluster(cluster_t* cluster) {
    printf("Establishing connections with other nodes...\n");
    
    // We'll use a convention to avoid connection collisions:
    // Lower node ID connects to higher node ID
    // This prevents both sides from trying to connect simultaneously
    
    // Create a socket for this node to accept connections from lower-ID nodes
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Failed to create listening socket");
        exit(1);
    }
    
    // Set socket option to reuse address
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(listen_fd);
        exit(1);
    }
    
    // Get our own node's address info
    struct sockaddr_in self_addr = cluster->nodes[cluster->self_id].addr;
    
    // Bind socket to our address
    if (bind(listen_fd, (struct sockaddr*)&self_addr, sizeof(self_addr)) < 0) {
        perror("Failed to bind socket");
        close(listen_fd);
        exit(1);
    }
    
    // Listen for incoming connections
    if (listen(listen_fd, cluster->num_nodes) < 0) {
        perror("Failed to listen on socket");
        close(listen_fd);
        exit(1);
    }
    
    printf("Node %d listening on %s:%d\n", 
           cluster->self_id, 
           cluster->nodes[cluster->self_id].presentation_ip,
           cluster->nodes[cluster->self_id].presentation_port);
    
    // Determine how many connections we expect to accept (from lower ID nodes)
    // and how many we need to initiate (to higher ID nodes)
    int expected_incoming = 0;
    int outgoing_needed = 0;
    
    for (int i = 0; i < cluster->num_nodes; i++) {
        if (i == cluster->self_id) continue; // Skip ourselves
        
        if (i < cluster->self_id) {
            expected_incoming++;
        } else {
            outgoing_needed++;
        }
    }
    
    printf("Node %d expects %d incoming and will make %d outgoing connections\n",
           cluster->self_id, expected_incoming, outgoing_needed);
    
    // Then, accept connections from all nodes with lower IDs
    for (int i = 0; i < expected_incoming; i++) {
        struct sockaddr_in peer_addr;
        socklen_t peer_addr_len = sizeof(peer_addr);
        
        printf("Waiting for incoming connection %d of %d...\n", i+1, expected_incoming);
        
        int conn_fd = accept(listen_fd, (struct sockaddr*)&peer_addr, &peer_addr_len);
        if (conn_fd < 0) {
            perror("Failed to accept connection");
            exit(1);
        }
        
        // Receive peer's node ID
        uint8_t peer_id;
        if (read(conn_fd, &peer_id, sizeof(peer_id)) != sizeof(peer_id)) {
            perror("Failed to receive ID during handshake");
            close(conn_fd);
            exit(1);
        }
        
        // Verify peer ID is valid
        if (peer_id >= cluster->num_nodes || peer_id >= cluster->self_id) {
            fprintf(stderr, "Invalid peer ID received: %d\n", peer_id);
            close(conn_fd);
            continue;
        }
        
        // Send acknowledgment (our ID)
        uint8_t id_buf = cluster->self_id;
        if (write(conn_fd, &id_buf, sizeof(id_buf)) != sizeof(id_buf)) {
            perror("Failed to send ID during handshake");
            close(conn_fd);
            exit(1);
        }
        
        // Store the socket descriptor
        cluster->node_fds[peer_id] = conn_fd;
        printf("Accepted connection from node %d\n", peer_id);
    }

    // First, connect to all nodes with higher IDs
    for (int i = cluster->self_id + 1; i < cluster->num_nodes; i++) {
		while (true) {
			int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
			if (sock_fd < 0) {
				perror("Failed to create outgoing socket");
				exit(1);
			}
			
			// Connect to the peer
			if (connect(sock_fd, (struct sockaddr*)&cluster->nodes[i].addr, sizeof(struct sockaddr_in)) < 0) {
				close(sock_fd);
				continue;
			}
			
			// Send our node ID as a handshake
			uint8_t id_buf = cluster->self_id;
			if (write(sock_fd, &id_buf, sizeof(id_buf)) != sizeof(id_buf)) {
				perror("Failed to send ID during handshake");
				close(sock_fd);
				exit(1);
			}
			
			// Receive acknowledgment (peer's ID)
			uint8_t peer_id;
			if (read(sock_fd, &peer_id, sizeof(peer_id)) != sizeof(peer_id)) {
				perror("Failed to receive ID during handshake");
				close(sock_fd);
				exit(1);
			}
			
			// Verify peer ID
			if (peer_id != i) {
				fprintf(stderr, "Invalid peer ID received: expected %d, got %d\n", i, peer_id);
				close(sock_fd);
				exit(1);
			}
			
			// Store the socket descriptor
			cluster->node_fds[i] = sock_fd;
			printf("Connected to node %d\n", i);
			break;
		}
    }
    
    // Close the listening socket, we don't need it anymore
    close(listen_fd);
    
    // Verify all connections are established
    for (int i = 0; i < cluster->num_nodes; i++) {
        if (i == cluster->self_id) continue; // Skip ourselves
        
        if (cluster->node_fds[i] < 0) {
            fprintf(stderr, "Warning: No connection established with node %d\n", i);
        }
    }
    
    printf("Cluster initialization complete. Connected to %d nodes.\n", cluster->num_nodes - 1);
}

void set_cluster_comms_nonblocking(cluster_t* cluster) {
    for(int i = 0; i < cluster->num_nodes; i++) {
        if (i == cluster->self_id) continue;
        int32_t flags = fcntl(cluster->node_fds[i], F_GETFL, 0);
        assert(flags != -1);

        int32_t res = fcntl(cluster->node_fds[i], F_SETFL, flags | O_NONBLOCK);
        assert(res != -1);
    }
}

void init_logfile(char* logfile_name) {
	int32_t permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

	logfile.file_fd = open(logfile_name, O_RDWR | O_SYNC | O_CREAT, permissions);
	if (logfile.file_fd < 0) {
		perror("Failed to open file");
		exit(1);
	}
}

void* thread_1(void* _) {
    assert(NUM_CLIENT_LISTENERS == 1);

    size_t max_kvs_cmd_size = (1 << 11);

    while (true) {
        pthread_mutex_lock(&raft_state.mutex);
        raft_state_t rf_srate = raft_state.state;
        pthread_mutex_unlock(&raft_state.mutex);

        if (rf_srate == RAFT_STATE_LEADER) {
            cmd_buffer_t* cmd_buffer_w = (cmd_buffer_t*) malloc(CMDBUF_SIZE);
            cmd_buffer_t* cmd_buffer_r = (cmd_buffer_t*) malloc(CMDBUF_SIZE);

            #ifndef NDEBUG
                // These are not necessary and are just here to suppress valgrind errors
                memset((void*) cmd_buffer_w, 0, CMDBUF_SIZE);
                memset((void*) cmd_buffer_r, 0, CMDBUF_SIZE);
            #endif

            pthread_mutex_lock(&raft_state.mutex);
            cmd_buffer_w->term = raft_state.current_term;
            cmd_buffer_w->log_index = raft_state.last_applied_index + 1;
            pthread_mutex_unlock(&raft_state.mutex);

            cmd_buffer_w->data_to_free = (uint8_t*) cmd_buffer_w;
            cmd_buffer_w->cmds_length = 0;


            *cmd_buffer_r = *cmd_buffer_w;
            cmd_buffer_r->data_to_free = (uint8_t*) cmd_buffer_r;

            double time_elapsed = 0.;

            alignas(8) uint8_t i_buffer[1 << 12];
            kvs_command_t* cmd_ibuf = (kvs_command_t*) i_buffer;

            while (time_elapsed < 0.1) {
                time_t start_time = clock();

                pthread_mutex_lock(&raft_state.mutex);
                uint64_t current_term = raft_state.current_term;
                pthread_mutex_unlock(&raft_state.mutex);
                if (current_term != cmd_buffer_w->term) {
                    break;
                }

                ssize_t bytes_read = recv(clients.client_socket_recv, &i_buffer, max_kvs_cmd_size, 0);

                if (bytes_read <= 0) {
                    time_t end_time = clock();
                    time_elapsed += ((double) end_time - (double) start_time) / (double) CLOCKS_PER_SEC;
                    continue;
                }

                assert(bytes_read >= sizeof(kvs_command_t));
                assert(jump_to_alignment(bytes_read, 8) == kvs_command_len((kvs_command_t*) i_buffer));

                cmd_buffer_t* target_cmdb = (cmd_ibuf->type == CMD_GET) ? cmd_buffer_r : cmd_buffer_w;

                assert(target_cmdb->cmds_length < CMDBUF_SIZE - 256);
                memcpy(&((uint8_t*) target_cmdb->data)[target_cmdb->cmds_length], i_buffer, bytes_read);
                target_cmdb->num_cmds = target_cmdb->num_cmds + 1;
                target_cmdb->cmds_length += jump_to_alignment(bytes_read, 8);

                if (target_cmdb->cmds_length >= (CMDBUF_SIZE - max_kvs_cmd_size - sizeof(cmd_buffer_t) - 64)) {
                    break;
                }

                time_t end_time = clock();
                time_elapsed += ((double) end_time - (double) start_time) / (double) CLOCKS_PER_SEC;
            }

            // If we're at a new term than when we started the batch, abandon it as it won't get accepted anyways
            pthread_mutex_lock(&raft_state.mutex);
            uint64_t current_term = raft_state.current_term;
            pthread_mutex_unlock(&raft_state.mutex);

            if (current_term != cmd_buffer_w->term) {
                printf("[DEBUG - thread_1] Term updated. Aborting command buffer\n");
                free(cmd_buffer_w->data_to_free);
                free(cmd_buffer_r->data_to_free);
                continue;
            }

            if (cmd_buffer_w->cmds_length == 0 && cmd_buffer_r->cmds_length == 0) {
                // If there are no commands in this batch, don't bother sending these to thread_3
                free(cmd_buffer_w->data_to_free);
                free(cmd_buffer_r->data_to_free);
                continue;
            }

            // Send the buffers to thread 3
            _1_to_3_t tr_msg = {
                .write_cmds = cmd_buffer_w,
                .read_cmds = cmd_buffer_r,
            };
            assert(tr_msg.write_cmds != NULL);
            assert(tr_msg.read_cmds != NULL);
            spsc_queue_enqueue(&_1_to_3, &tr_msg);
        }
        else {
            sleep_milisec(10);
        }
    }
}

void* thread_2(void* _) {
    size_t buf_size = (CMDBUF_SIZE + sizeof(raft_msg_t));
    raft_msg_t* msg_buffer = (raft_msg_t*) malloc(buf_size);

    while (true) {
        bool received_msg = false;
        for (uint64_t i = 0; i < cluster_state.num_nodes; i++) {
            if (i == cluster_state.self_id) continue;

            assert(cluster_state.node_fds[i] > 0);

            ssize_t bytes_read = recv(cluster_state.node_fds[i], (void*) msg_buffer, sizeof(raft_msg_t), 0);
            if (bytes_read <= 0) {
                continue;
            }

            if (msg_buffer->body_length > 0) {
                bytes_read = 0;
                
                while (bytes_read < msg_buffer->body_length) {
                    int64_t res = recv(cluster_state.node_fds[i], (void*) &msg_buffer->data[bytes_read], msg_buffer->body_length, 0);
                    assert(res > 0);
                    bytes_read += res;
                }
                assert(bytes_read == msg_buffer->body_length);
            }
            received_msg = true;

            break;
        }

        if (!received_msg) {
            continue;
        }

        assert(msg_buffer->sender_node_id < MAX_NODES);

        // Handle term
        pthread_mutex_lock(&raft_state.mutex);
        if (msg_buffer->term > raft_state.current_term) {
            printf("[DEBUG - thread_2] received a message from node[id=%lu] with greater term (updating self term)\n", msg_buffer->sender_node_id);
            raft_state.current_term = msg_buffer->term;
            raft_state.state = RAFT_STATE_FOLLOWER;
        }
        else if (msg_buffer->term < raft_state.current_term) {
            printf("[DEBUG - thread_2] received a message from node[id=%lu] with outdated term\n", msg_buffer->sender_node_id);
            pthread_mutex_unlock(&raft_state.mutex);
            continue;
        }
        pthread_mutex_unlock(&raft_state.mutex);

        // Handle append entries
        if (msg_buffer->rpc_type == RAFT_RPC_APPEND_ENTRIES) {
            printf("[DEBUG - thread_2] Received append entries request from node [id=%lu]\n", msg_buffer->sender_node_id);

            raft_append_entries_req_t* ae_req = (raft_append_entries_req_t*) msg_buffer->data;

            pthread_mutex_lock(&raft_state.mutex);

            size_t ae_resp_tsize = sizeof(raft_msg_t) + sizeof(raft_append_entries_resp_t);
            bool ae_successful = false;

            alignas(8) uint8_t buffer[ae_resp_tsize];
            raft_msg_t* ae_resp = (raft_msg_t*) buffer;
            *ae_resp = (raft_msg_t) {
                .rpc_type = RAFT_RPC_RESP_APPEND_ENTRIES,
                .sender_node_id = cluster_state.self_id,
                .term = raft_state.current_term,
                .body_length = sizeof(raft_append_entries_resp_t),
            };

            #ifndef NDEBUG
                // Not necessary for functionality, only exists to suppress valgrind warnings
                uint8_t n1[4] = {183};
                memcpy(ae_resp->padding, &n1, 4);
            #endif

            bool last_logs_match = (
                ae_req->prev_log_term == raft_state.last_applied_term && 
                ae_req->prev_log_index == raft_state.last_applied_index
            );
            bool is_first_log = (raft_state.last_applied_term == 0) && (raft_state.last_applied_index == 0);

            raft_append_entries_resp_t* aer_data = ((raft_append_entries_resp_t*) ae_resp->data) ;
            if (last_logs_match || is_first_log) {
                *aer_data = (raft_append_entries_resp_t) {
                    .success = true,
                    .term = ae_req->prev_log_term,
                    .log_idx = ae_req->prev_log_index + 1,
                };
                ae_successful = true;
            }
            else {
                // Our node is behind, so we cannot commit this. 
                printf("[DEBUG - thread_2] received invalid appendEntries() RPC (meaning that this follower is behind)\n");

                *aer_data = (raft_append_entries_resp_t) {
                    .success = false,
                    .log_idx = raft_state.last_applied_index,
                    .term = raft_state.last_applied_term,
                };
            }

            #ifndef NDEBUG
                // Not necessary for functionality, only exists to suppress valgrind warnings
                uint8_t n2[7] = {183};
                memcpy(aer_data->padding, &n2, 7);
            #endif

            pthread_mutex_unlock(&raft_state.mutex);

            ssize_t bytes_sent = send(
                cluster_state.node_fds[msg_buffer->sender_node_id],
                buffer,
                ae_resp_tsize,
                0
            );

            if (bytes_sent < 0) {
                perror("failed to sent appendEntries() response");
                exit(1);
            }
            assert(bytes_sent >= 0);


            printf("[DEBUG - thread_2] Responded to appendEntries (send_to=node[%lu] success=%hu)\n", msg_buffer->sender_node_id, ae_successful);

            if (!ae_successful) {
                continue;
            }

            cmd_buffer_t* cmd_buffer = ae_req->entries;
            cmd_buffer->data_to_free = (uint8_t*) msg_buffer;

            // Create a new message buffer (since thread 5 will free the old one)
            msg_buffer = (raft_msg_t*) malloc(buf_size);

            logfile_append_data(&logfile, cmd_buffer);

            // Not thread 3, but we need 5 to process the set/delete commands
            spsc_queue_enqueue(&_3_to_5, &cmd_buffer);

            continue;
        }

        // Handle vote request
        if (msg_buffer->rpc_type == RAFT_RPC_REQUEST_VOTE) {
            raft_request_vote_resp_t vote_resp = {
                .vote_granted = true,
            };
            
            raft_request_vote_req_t* vote_req = (raft_request_vote_req_t*) msg_buffer->data;
                            
            pthread_mutex_lock(&raft_state.mutex);

            if (vote_req->last_log_term < raft_state.last_applied_term) {
                vote_resp.vote_granted = false;
            }
            else if (vote_req->last_log_term == raft_state.last_applied_term) {
                if (vote_req->last_log_index < raft_state.last_applied_index) {
                    vote_resp.vote_granted = false;
                }
            }
            
            size_t size_of_full_resp = sizeof(raft_msg_t) + sizeof(raft_request_vote_resp_t);
            uint8_t vr_msg_b[size_of_full_resp];
            raft_msg_t* vr_msg = (raft_msg_t*) vr_msg_b;

            *vr_msg = (raft_msg_t) {
                .term = raft_state.current_term,
                .body_length = sizeof(raft_append_entries_resp_t),
                .rpc_type = RAFT_RPC_RESP_REQUEST_VOTE,
                .sender_node_id = cluster_state.self_id,
            };

            *((raft_request_vote_resp_t*) vr_msg->data) = vote_resp;

            pthread_mutex_unlock(&raft_state.mutex);
            send(cluster_state.node_fds[msg_buffer->sender_node_id], vr_msg, size_of_full_resp, 0);
            continue;
        }

        // Handle append entries resp
        if (msg_buffer->rpc_type == RAFT_RPC_RESP_APPEND_ENTRIES) {
            raft_append_entries_resp_t* ae_resp = (raft_append_entries_resp_t*) msg_buffer->data;

            _2_to_3_t block = {
                .node_id = msg_buffer->sender_node_id,
                .term = ae_resp->term,
                .log_index = ae_resp->log_idx,
            };

            if (ae_resp->success) {
                printf(
                    "[DEBUG - thread_2] received an OK response from an appendEntries() RPC from node[id=%lu]\n", 
                    msg_buffer->sender_node_id
                );
                spsc_queue_enqueue(&_2_to_3, &block);
            }
            else {
                printf(
                    "[DEBUG - thread_2] received an ERR response from an appendEntries() RPC from node[id=%lu]\n", 
                    msg_buffer->sender_node_id
                );
                spsc_queue_enqueue(&_2_to_6, &block);
            }
        }

        // Handle vote resp
        if (msg_buffer->rpc_type == RAFT_RPC_RESP_REQUEST_VOTE) {
            raft_request_vote_resp_t* v_resp = (raft_request_vote_resp_t*) msg_buffer->data;
            
            pthread_mutex_lock(&raft_state.mutex);

            assert(raft_state.state == RAFT_STATE_CANDIDATE);
            
            if (raft_state.current_term == msg_buffer->term) {
                if (v_resp->vote_granted) {
                    raft_state.votes_received++;
                }
            } else {
                printf("unrechable!()\n");
            }

            if (raft_state.votes_received > cluster_state.num_nodes / 2) {
                pthread_mutex_unlock(&raft_state.mutex);
                raft_become_leader(&raft_state);
            }
            else {
                // We need to repeat pthread_mutex_unlock() to ensure that raft_state.votes_received is compared 
                // while we hold the lock
                pthread_mutex_unlock(&raft_state.mutex);
            }
            continue;
        }
        
    }
}

void* thread_3(void* _) {
    while (true) {
        _1_to_3_t cmd_buffers;
        int32_t res = spsc_queue_dequeue(&_1_to_3, &cmd_buffers);

        if (res != 0) {
            sleep_milisec(10);
            continue;
        }

        printf(
            "[DEBUG - thread_3] received command_buffer from thread 1 (length of cmds: (W %lu) (R %lu)) (RW num_cmds %lu)\n", 
            cmd_buffers.write_cmds->cmds_length, 
            cmd_buffers.read_cmds->cmds_length,
            cmd_buffers.read_cmds->num_cmds + cmd_buffers.write_cmds->num_cmds
        );

        assert(cmd_buffers.read_cmds->term == cmd_buffers.write_cmds->term);

        size_t cmd_buf_length = sizeof(cmd_buffer_t) + cmd_buffers.write_cmds->cmds_length;
        size_t body_length = cmd_buf_length + sizeof(raft_append_entries_req_t);
        size_t msg_length = body_length + sizeof(raft_msg_t);

        printf(
            "[DEBUG - thread_3] cmd_buf_length=%lu body_length=%lu msg_length=%lu\n",
            cmd_buf_length,
            body_length,
            msg_length
        );

        assert(body_length <= CMDBUF_SIZE);

        raft_msg_t* msg = (raft_msg_t*) malloc(msg_length);
        *msg = (raft_msg_t) {
            .term = cmd_buffers.write_cmds->term,
            .rpc_type = RAFT_RPC_APPEND_ENTRIES,
            .sender_node_id = cluster_state.self_id,
            .body_length = body_length,
        };

        raft_append_entries_req_t* ae_req = (raft_append_entries_req_t*) msg->data;

        pthread_mutex_lock(&raft_state.mutex);
        *ae_req = (raft_append_entries_req_t) {
            .prev_log_term = raft_state.last_applied_term,
            .prev_log_index = raft_state.last_applied_index,
            .leader_commit = raft_state.last_applied_index + 1,
        };
        pthread_mutex_unlock(&raft_state.mutex);

        memcpy(ae_req->entries, cmd_buffers.write_cmds, cmd_buf_length);

        for(int i = 0; i < cluster_state.num_nodes; i++) {
            if (i == cluster_state.self_id) continue;

            ssize_t res = send(cluster_state.node_fds[i], (void*) msg, msg_length, 0);
            assert(res > 0);
        }

        printf(
            "[DEBUG - thread_3] Sent out all appendEntry() RPCs (term=%lu, log_idx=%lu) \n",
            cmd_buffers.write_cmds->term, 
            cmd_buffers.write_cmds->log_index
        );

        uint64_t num_nodes_ok = 1;

        _2_to_3_t ae_resp_msg = {
            .term = 0,
            .log_index = 0,
            .node_id = UINT64_MAX,
        };

        while (num_nodes_ok <= cluster_state.num_nodes / 2) {
            res = spsc_queue_dequeue(&_2_to_3, &ae_resp_msg);
            if (res != 0) continue;

            assert(ae_resp_msg.node_id != UINT64_MAX);

            if (ae_resp_msg.term == cmd_buffers.write_cmds->term && ae_resp_msg.log_index == cmd_buffers.write_cmds->log_index) {
                printf("[DEBUG - thread_3] received OK from node[id=%lu]\n", ae_resp_msg.node_id);
                num_nodes_ok++;
            } else {
                printf(
                    "[DEBUG - thread_3] received irrelevent OK from node[id=%lu] (term=%lu, idx=%lu)\n", 
                    ae_resp_msg.node_id,
                    ae_resp_msg.term,
                    ae_resp_msg.log_index
                );
                assert(ae_resp_msg.term <= cmd_buffers.read_cmds->term);
            }
            sleep_milisec(5);
        }

        printf(
            "[DEBUG - thread_3] Committed (term=%lu, log_idx=%lu)\n", 
            cmd_buffers.write_cmds->term, 
            cmd_buffers.write_cmds->log_index
        );

        logfile_append_data(&logfile, cmd_buffers.write_cmds);

        assert(cmd_buffers.read_cmds != NULL);
        assert(cmd_buffers.write_cmds != NULL);
        spsc_queue_enqueue(&_3_to_4, &cmd_buffers.read_cmds);
        spsc_queue_enqueue(&_3_to_5, &cmd_buffers.write_cmds);
    }
}

void* thread_4(void* _) {
    // Handles commiting GET operations to the kv_store

    while (true) {
        cmd_buffer_t* cmd_buffer = NULL;
        int32_t res = spsc_queue_dequeue(&_3_to_4, &cmd_buffer);
        if (res != 0) {
            sleep_milisec(5);
            continue;
        }

        kvs_command_t* cmd = cmd_buffer->data;

        size_t total_length_processed = 0;

        while (total_length_processed < cmd_buffer->cmds_length) {
            size_t length = kvs_command_len(cmd);
            alignas(8) uint8_t value[1 << 12];

            assert(total_length_processed % 8 == 0);
            assert(cmd->value_length + sizeof(client_resp_t) <= (1 << 12));

            client_resp_t* cr = (client_resp_t*) value;
            

            char* key = kvs_command_get_key(cmd);
            
            int64_t value_length = INT64_MAX;
            uint8_t* data = kv_store_get(&disk_map, key, &value_length);
            assert(value_length < MAX_VAL_LEN);


            if (data == NULL) {
                *cr = (client_resp_t) {
                    .body_length = 0,
                    .status_code = 201,
                    .op_type = CMD_GET,
                };
                value_length = 0;
            }
            else {
                char string[128] = {0};
                memcpy(string, data, value_length);

                assert(value_length < MAX_VAL_LEN || value_length > 0);
                *cr = (client_resp_t) {
                    .body_length = value_length,
                    .status_code = 200,
                    .op_type = CMD_GET,
                };
                memcpy(cr->data, data, value_length);
            }

            free(data);
            
            // Send Message
            resp_to_client(clients.clinet_socket_send, &cmd->return_addr, cr);

            char ip[128];
            inet_ntop(AF_INET, &cmd->return_addr.sin_addr, ip, 128);

            cmd = (kvs_command_t*) &((uint8_t*) cmd)[length];
            total_length_processed += length;
        }

        free(cmd_buffer->data_to_free);
    }
    
}

void* thread_5(void* _) {
    // Handles commiting SET & DELETE operations to the kv_store

    while (true) {
        cmd_buffer_t* cmd_buffer = NULL;

        int32_t res = spsc_queue_dequeue(&_3_to_5, &cmd_buffer);
        if (res != 0) {
            sleep_milisec(5);
            continue;
        }

        assert(cmd_buffer != NULL);
        assert(cmd_buffer->cmds_length <= CMDBUF_SIZE);

        // Size of the log file before this batch
        kvs_command_t* cmd = cmd_buffer->data;

        size_t total_length_processed = 0;

        printf("[DEBUG - thread_5] cmd_buffer->cmds_length = %lu\n", cmd_buffer->cmds_length);

        while (total_length_processed < cmd_buffer->cmds_length) {
            assert(cmd->value_length < MAX_VAL_LEN);
            assert(cmd->value_length < CMDBUF_SIZE);

            assert((cmd_buffer->cmds_length - total_length_processed) >= sizeof(kvs_command_t));

            size_t length = kvs_command_len(cmd);
            char* key = kvs_command_get_key(cmd);

            if (cmd->type == CMD_DELETE) {
                kv_store_delete(&disk_map, key);
            }
            else if (cmd->type == CMD_SET) {
                uint8_t* value = NULL;
                kvs_command_get_value(cmd, &value);

                assert(value != NULL);

                char string[128] = {0};
                memcpy(string, value, cmd->value_length);
                kv_store_set(&disk_map, key, value, cmd->value_length);
            }
            else {
                printf("FOUND CMD TYPE [%d] in thread_5\n", cmd->type);
                exit(1);
            }

            pthread_mutex_lock(&raft_state.mutex);
            raft_state_t state = raft_state.state;
            pthread_mutex_unlock(&raft_state.mutex);

            // Returns success to the client. (only do so if leader)
            if (state == RAFT_STATE_LEADER) {
                alignas(8) uint8_t buffer[128];
                client_resp_t* cresp = (client_resp_t*) buffer;
                *cresp = (client_resp_t) {
                    .body_length=0,
                    .status_code=200,
                    .op_type=cmd->type,
                };
    
                char ip[128];
                inet_ntop(AF_INET, &cmd->return_addr.sin_addr, ip, 128);
    
                resp_to_client(clients.clinet_socket_send, &cmd->return_addr, cresp);
            }

            cmd = (kvs_command_t*) &((uint8_t*) cmd)[length];
            total_length_processed += length;

        }

        free(cmd_buffer->data_to_free);
    }
}

void* thread_6(void* _) {
    while (true) {
        // TODO: implement this
        sleep_milisec(10000);
    }
    
}

void* thread_7(void* _) {

    while (true) {
        sleep_milisec(75);

        // pthread_mutex_lock(&raft_state.mutex);
        // raft_node_t rf_srate = raft_state;
        // pthread_mutex_unlock(&raft_state.mutex);

        // if (rf_srate.state == RAFT_STATE_LEADER) {
        //     raft_msg_t msg = {
        //         .sender_node_id = cluster_state.self_id,
        //         .body_length = 0,
        //         .rpc_type = RAFT_HEARTBEAT,
        //         .term = rf_srate.current_term,
        //     };

        //     for(int i = 0; i < cluster_state.num_nodes; i++) {
        //         if (i == cluster_state.self_id) continue;
        //         send(cluster_state.node_fds[i], &msg, sizeof(msg), 0);
        //     }
        // }
    }
}


int main(int argc, char** argv) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

	if (argc != 3) {
		perror("invalid number of arguments");
		exit(1);
	}

    #ifndef NDEBUG
        printf("DEBUG MODE\n");
    #else
        printf("RELEASE MODE\n");
    #endif

	uint64_t slef_id = (uint64_t) strtoll(argv[1], NULL, 10);
	assert(slef_id < MAX_NODES);

	parse_ip_addr(&cluster_state, (uint8_t) slef_id);

	init_logfile(argv[2]);
    init_channels();
    

    char map_name[100] = {0};
    snprintf(map_name, 99, "kv-store-%lu.bin", slef_id);
    disk_map = kv_store_init(map_name, &logfile);

    init_client_listeners(&clients, slef_id);
	init_cluster(&cluster_state);
    set_cluster_comms_nonblocking(&cluster_state);

    raft_state = raft_init(cluster_state.self_id, &logfile);

    typedef void*(*thread_routine)(void*);
    
    pthread_t threads[7];
    thread_routine thread_procedures[7] = {
        thread_1,
        thread_2,
        thread_3,
        thread_4,
        thread_5,
        thread_6,
        thread_7,
    };

    for(int i = 0; i < 7; i++) {
        pthread_create(&threads[i], NULL, thread_procedures[i], NULL);
    }

    for(int i = 0; i < 7; i++) {
        pthread_join(threads[i], NULL);
    }


	return 0;
}