#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(*-identifier-naming)

typedef void (*eillance_libp2p_on_peer_connected_cb)(void* context, const char* peer_id_hex);
typedef void (*eillance_libp2p_on_peer_disconnected_cb)(void* context, const char* peer_id_hex);
typedef void (*eillance_libp2p_on_message_cb)(void* context, const char* peer_id_hex, const uint8_t* data, size_t len);

/* Creates a new libp2p node instance using a 32-byte Ed25519 secret key. Returns an opaque pointer. */
void* eillance_libp2p_node_new(const uint8_t* secret_key_32_bytes);
/* Starts the libp2p swarm background task. */
void eillance_libp2p_node_start(void* node);
/* Signals the node to cleanly shutdown its networking operations. */
void eillance_libp2p_node_stop(void* node);
/* Frees the underlying memory and shuts down the runtime. */
void eillance_libp2p_node_free(void* node);

void eillance_libp2p_node_set_callbacks(
    void* node,
    void* context,
    eillance_libp2p_on_peer_connected_cb on_connected,
    eillance_libp2p_on_peer_disconnected_cb on_disconnected,
    eillance_libp2p_on_message_cb on_message
);

bool eillance_libp2p_node_send_message(void* node, const char* peer_id_hex, const uint8_t* data, size_t len);
void eillance_libp2p_node_dial(void* node, const char* peer_id_hex);
void eillance_libp2p_node_add_bootstrap_node(void* node, const char* multiaddr);
void eillance_libp2p_node_listen(void* node, const char* multiaddr);

// NOLINTEND(*-identifier-naming)

#ifdef __cplusplus
}
#endif
