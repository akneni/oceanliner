#ifndef SPSC_H
#define SPSC_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <glib.h>


typedef struct {
    GAsyncQueue *gqueue;
} spsc_queue_t;


int32_t spsc_queue_init(spsc_queue_t *queue, size_t user_element_size);
int32_t spsc_queue_destroy(spsc_queue_t *queue);
int32_t spsc_queue_enqueue(spsc_queue_t *queue, const void* item_data);
int32_t spsc_queue_dequeue(spsc_queue_t *queue, void* buffer_for_item);

#endif // SPSC_H