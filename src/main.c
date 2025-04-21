#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

// #include "kiln_string.h"

#include "../include/cluster.h"
#include "../include/globals.h"
#include "../include/kv_store.h"


// ========================= Raft Global Variables
uint64_t currentTerm = 0;
uint64_t votedFor = 0;

uint64_t commitIndex = 0;
uint64_t lastApplied = 0;

uint64_t nextIndex = 0;
uint64_t matchIndex = 0;
// =========================


// void init_cluster(Cluster* cluster) {
	
// 	FILE* config_fp = fopen("cluster.txt", "r");

// 	if (config_fp == NULL) {
// 		perror("cluster.txt config file not found");
// 		exit(1);
// 	}

// 	uint64_t length = fsizeof(config_fp);

// 	uint8_t* buffer = (uint8_t*) malloc(length + 1);
// 	fread((char*) buffer, 1, length, config_fp);
// 	buffer[length] = '\0';
// 	fclose(config_fp);

// 	KilnString text = KilnString_from_cstr((char*) buffer);

// 	KilnString* addrs = NULL;
// 	uint64_t addrs_length = 0;
// 	KilnString_split(&text, "\n", addrs, &addrs_length);
// 	KilnString_free(&text);

// 	for(uint64_t i = 0; i < addrs_length; i++) {
// 		KilnString addr[2];

// 		uint64_t addr_lenth = 2;
// 		KilnString_split(&addrs[i], ":", &addr[0], &addr_lenth);

// 		char* ip = KilnString_as_cstr(&addr[0]);
// 		char* port = KilnString_as_cstr(&addr[1]);

// 		strncpy(cluster->addrs[cluster->length].ip, ip, ADDR_IP_LEN);
// 		strncpy(cluster->addrs[cluster->length].port, port, ADDR_PORT_LEN);

// 		cluster->fds[length] = -1;
// 		cluster->length++;

// 		KilnString_free(&addr[0]);
// 		KilnString_free(&addr[1]);
// 	}

// 	for(uint64_t i = 0; i < addrs_length; i++) {
// 		KilnString_free(&addrs[i]);
// 	}
// 	free(addrs);

// }

void init_logfile(char* logfile_name) {
	int32_t exists = access(logfile_name, F_OK);
	if (!exists) {
		int32_t fd = open(logfile_name, O_CREAT) ;
		close(fd);
	}

	logfile_fd_mut = open(logfile_name, O_RDWR | O_SYNC);

	for(int i = 0; i < LF_NUM_FDS; i++) {
		logfile_fds_ro[i] = open(logfile_name, O_RDONLY);
	}

	assert(logfile_fd_mut > 0);
}

int main(int argc, char** argv) {
	if (argc != 3) {
		perror("invalid number of arguments");
		exit(1);
	}

	uint64_t addr_index = (uint64_t) strtoll(argv[1], NULL, 10);



	printf("logfile: %s\n", argv[2]);
	printf("%lu\n", sizeof(kvs_page_t));

	assert(addr_index - addr_index == 0);
	return 0;
}