#include <glib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../include/spsc.h"


int32_t spsc_queue_init(spsc_queue_t *queue, size_t user_element_size) {
    if (queue == NULL) {
        return -EINVAL;
    }

    queue->gqueue = g_async_queue_new();
    if (queue->gqueue == NULL) {
        return -ENOMEM;
    }

    return 0;
}

int32_t spsc_queue_destroy(spsc_queue_t *queue) {
    if (queue == NULL || queue->gqueue == NULL) {
        return -EINVAL;
    }
    g_async_queue_unref(queue->gqueue);
    queue->gqueue = NULL;
    return 0;
}

int32_t spsc_queue_enqueue(spsc_queue_t *queue, const void *item_data_ptr) {
    if (queue == NULL || queue->gqueue == NULL) {
        return -EINVAL;
    }
    g_async_queue_push(queue->gqueue, (gpointer)item_data_ptr);
    return 0;
}

int32_t spsc_queue_dequeue(spsc_queue_t *queue, void *buffer_for_item_ptr) {
    if (queue == NULL || queue->gqueue == NULL || buffer_for_item_ptr == NULL) {
        return -EINVAL;
    }

    gpointer dequeued_data_ptr = g_async_queue_pop(queue->gqueue);


    if (dequeued_data_ptr == NULL) {
        memset(buffer_for_item_ptr, 0, sizeof(gpointer));
        return -EPIPE; 
    }

    memcpy(buffer_for_item_ptr, &dequeued_data_ptr, sizeof(gpointer));

    return 0;
}
