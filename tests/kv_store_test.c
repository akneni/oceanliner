#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../include/globals.h"
#include "../include/log_file.h"
#include "../include/kv_store.h"  



void init_logfile(logfile_t* logfile, char* logfile_name) {
	int32_t permissions = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;

	logfile->file_fd = open(logfile_name, O_RDWR | O_SYNC | O_CREAT, permissions);
	if (logfile->file_fd < 0) {
		perror("Failed to open file");
		exit(1);
	}
}

int main() {
    logfile_t logfile;

    init_logfile(&logfile, "test-logfile.bin");

    char key[128] = "name:";
    

    kv_store_t disk_map = kv_store_init("test-kv-store.bin", &logfile);


    char value[128] = "value-123";
    kv_store_set(&disk_map, 0, key, (uint8_t*) value, strlen(value) + 1);

    kv_store_set(&disk_map, 0, key, (uint8_t*) value, strlen(value) + 1);

    strcpy(value, "billy");
    kv_store_set(&disk_map, 0, key, (uint8_t*) value, strlen(value) + 1);

    kv_store_delete(&disk_map, key);

    printf("(1)\n");


    strcpy(value, "k8s\0");
    int64_t res =  kv_store_set(&disk_map, 0, key, (uint8_t*) value, strlen(value) + 1);
    assert(res == 1);

    printf("(2)\n");

    int64_t value_length;
    uint8_t* data = kv_store_get(&disk_map, key, &value_length);

    printf("(3)\n");

    if (data == NULL) {
        printf("data  =  NULL\n");
    }
    else {
        assert(value_length > 0);
        printf("data  =  `%s`\n", (char*) data);
        free(data);
    }





    return 0;
}