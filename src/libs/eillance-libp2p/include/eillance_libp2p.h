#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Creates a new libp2p node instance. Returns an opaque pointer. */
void* eillance_libp2p_node_new();
/* Starts the libp2p swarm background task. */
void eillance_libp2p_node_start(void* node);
/* Signals the node to cleanly shutdown its networking operations. */
void eillance_libp2p_node_stop(void* node);
/* Frees the underlying memory and shuts down the runtime. */
void eillance_libp2p_node_free(void* node);

#ifdef __cplusplus
}
#endif
