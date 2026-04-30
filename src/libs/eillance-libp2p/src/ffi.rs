use std::ffi::CStr;
use std::os::raw::{c_char, c_void};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::str::FromStr;

use tokio::runtime::Runtime;
use tokio::sync::mpsc;

use libp2p::{Multiaddr, SwarmBuilder, identity, noise, tcp, yamux};

use crate::behaviour::build_behaviour;
use crate::event_loop::run_event_loop;
use crate::node::Node;
use crate::types::{Callbacks, Command, NodeRole};
use crate::utils::{hex_decode, pubkey_to_peerid};

fn run_ffi<F, R>(f: F, default: R) -> R
where
  F: FnOnce() -> R + std::panic::UnwindSafe,
{
  match catch_unwind(f) {
    Ok(res) => res,
    Err(err) => {
      if let Some(s) = err.downcast_ref::<&str>() {
        eprintln!("Panic in libp2p FFI: {}", s);
      } else if let Some(s) = err.downcast_ref::<String>() {
        eprintln!("Panic in libp2p FFI: {}", s);
      } else {
        eprintln!("Panic in libp2p FFI: Unknown error type");
      }
      default
    }
  }
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_new(secret_key: *const u8, role_val: i32) -> *mut c_void {
  run_ffi(
    AssertUnwindSafe(|| {
      if secret_key.is_null() {
        eprintln!("Null secret_key passed to node_new");
        return std::ptr::null_mut();
      }
      let mut key_bytes = [0u8; 32];
      unsafe { std::ptr::copy_nonoverlapping(secret_key, key_bytes.as_mut_ptr(), 32) };

      let secret = match identity::ed25519::SecretKey::try_from_bytes(key_bytes) {
        Ok(s) => s,
        Err(e) => {
          eprintln!("Invalid ed25519 secret key: {:?}", e);
          return std::ptr::null_mut();
        }
      };
      let keypair = identity::Keypair::from(identity::ed25519::Keypair::from(secret));

      let rt = match Runtime::new() {
        Ok(rt) => rt,
        Err(e) => {
          eprintln!("Failed to create tokio runtime: {:?}", e);
          return std::ptr::null_mut();
        }
      };

      let node = Box::new(Node {
        runtime: rt,
        command_tx: None,
        keypair,
        role: NodeRole::from(role_val),
        callbacks: Callbacks::default(),
      });
      Box::into_raw(node) as *mut c_void
    }),
    std::ptr::null_mut(),
  )
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_set_callbacks(
  ptr: *mut c_void,
  context: *mut c_void,
  on_connected: *const c_void,
  on_disconnected: *const c_void,
  on_message: *const c_void,
  on_log: *const c_void,
) {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() {
        eprintln!("Null pointer passed to set_callbacks");
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      node.callbacks = Callbacks {
        context: context as usize,
        on_connected: on_connected as usize,
        on_disconnected: on_disconnected as usize,
        on_message: on_message as usize,
        on_log: on_log as usize,
      };
    }),
    (),
  );
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_start(ptr: *mut c_void) {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() {
        eprintln!("Null pointer passed to node_start");
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      if node.command_tx.is_some() {
        eprintln!("Node already started");
        return;
      }

      let (tx, rx) = mpsc::unbounded_channel();
      node.command_tx = Some(tx.clone());
      let local_key = node.keypair.clone();
      let callbacks = node.callbacks.clone();
      let role = node.role;

      node.runtime.spawn(async move {
        let tcp_cfg = tcp::Config::default();
        let swarm = match SwarmBuilder::with_existing_identity(local_key.clone())
          .with_tokio()
          .with_tcp(tcp_cfg, noise::Config::new, yamux::Config::default)
        {
          Ok(b) => match b.with_relay_client(noise::Config::new, yamux::Config::default) {
            Ok(b) => match b.with_behaviour(|key, relay_client| build_behaviour(key, relay_client, role)) {
              Ok(b) => b
                .with_swarm_config(|c| c.with_idle_connection_timeout(std::time::Duration::from_secs(3600)))
                .build(),
              Err(e) => {
                eprintln!("Failed to build swarm behaviour: {:?}", e);
                return;
              }
            },
            Err(e) => {
              eprintln!("Failed to configure relay client: {:?}", e);
              return;
            }
          },
          Err(e) => {
            eprintln!("Failed to configure tcp transport: {:?}", e);
            return;
          }
        };

        run_event_loop(swarm, rx, tx, callbacks).await;
      });
    }),
    (),
  );
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_stop(ptr: *mut c_void) {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() {
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      if let Some(tx) = node.command_tx.take() {
        let _ = tx.send(Command::Stop);
      }
    }),
    (),
  );
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_free(ptr: *mut c_void) {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() {
        return;
      }
      unsafe { drop(Box::from_raw(ptr as *mut Node)) };
    }),
    (),
  );
}

