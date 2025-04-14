#ifndef LOG_FILE_H
#define LOG_FILE_H

#include <stdint.h>
#include <string.h>
#include "../include/globals.h"



uint64_t LogEntry_len(const uint8_t* log_entry);
StateCommand LogEntry_get_cmd(const uint8_t* log_entry);
char* LogEntry_get_key(const uint8_t* log_entry);
void LogEntry_get_value(const uint8_t* log_entry, uint8_t** value, uint64_t* value_len);
void LogFile_get_data(uint64_t cmd_offset, uint8_t* out_buffer, size_t length);

#endif // LOG_FILE_H