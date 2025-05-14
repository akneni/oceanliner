#include <glib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <glib.h>

#include "../include/spsc.h"

int32_t spsc_queue_init(spsc_queue_t *queue, size_t user_element_size) {
    assert(queue != NULL);
    assert(user_element_size > 0); // Element size must be valid

    // GDestroyNotify for g_free, as g_memdup uses g_malloc
    queue->gqueue = g_async_queue_new_full(g_free);
    if (queue->gqueue == NULL) {
        // This is highly unlikely with GLib unless severe memory exhaustion,
        // as GLib tends to abort on OOM for g_async_queue_new.
        // However, defensive check.
        return -ENOMEM; // Or some other appropriate error
    }
    queue->element_size = user_element_size;
    return 0;
}

int32_t spsc_queue_destroy(spsc_queue_t *queue) {
    assert(queue != NULL && queue->gqueue != NULL);

    // g_async_queue_unref will decrement the reference count.
    // If it drops to zero, the queue will be finalized.
    // Because we used g_async_queue_new_full(g_free),
    // any remaining items (pointers to our copies) in the queue
    // will be passed to g_free automatically.
    g_async_queue_unref(queue->gqueue);
    queue->gqueue = NULL; // Mark as destroyed
    queue->element_size = 0;
    return 0;
}

int32_t spsc_queue_enqueue(spsc_queue_t *queue, const void *item_data_ptr) {
    assert(queue != NULL && queue->gqueue != NULL);
    assert(item_data_ptr != NULL);
    assert(queue->element_size > 0); // Ensure queue was initialized properly

    // Create a heap-allocated copy of the item data.
    // g_memdup uses g_malloc internally and copies `queue->element_size` bytes.
    void *item_copy = g_memdup2(item_data_ptr, queue->element_size);
    if (item_copy == NULL) {
        fprintf(stderr, "spsc_queue_enqueue: g_memdup failed (out of memory).\n");
        return -ENOMEM;
    }

    // Push the pointer to the copy onto the GAsyncQueue.
    // The GAsyncQueue now owns this copy until it's popped or the queue is destroyed.
    g_async_queue_push(queue->gqueue, item_copy);
    return 0;
}

int32_t spsc_queue_dequeue(spsc_queue_t *queue, void *buffer_for_item_ptr) {
    assert(queue != NULL && queue->gqueue != NULL);
    assert(buffer_for_item_ptr != NULL);
    assert(queue->element_size > 0); // Ensure queue was initialized properly

    // Pop the pointer to the heap-allocated copy.
    // g_async_queue_pop() blocks until an item is available or the queue is unreffed.
    gpointer item_copy_ptr = g_async_queue_pop(queue->gqueue);

    if (item_copy_ptr == NULL) {
        // This can happen if the queue is unreffed (destroyed) while pop is waiting.
        // Or if NULL was somehow pushed (which our enqueue doesn't do on OOM, it returns error).
        // Zero out the user's buffer to prevent use of stale data if they don't check return code.
        memset(buffer_for_item_ptr, 0, queue->element_size);
        return -EPIPE; // Indicates a broken pipe or closed queue condition
    }

    // Copy the data from our internal copy to the user's provided buffer.
    memcpy(buffer_for_item_ptr, item_copy_ptr, queue->element_size);

    // Free the heap-allocated copy that we made in enqueue.
    // The user now has their own copy in buffer_for_item_ptr.
    g_free(item_copy_ptr);

    return 0;
}