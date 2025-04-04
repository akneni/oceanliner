#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <memory.h>
#include <string.h>
#include <fcntl.h>

#include "sso_string.h"

#include "../include/cluster.h"
#include "../include/utils.h"


// ========================= Raft Global Variables
uint64_t currentTerm = 0;
uint64_t votedFor = 0;

uint64_t commitIndex = 0;
uint64_t lastApplied = 0;

uint64_t nextIndex = 0;
uint64_t matchIndex = 0;
// =========================


void init_cluster(Cluster* cluster) {
	
	FILE* config_fp = fopen("cluster.txt", "r");

	if (config_fp == NULL) {
		perror("cluster.txt config file not found");
		exit(1);
	}

	uint64_t length = fsizeof(config_fp);

	uint8_t* buffer = (uint8_t*) malloc(length + 1);
	fread((char*) buffer, 1, length, config_fp);
	buffer[length] = '\0';
	fclose(config_fp);

	SsoString text = SsoString_from_cstr((char*) buffer);

	SsoString* addrs = NULL;
	uint64_t addrs_length = 0;
	SsoString_split(&text, "\n", addrs, &addrs_length);
	SsoString_free(&text);

	for(uint64_t i = 0; i < addrs_length; i++) {
		SsoString addr[2];

		uint64_t addr_lenth = 2;
		SsoString_split(&addrs[i], ":", &addr[0], &addr_lenth);

		char* ip = SsoString_as_cstr(&addr[0]);
		char* port = SsoString_as_cstr(&addr[1]);

		strncpy(cluster->addrs[cluster->length].ip, ip, ADDR_IP_LEN);
		strncpy(cluster->addrs[cluster->length].port, port, ADDR_PORT_LEN);

		cluster->fds[length] = -1;
		cluster->length++;

		SsoString_free(&addr[0]);
		SsoString_free(&addr[1]);
	}

	for(uint64_t i = 0; i < addrs_length; i++) {
		SsoString_free(&addrs[i]);
	}
	free(addrs);
}


int main(int argc, char** argv) {
	if (argc != 3) {
		perror("invalid number of arguments");
		exit(1);
	}
	
	uint64_t addr_index = (uint64_t) atoi(argv[1]);
	
	Cluster cluster = {
		.length = 0,
	};
	
	SsoString storage_path = SsoString_from_cstr(argv[2]);

	
	init_cluster(&cluster);

	for(int i = 0; i < cluster.length; i++) {
		printf("%s | %s\n", cluster.addrs[i].ip, cluster.addrs[i].port);
	}





	SsoString_free(&storage_path);
	return 0;
}