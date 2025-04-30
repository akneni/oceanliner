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
            
            pthread_mutex_lock(&raft_state.mutex);
            cmd_buffer_w->term = raft_state.current_term;
            cmd_buffer_w->log_index = raft_state.last_applied_index + 1;
            pthread_mutex_unlock(&raft_state.mutex);

            cmd_buffer_w->data_to_free = (uint8_t*) cmd_buffer_w;

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
                    continue;
                }
                assert(bytes_read >= sizeof(kvs_command_t));

                cmd_buffer_t* target_cmdb = (cmd_ibuf->type == CMD_GET) ? cmd_buffer_r : cmd_buffer_w;
                memcpy(&target_cmdb->data[target_cmdb->cmds_length], i_buffer, bytes_read);
                target_cmdb->cmds_length += bytes_read;
                target_cmdb->cmds_length = (size_t) jump_to_alignment((uint8_t*) target_cmdb->cmds_length, 8);

                if (target_cmdb->cmds_length >= (CMDBUF_SIZE - max_kvs_cmd_size - sizeof(cmd_buffer_t))) {
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
                free(cmd_buffer_w);
                continue;
            }

            // Send the buffers to thread 3
            _1_to_3_t tr_msg = {
                .write_cmds = cmd_buffer_w,
                .read_cmds = cmd_buffer_r,
            };
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
        for(int i = 0; i < cluster_state.num_nodes; i++) {
            if (i == cluster_state.self_id) continue;

            ssize_t bytes_read = recv(cluster_state.node_fds[i], (void*) msg_buffer, sizeof(raft_msg_t), O_NONBLOCK);
            if (bytes_read <= 0) continue;

            if (msg_buffer->body_length > 0) {
                bytes_read = recv(cluster_state.node_fds[i], (void*) msg_buffer->data, msg_buffer->body_length, 0);
                assert(bytes_read >= 0);
            }

            // Hnadle term
            pthread_mutex_lock(&raft_state.mutex);
            if (msg_buffer->term > raft_state.current_term) {
                raft_state.current_term = msg_buffer->term;
            }
            else if (msg_buffer->term < raft_state.current_term) {
                pthread_mutex_unlock(&raft_state.mutex);
                continue;
            }
            pthread_mutex_unlock(&raft_state.mutex);

            // Handle append entries
            if (msg_buffer->rpc_type == RAFT_RPC_APPEND_ENTRIES) {
                raft_append_entries_req_t* ae_req = (raft_append_entries_req_t*) msg_buffer->data;

                pthread_mutex_lock(&raft_state.mutex);
                *ae_req = (raft_append_entries_req_t) {
                    .prev_log_index = raft_state.last_applied_index,
                    .prev_log_term = raft_state.last_applied_term,
                    .leader_commit = 0,
                };
                pthread_mutex_unlock(&raft_state.mutex);

                cmd_buffer_t* cmd_buffer = ae_req->entries;
                cmd_buffer->data_to_free = (uint8_t*) msg_buffer;

                logfile_append_data(&logfile, cmd_buffer);

                // Not thread 3, but we need 5 to process the set/delete commands
                spsc_queue_enqueue(&_3_to_5, &cmd_buffer);

                // Create a new message buffer
                msg_buffer = (raft_msg_t*) malloc(buf_size);
                
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
                send(cluster_state.node_fds[i], vr_msg, size_of_full_resp, 0);
                continue;
            }

            // Handle append entries resp
            if (msg_buffer->rpc_type == RAFT_RPC_RESP_APPEND_ENTRIES) {
                raft_append_entries_resp_t* ae_resp = (raft_append_entries_resp_t*) msg_buffer->data;

                _2_to_3_t block = {
                    .node_id = msg_buffer->sender_node_id,
                    .lc_term = ae_resp->lc_term,
                    .lc_log_index = ae_resp->lc_log_idx,
                };

                if (ae_resp->success) {
                    spsc_queue_enqueue(&_2_to_3, &block);
                }
                else {
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
}

void* thread_3(void* _) {
    while (true) {
        _1_to_3_t cmd_buffers;
        int32_t res = spsc_queue_dequeue(&_1_to_3, &cmd_buffers);

        if (res != 0) {
            sleep_milisec(10);
            continue;
        }

        assert(cmd_buffers.read_cmds->term == cmd_buffers.write_cmds->term);

        size_t cmd_buf_length = sizeof(cmd_buffer_t) + cmd_buffers.write_cmds->cmds_length;
        size_t body_length = cmd_buf_length + sizeof(raft_append_entries_req_t);
        size_t msg_length = body_length + sizeof(raft_msg_t);

        assert(body_length <= CMDBUF_SIZE);

        raft_msg_t* msg = (raft_msg_t*) malloc(sizeof(raft_msg_t) + body_length);
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

        memcpy(ae_req->entries, cmd_buffers.write_cmds, body_length);

        for(int i = 0; i < cluster_state.num_nodes; i++) {
            if (i == cluster_state.self_id) continue;

            send(cluster_state.node_fds[i], msg, msg_length, 0);
        }

        uint64_t num_nodes_ok = 1;
        _2_to_3_t tr_msg = {
            .lc_log_index = 0,
            .lc_term = 0,
            .node_id = 0,
        };

        while (num_nodes_ok <= cluster_state.num_nodes / 2) {
            res = spsc_queue_dequeue(&_2_to_3, &msg);
            if (res != 0) continue;

            if (tr_msg.lc_term == cmd_buffers.write_cmds->term && tr_msg.lc_log_index == cmd_buffers.write_cmds->log_index) {
                num_nodes_ok++;
            } else {
                assert(tr_msg.lc_term < cmd_buffers.read_cmds->term);
            }
            sleep_milisec(5);
        }

        logfile_append_data(&logfile, cmd_buffers.write_cmds);

        assert(cmd_buffers.read_cmds != NULL);
        assert(cmd_buffers.write_cmds != NULL);
        spsc_queue_enqueue(&_3_to_4, &cmd_buffers.read_cmds);
        spsc_queue_enqueue(&_3_to_5, &cmd_buffers.write_cmds);
    }
}

void* thread_4(void* _) {
    while (true) {
        cmd_buffer_t* cmd_buffer;
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

            assert(cmd->value_length + sizeof(client_resp_t) <= (1 << 12));

            client_resp_t* cr = (client_resp_t*) value;
            
            *cr = (client_resp_t) {
                .body_length = cmd->value_length,
                .status_code = 200,
            };

            uint8_t* value_ptr;
            kvs_command_get_value(cmd, &value_ptr);
            memcpy(cr->data, value_ptr, cr->body_length);
            
            // Send Message
            resp_to_client(clients.clinet_socket_send, &cmd->return_addr, cr);

            cmd = (kvs_command_t*) &((uint8_t*) cmd)[length];
            total_length_processed += length;
        }

        free(cmd_buffer->data_to_free);
    }
    
}

void* thread_5(void* _) {
    while (true) {
        cmd_buffer_t* cmd_buffer = NULL;
        int32_t res = spsc_queue_dequeue(&_3_to_5, &cmd_buffer);
        if (res != 0) {
            sleep_milisec(5);
            continue;
        }
        assert(cmd_buffer != NULL);

        // Size of the log file before this batch
        size_t log_size = logfile_sizeof(&logfile) - 2 * sizeof(kvsb_header_t) - cmd_buffer->cmds_length;

        kvs_command_t* cmd = cmd_buffer->data;

        size_t total_length_processed = 0;

        while (total_length_processed < cmd_buffer->cmds_length) {
            size_t length = kvs_command_len(cmd);

            char* key = kvs_command_get_key(cmd);

            if (cmd->type == CMD_DELETE) {
                kv_store_delete(&disk_map, key);
            }
            else if (cmd->type == CMD_SET) {
                uint8_t* value;
                kvs_command_get_value(cmd, &value);

                uint64_t cmd_offset = log_size + sizeof(kvsb_header_t) + total_length_processed;
                kv_store_set(&disk_map, cmd_offset, key, value, cmd->value_length);
            }
            else {
                printf("FOUND CMD TYPE [%d] in thread 5\n", cmd->type);
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

        pthread_mutex_lock(&raft_state.mutex);
        raft_state_t rf_srate = raft_state.state;
        pthread_mutex_unlock(&raft_state.mutex);

        if (rf_srate == RAFT_STATE_LEADER) {
            raft_msg_t msg;
            msg.sender_node_id = cluster_state.self_id;
            msg.body_length = 0;
            msg.rpc_type = RAFT_HEARTBEAT;
            msg.term = raft_state.current_term;
            
            for(int i = 0; i < cluster_state.num_nodes; i++) {
                if (i == cluster_state.self_id) continue;
                send(cluster_state.node_fds[i], &msg, sizeof(msg), 0);        
            }
        }
    }
}


int main(int argc, char** argv) {
	assert(sizeof(kvs_page_t) == 4096);

	if (argc != 3) {
		perror("invalid number of arguments");
		exit(1);
	}

	uint64_t slef_id = (uint64_t) strtoll(argv[1], NULL, 10);
	assert(slef_id < MAX_NODES);

	parse_ip_addr(&cluster_state, (uint8_t) slef_id);


	init_logfile(argv[2]);
	

    char map_name[100] = {0};
    snprintf(map_name, 99, "kv-store-%lu.bin", slef_id);
    disk_map = kv_store_init(map_name, &logfile);

    init_client_listeners(&clients, slef_id);
	init_cluster(&cluster_state);

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