fn send_internal(ptr: *mut c_void, peer_id_hex: *const c_char, data: *const u8, len: usize, is_file: bool) -> bool {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() || peer_id_hex.is_null() || data.is_null() {
        return false;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      if let Ok(hex_str) = unsafe { CStr::from_ptr(peer_id_hex) }.to_str() {
        if let Ok(peer_bytes) = hex_decode(hex_str) {
          if peer_bytes.len() == 32 {
            let mut arr = [0u8; 32];
            arr.copy_from_slice(&peer_bytes);
            if let Some(peer_id) = pubkey_to_peerid(&arr) {
              let payload = unsafe { std::slice::from_raw_parts(data, len) }.to_vec();
              if let Some(tx) = &node.command_tx {
                let cmd = if is_file {
                  Command::SendFile(peer_id, payload)
                } else {
                  Command::SendChat(peer_id, payload)
                };
                return tx.send(cmd).is_ok();
              }
            }
          }
        }
      }
      false
    }),
    false,
  )
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_send_chat(
  ptr: *mut c_void,
  peer_id_hex: *const c_char,
  data: *const u8,
  len: usize,
) -> bool {
  send_internal(ptr, peer_id_hex, data, len, false)
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_send_file(
  ptr: *mut c_void,
  peer_id_hex: *const c_char,
  data: *const u8,
  len: usize,
) -> bool {
  send_internal(ptr, peer_id_hex, data, len, true)
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_disconnect(ptr: *mut c_void, peer_id_hex: *const c_char) {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() || peer_id_hex.is_null() {
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      if let Ok(hex_str) = unsafe { CStr::from_ptr(peer_id_hex) }.to_str() {
        if let Ok(peer_bytes) = hex_decode(hex_str) {
          if peer_bytes.len() == 32 {
            let mut arr = [0u8; 32];
            arr.copy_from_slice(&peer_bytes);
            if let Some(peer_id) = pubkey_to_peerid(&arr) {
              if let Some(tx) = &node.command_tx {
                let _ = tx.send(Command::Disconnect(peer_id));
              }
            }
          }
        }
      }
    }),
    (),
  );
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_dial(ptr: *mut c_void, peer_id_hex: *const c_char) {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() || peer_id_hex.is_null() {
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      if let Ok(hex_str) = unsafe { CStr::from_ptr(peer_id_hex) }.to_str() {
        if let Ok(peer_bytes) = hex_decode(hex_str) {
          if peer_bytes.len() == 32 {
            let mut arr = [0u8; 32];
            arr.copy_from_slice(&peer_bytes);
            if let Some(peer_id) = pubkey_to_peerid(&arr) {
              if let Some(tx) = &node.command_tx {
                let _ = tx.send(Command::Dial(peer_id));
              }
            }
          }
        }
      }
    }),
    (),
  );
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_add_bootstrap_node(ptr: *mut c_void, multiaddr: *const c_char) {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() || multiaddr.is_null() {
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      if let Ok(addr_str) = unsafe { CStr::from_ptr(multiaddr) }.to_str() {
        if let Ok(addr) = Multiaddr::from_str(addr_str) {
          if let Some(tx) = &node.command_tx {
            let _ = tx.send(Command::AddBootstrap(addr));
          }
        }
      }
    }),
    (),
  );
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_listen(ptr: *mut c_void, multiaddr: *const c_char) {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() || multiaddr.is_null() {
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      if let Ok(addr_str) = unsafe { CStr::from_ptr(multiaddr) }.to_str() {
        if let Ok(addr) = Multiaddr::from_str(addr_str) {
          if let Some(tx) = &node.command_tx {
            let _ = tx.send(Command::Listen(addr));
          }
        }
      }
    }),
    (),
  );
}
