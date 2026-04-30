#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NOLINTBEGIN(*-identifier-naming)

typedef enum {
  EILLANCE_NODE_ROLE_BOOTSTRAP_NODE = 0,
  EILLANCE_NODE_ROLE_DESKTOP_CLIENT = 1,
  EILLANCE_NODE_ROLE_MOBILE_CLIENT = 2,
  EILLANCE_NODE_ROLE_BOT = 3
} eillance_libp2p_node_role;

typedef void (*eillance_libp2p_on_peer_connected_cb)(void* context, char const* peer_id_hex, bool is_circuit);
typedef void (*eillance_libp2p_on_peer_disconnected_cb)(void* context, char const* peer_id_hex);
typedef void (*eillance_libp2p_on_message_cb)(void* context, char const* peer_id_hex, uint8_t const* data, size_t len);
typedef void (*eillance_libp2p_log_cb)(void* context, int level, char const* message);
typedef void (*eillance_libp2p_on_turn_relays_discovered_cb)(void* context, char const* providers_csv);
typedef void (*eillance_libp2p_on_turn_auth_result_cb)(void* context, char const* peer_id_hex, char const* ip, uint16_t port, char const* username, char const* credential);

/* Creates a new libp2p node instance using a 32-byte Ed25519 secret key. Returns an opaque pointer. */
auto eillance_libp2p_node_new(uint8_t const* secret_key_32_bytes, eillance_libp2p_node_role role) -> void*;
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
    eillance_libp2p_on_message_cb on_message,
    eillance_libp2p_log_cb on_log,
    eillance_libp2p_on_turn_relays_discovered_cb on_turn_relays_discovered,
    eillance_libp2p_on_turn_auth_result_cb on_turn_auth_result
);

void eillance_libp2p_node_disconnect(void* node, char const* peer_id_hex);

auto eillance_libp2p_node_send_chat(void* node, char const* peer_id_hex, uint8_t const* data, size_t len) -> bool;
auto eillance_libp2p_node_send_file(void* node, char const* peer_id_hex, uint8_t const* data, size_t len) -> bool;
void eillance_libp2p_node_dial(void* node, char const* peer_id_hex);
void eillance_libp2p_node_add_bootstrap_node(void* node, char const* multiaddr);
void eillance_libp2p_node_listen(void* node, char const* multiaddr);

void eillance_libp2p_node_discover_turn_relays(void* node);
void eillance_libp2p_node_auth_turn_relay(void* node, char const* peer_id_hex);

// NOLINTEND(*-identifier-naming)

#ifdef __cplusplus
}
#endif
