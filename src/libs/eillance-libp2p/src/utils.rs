use crate::types::Callbacks;
use libp2p::{Multiaddr, PeerId, identity, swarm::NetworkBehaviour};
use std::ffi::CString;
use std::os::raw::{c_char, c_void};

#[macro_export]
macro_rules! node_log {
    ($callbacks:expr, $level:expr, $($arg:tt)*) => {
        if $callbacks.on_log != 0 {
            let msg = format!($($arg)*);
            if let Ok(c_str) = std::ffi::CString::new(msg) {
                let cb: extern "C" fn(*mut std::os::raw::c_void, i32, *const std::os::raw::c_char) = unsafe { std::mem::transmute($callbacks.on_log) };
                cb($callbacks.context as *mut std::os::raw::c_void, $level, c_str.as_ptr());
            }
        } else {
            eprintln!($($arg)*);
        }
    }
}

pub fn pubkey_to_peerid(raw: &[u8; 32]) -> Option<PeerId> {
  identity::ed25519::PublicKey::try_from_bytes(&raw[..])
    .ok()
    .map(|pk| identity::PublicKey::from(pk).to_peer_id())
}

pub fn peerid_to_pubkey(id: &PeerId) -> Option<[u8; 32]> {
  let mh = id.as_ref();
  if mh.code() == 0x00 {
    if let Ok(pk) = identity::PublicKey::try_decode_protobuf(mh.digest()) {
      if let Ok(ed25519_pk) = pk.try_into_ed25519() {
        let mut pubkey = [0u8; 32];
        pubkey.copy_from_slice(&ed25519_pk.to_bytes());
        return Some(pubkey);
      }
    }
  }
  None
}

pub fn hex_decode(s: &str) -> Result<Vec<u8>, ()> {
  if s.len() % 2 != 0 {
    return Err(());
  }
  (0..s.len())
    .step_by(2)
    .map(|i| u8::from_str_radix(&s[i..i + 2], 16).map_err(|_| ()))
    .collect()
}

pub fn hex_encode(bytes: &[u8]) -> String {
  bytes.iter().map(|b| format!("{:02x}", b)).collect()
}

pub fn notify_connected(callbacks: &Callbacks, peer_id: &PeerId, is_circuit: bool) {
  if callbacks.on_connected != 0 {
    let cb: extern "C" fn(*mut c_void, *const c_char, bool) = unsafe { std::mem::transmute(callbacks.on_connected) };
    if let Some(pk) = peerid_to_pubkey(peer_id) {
      if let Ok(c_str) = CString::new(hex_encode(&pk)) {
        cb(callbacks.context as *mut c_void, c_str.as_ptr(), is_circuit);
      }
    }
  }
}

pub fn notify_disconnected<T: NetworkBehaviour>(callbacks: &Callbacks, swarm: &libp2p::Swarm<T>, peer_id: &PeerId) {
  if swarm.is_connected(peer_id) {
    return;
  }
  if callbacks.on_disconnected != 0 {
    let cb: extern "C" fn(*mut c_void, *const c_char) = unsafe { std::mem::transmute(callbacks.on_disconnected) };
    if let Some(pk) = peerid_to_pubkey(peer_id) {
      if let Ok(c_str) = CString::new(hex_encode(&pk)) {
        cb(callbacks.context as *mut c_void, c_str.as_ptr());
      }
    }
  }
}

pub async fn resolve_multiaddr(addr: Multiaddr) -> Option<Multiaddr> {
  let mut components = addr.iter();
  let first = components.next()?;

  let (host, is_dns4, is_dns6) = match first {
    libp2p::multiaddr::Protocol::Dns(ref h) => (h.to_string(), true, true),
    libp2p::multiaddr::Protocol::Dns4(ref h) => (h.to_string(), true, false),
    libp2p::multiaddr::Protocol::Dns6(ref h) => (h.to_string(), false, true),
    _ => return Some(addr),
  };

  let second = components.next()?;
  let port = match second {
    libp2p::multiaddr::Protocol::Tcp(p) => p,
    libp2p::multiaddr::Protocol::Udp(p) => p,
    _ => return Some(addr),
  };

  let host_port = format!("{}:{}", host, port);
  if let Ok(mut addrs) = tokio::net::lookup_host(&host_port).await {
    while let Some(socket_addr) = addrs.next() {
      if socket_addr.is_ipv4() && !is_dns4 {
        continue;
      }
      if socket_addr.is_ipv6() && !is_dns6 {
        continue;
      }

      let mut resolved = Multiaddr::empty();
      match socket_addr.ip() {
        std::net::IpAddr::V4(ip) => resolved.push(libp2p::multiaddr::Protocol::Ip4(ip)),
        std::net::IpAddr::V6(ip) => resolved.push(libp2p::multiaddr::Protocol::Ip6(ip)),
      }
      resolved.push(second.clone());

      for remaining in components.by_ref() {
        resolved.push(remaining);
      }
      return Some(resolved);
    }
  }
  None
}
