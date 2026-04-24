use futures::StreamExt;
use libp2p::{
  Multiaddr, PeerId, StreamProtocol, SwarmBuilder, identify, identity,
  kad::{self, Behaviour as Kademlia, Config as KademliaConfig, store::MemoryStore},
  mdns, noise, relay,
  ping,
  request_response::{self, ProtocolSupport},
  swarm::{NetworkBehaviour, SwarmEvent},
  tcp, yamux,
};
use std::collections::{HashMap, HashSet};
use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_void};
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::str::FromStr;
use std::time::Duration;
use tokio::runtime::Runtime;
use tokio::sync::mpsc;

#[derive(NetworkBehaviour)]
struct MercuryBehaviour {
  relay_client: relay::client::Behaviour,
  relay_server: relay::Behaviour,
  kademlia: Kademlia<MemoryStore>,
  req_resp: request_response::cbor::Behaviour<Vec<u8>, Vec<u8>>,
  mdns: mdns::tokio::Behaviour,
  identify: identify::Behaviour,
  ping: ping::Behaviour,
}

#[derive(Clone, Default)]
struct Callbacks {
  context: usize,
  on_connected: usize,
  on_disconnected: usize,
  on_message: usize,
  on_log: usize,
}
unsafe impl Send for Callbacks {}
unsafe impl Sync for Callbacks {}

enum Command {
  Dial(PeerId),
  AddBootstrap(Multiaddr),
  AddBootstrapResolved(Multiaddr),
  Listen(Multiaddr),
  SendMessage(PeerId, Vec<u8>),
  Disconnect(PeerId),
  Stop,
}

macro_rules! node_log {
    ($callbacks:expr, $level:expr, $($arg:tt)*) => {
        if $callbacks.on_log != 0 {
            let msg = format!($($arg)*);
            if let Ok(c_str) = std::ffi::CString::new(msg) {
                let cb: extern "C" fn(*mut c_void, i32, *const c_char) = unsafe { std::mem::transmute($callbacks.on_log) };
                cb($callbacks.context as *mut c_void, $level, c_str.as_ptr());
            }
        } else {
            eprintln!($($arg)*);
        }
    }
}

async fn resolve_multiaddr(addr: Multiaddr) -> Option<Multiaddr> {
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

struct Node {
  runtime: Runtime,
  command_tx: Option<mpsc::UnboundedSender<Command>>,
  keypair: identity::Keypair,
  is_server: bool,
  callbacks: Callbacks,
}

fn pubkey_to_peerid(raw: &[u8; 32]) -> Option<PeerId> {
  identity::ed25519::PublicKey::try_from_bytes(&raw[..])
    .ok()
    .map(|pk| identity::PublicKey::from(pk).to_peer_id())
}

fn peerid_to_pubkey(id: &PeerId) -> Option<[u8; 32]> {
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

fn hex_decode(s: &str) -> Result<Vec<u8>, ()> {
  if s.len() % 2 != 0 {
    return Err(());
  }
  (0..s.len())
    .step_by(2)
    .map(|i| u8::from_str_radix(&s[i..i + 2], 16).map_err(|_| ()))
    .collect()
}

fn hex_encode(bytes: &[u8]) -> String {
  bytes.iter().map(|b| format!("{:02x}", b)).collect()
}

fn notify_connected(callbacks: &Callbacks, peer_id: &PeerId) {
  if callbacks.on_connected != 0 {
    let cb: extern "C" fn(*mut c_void, *const c_char) = unsafe { std::mem::transmute(callbacks.on_connected) };
    if let Some(pk) = peerid_to_pubkey(peer_id) {
      if let Ok(c_str) = CString::new(hex_encode(&pk)) {
        cb(callbacks.context as *mut c_void, c_str.as_ptr());
      } else {
        eprintln!("Failed to create CString from peer public key");
      }
    }
  }
}

fn notify_disconnected(callbacks: &Callbacks, swarm: &libp2p::Swarm<MercuryBehaviour>, peer_id: &PeerId) {
  if swarm.is_connected(peer_id) {
    return;
  }
  if callbacks.on_disconnected != 0 {
    let cb: extern "C" fn(*mut c_void, *const c_char) = unsafe { std::mem::transmute(callbacks.on_disconnected) };
    if let Some(pk) = peerid_to_pubkey(peer_id) {
      if let Ok(c_str) = CString::new(hex_encode(&pk)) {
        cb(callbacks.context as *mut c_void, c_str.as_ptr());
      } else {
        eprintln!("Failed to create CString from peer public key");
      }
    }
  }
}

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
pub extern "C" fn eillance_libp2p_node_new(secret_key: *const u8, is_server: bool) -> *mut c_void {
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
        is_server,
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

      let (tx, mut rx) = mpsc::unbounded_channel();
      node.command_tx = Some(tx.clone());
      let local_key = node.keypair.clone();
      let callbacks = node.callbacks.clone();
      let is_server = node.is_server;

      node.runtime.spawn(async move {
            let tcp_cfg = tcp::Config::default();
            let mut swarm = match SwarmBuilder::with_existing_identity(local_key.clone())
                .with_tokio()
                .with_tcp(tcp_cfg, noise::Config::new, yamux::Config::default) {
                Ok(b) => match b.with_relay_client(noise::Config::new, yamux::Config::default) {
                    Ok(b) => match b.with_behaviour(|key, relay_client| {
                        let peer_id = PeerId::from(key.public());
                        let mut kad_config = KademliaConfig::default();
                        kad_config.set_record_ttl(Some(Duration::from_secs(60 * 60)));

                        let mut kademlia = Kademlia::with_config(peer_id, MemoryStore::new(peer_id), kad_config);
                        if is_server {
                            kademlia.set_mode(Some(kad::Mode::Server));
                        } else {
                            kademlia.set_mode(Some(kad::Mode::Client));
                        }

                        let mdns = mdns::tokio::Behaviour::new(mdns::Config::default(), peer_id).expect("mDNS valid");
                        let req_resp = request_response::cbor::Behaviour::new([(StreamProtocol::new("/mercury/1.0.0"), ProtocolSupport::Full)],
                                                                              request_response::Config::default(),
                        );
                        let identify = identify::Behaviour::new(identify::Config::new(
                            "/mercury/id/1.0.0".into(),
                            key.public(),
                        ));

                        let relay_config = if is_server {
                            relay::Config {
                                max_reservations: 8192,
                                max_reservations_per_peer: 1024,
                                reservation_rate_limiters: vec![],
                                max_circuits: 8192,
                                max_circuits_per_peer: 1024,
                                circuit_src_rate_limiters: vec![],
                                ..Default::default()
                            }
                        } else {
                            relay::Config::default()
                        };
                        let relay_server = relay::Behaviour::new(peer_id, relay_config);
                        let ping = ping::Behaviour::new(ping::Config::new().with_interval(Duration::from_secs(15)));

                        MercuryBehaviour { relay_client, relay_server, kademlia, req_resp, mdns, identify, ping }
                    }) {
                        Ok(b) => b.with_swarm_config(|c| c.with_idle_connection_timeout(Duration::from_secs(3600))).build(),
                        Err(e) => {
                            eprintln!("Failed to build swarm behaviour: {:?}", e);
                            return;
                        }
                    }
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

            let mut pending_dials = HashSet::new();
            let mut known_addresses: HashMap<PeerId, HashSet<Multiaddr>> = HashMap::new();
            let mut explicit_bootstrap_peers: HashSet<PeerId> = HashSet::new();
            let mut active_relay_peers: HashMap<PeerId, Multiaddr> = HashMap::new();
            let mut bootstrap_addrs: HashSet<Multiaddr> = HashSet::new();
            let mut keepalive_interval = tokio::time::interval(Duration::from_secs(30));

            macro_rules! do_dial {
                ($peer:expr) => {
                    let peer_val = $peer;
                    let known = known_addresses.get(&peer_val);
                    let mut addrs: Vec<_> = known.map(|s| s.iter().cloned().collect()).unwrap_or_default();

                    if addrs.is_empty() {
                        swarm.behaviour_mut().kademlia.get_closest_peers(peer_val);
                        pending_dials.insert(peer_val);
                    }

                    for (_, relay_addr) in &active_relay_peers {
                        let mut relayed = relay_addr.clone();
                        relayed.push(libp2p::multiaddr::Protocol::P2pCircuit);
                        relayed.push(libp2p::multiaddr::Protocol::P2p(peer_val));
                        if !addrs.contains(&relayed) {
                            addrs.push(relayed);
                        }
                    }
                    for addr in &addrs {
                        swarm.behaviour_mut().kademlia.add_address(&peer_val, addr.clone());
                    }
                    let opts = if addrs.is_empty() {
                        libp2p::swarm::dial_opts::DialOpts::peer_id(peer_val).build()
                    } else {
                        libp2p::swarm::dial_opts::DialOpts::peer_id(peer_val).addresses(addrs).build()
                    };
                    match swarm.dial(opts) {
                        Ok(_) => {
                            // Dial initiated. Do not remove from pending_dials here so that if 
                            // DHT finds direct addresses in the meantime, they can still be processed.
                        }
                        Err(libp2p::swarm::DialError::NoAddresses) => {
                            swarm.behaviour_mut().kademlia.get_closest_peers(peer_val);
                            pending_dials.insert(peer_val);
                        }
                        Err(libp2p::swarm::DialError::DialPeerConditionFalse(_)) => {
                            // If condition is false, peer might already be connected. If not, it means a dial
                            // is already inflight. Let libp2p handle the inflight dial naturally.
                            if swarm.is_connected(&peer_val) {
                                pending_dials.remove(&peer_val);
                                notify_connected(&callbacks, &peer_val);
                            }
                        }
                        Err(e) => {
                            eprintln!("Failed to dial peer {}: {:?}", peer_val, e);
                            pending_dials.remove(&peer_val);
                            notify_disconnected(&callbacks, &swarm, &peer_val);
                        }
                    }
                }
            }

            loop {
                tokio::select! {
                    _ = keepalive_interval.tick() => {
                        for b_addr in &bootstrap_addrs {
                            let mut p2p_peer_id = None;
                            for protocol in b_addr.iter() {
                                if let libp2p::multiaddr::Protocol::P2p(peer_id) = protocol {
                                    p2p_peer_id = Some(peer_id);
                                }
                            }
                            if let Some(peer_id) = p2p_peer_id {
                                if !swarm.is_connected(&peer_id) {
                                    node_log!(callbacks, 3, "Periodic check: Bootstrap node {} not connected, dialing...", peer_id);
                                    let _ = swarm.dial(b_addr.clone());
                                }
                            }
                        }
                    }
                    cmd = rx.recv() => match cmd {
                        Some(Command::Stop) | None => break,
                        Some(Command::Dial(peer_id)) => {
                            node_log!(callbacks, 4, "Dial command received for {}", peer_id);
                            do_dial!(peer_id);
                        }
                        Some(Command::AddBootstrap(addr)) => {
                            let tx_clone = tx.clone();
                            tokio::spawn(async move {
                                if let Some(resolved) = resolve_multiaddr(addr).await {
                                    let _ = tx_clone.send(Command::AddBootstrapResolved(resolved));
                                } else {
                                    eprintln!("Failed to resolve bootstrap node");
                                }
                            });
                        }
                        Some(Command::AddBootstrapResolved(addr)) => {
                            bootstrap_addrs.insert(addr.clone());
                            let mut p2p_peer_id = None;
                            for protocol in addr.iter() {
                                if let libp2p::multiaddr::Protocol::P2p(peer_id) = protocol {
                                    p2p_peer_id = Some(peer_id);
                                }
                            }
                            if let Some(peer_id) = p2p_peer_id {
                                explicit_bootstrap_peers.insert(peer_id);
                                known_addresses.entry(peer_id).or_default().insert(addr.clone());
                                swarm.behaviour_mut().kademlia.add_address(&peer_id, addr.clone());
                            }
                            if let Err(e) = swarm.dial(addr.clone()) {
                                node_log!(callbacks, 1, "Failed to dial bootstrap node {}: {:?}", addr, e);
                            }
                        }
                        Some(Command::Listen(addr)) => {
                            if let Err(e) = swarm.listen_on(addr.clone()) {
                                node_log!(callbacks, 1, "Failed to listen on {}: {:?}", addr, e);
                            }
                        }
                        Some(Command::SendMessage(peer_id, data)) => {
                            swarm.behaviour_mut().req_resp.send_request(&peer_id, data);
                        }
                        Some(Command::Disconnect(peer_id)) => {
                            node_log!(callbacks, 4, "Disconnect command received for {}", peer_id);
                            let _ = swarm.disconnect_peer_id(peer_id);
                        }
                    },
                    event = swarm.select_next_some() => match event {
                        SwarmEvent::NewListenAddr { address, .. } => {
                            node_log!(callbacks, 3, "Listen Active. Multiaddr: {}", address);
                        }
                        SwarmEvent::ConnectionEstablished { peer_id, endpoint, .. } => {
                            let mut remote_addr = endpoint.get_remote_address().clone();

                            // Ensure remote_addr has an IP address for relay listening and dialing
                            if !remote_addr.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::Ip4(_) | libp2p::multiaddr::Protocol::Ip6(_))) {
                                let mut found = false;
                                if let Some(addrs) = known_addresses.get(&peer_id) {
                                    if let Some(ip_addr) = addrs.iter().find(|a| a.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::Ip4(_) | libp2p::multiaddr::Protocol::Ip6(_)))) {
                                        remote_addr = ip_addr.clone();
                                        found = true;
                                    }
                                }
                                if !found {
                                    for b_addr in &bootstrap_addrs {
                                        if b_addr.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::P2p(id) if id == peer_id)) {
                                            if b_addr.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::Ip4(_) | libp2p::multiaddr::Protocol::Ip6(_))) {
                                                remote_addr = b_addr.clone();
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                            if !remote_addr.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::P2p(_))) {
                                remote_addr.push(libp2p::multiaddr::Protocol::P2p(peer_id));
                            }

                            known_addresses.entry(peer_id).or_default().insert(remote_addr.clone());
                            swarm.behaviour_mut().kademlia.add_address(&peer_id, remote_addr.clone());
                            node_log!(callbacks, 3, "Connection established with {}", peer_id);

                            let mut is_bootstrap = explicit_bootstrap_peers.contains(&peer_id);
                            if !is_bootstrap {
                                for b_addr in &bootstrap_addrs {
                                    let b_addr_len = b_addr.iter().count();
                                    if remote_addr.iter().take(b_addr_len).eq(b_addr.iter()) {
                                        is_bootstrap = true;
                                        explicit_bootstrap_peers.insert(peer_id);
                                        break;
                                    }
                                }
                            }

                            if endpoint.is_dialer() {
                                let _ = swarm.behaviour_mut().kademlia.bootstrap();

                                if is_bootstrap {
                                    let is_circuit = remote_addr.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit));
                                    if !is_circuit {
                                        let mut relay_listen_addr = remote_addr.clone();
                                        relay_listen_addr.push(libp2p::multiaddr::Protocol::P2pCircuit);

                                        if let Err(e) = swarm.listen_on(relay_listen_addr) {
                                            node_log!(callbacks, 2, "Failed to listen on relay: {:?}", e);
                                        }
                                        active_relay_peers.insert(peer_id, remote_addr);
                                    }
                                }
                            }

                            pending_dials.remove(&peer_id);
                            if !is_bootstrap {
                                notify_connected(&callbacks, &peer_id);
                            }
                        }
                        SwarmEvent::ConnectionClosed { peer_id, .. } => {
                            node_log!(callbacks, 3, "Connection closed with {}", peer_id);
                            if !swarm.is_connected(&peer_id) {
                                active_relay_peers.remove(&peer_id);
                            }
                            if !explicit_bootstrap_peers.contains(&peer_id) {
                                notify_disconnected(&callbacks, &swarm, &peer_id);
                            }
                        }
                        SwarmEvent::OutgoingConnectionError { peer_id: Some(peer_id), error, .. } => {
                            node_log!(callbacks, 1, "Outgoing connection error to {}: {:?}", peer_id, error);
                            known_addresses.remove(&peer_id);
                            swarm.behaviour_mut().kademlia.remove_peer(&peer_id);
                            pending_dials.remove(&peer_id);
                            if !explicit_bootstrap_peers.contains(&peer_id) {
                                notify_disconnected(&callbacks, &swarm, &peer_id);
                            }
                        }
                        SwarmEvent::OutgoingConnectionError { peer_id: None, error, .. } => {
                            node_log!(callbacks, 1, "Outgoing connection error to unknown peer: {:?}", error);
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::Identify(identify::Event::Received { peer_id, info })) => {
                            for addr in info.listen_addrs {
                                known_addresses.entry(peer_id).or_default().insert(addr.clone());
                                swarm.behaviour_mut().kademlia.add_address(&peer_id, addr);
                            }
                            if let Err(e) = swarm.behaviour_mut().kademlia.bootstrap() {
                                node_log!(callbacks, 2, "Kademlia bootstrap failed: {:?}", e);
                            }
                            if pending_dials.contains(&peer_id) {
                                do_dial!(peer_id);
                            }
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::Mdns(mdns::Event::Discovered(list))) => {
                            for (peer_id, addr) in list {
                                known_addresses.entry(peer_id).or_default().insert(addr.clone());
                                swarm.behaviour_mut().kademlia.add_address(&peer_id, addr);
                                if pending_dials.contains(&peer_id) {
                                    do_dial!(peer_id);
                                }
                            }
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(kad::Event::OutboundQueryProgressed {
                            result: kad::QueryResult::GetClosestPeers(Ok(res)),
                            ..
                        })) => {
                            for peer in res.peers {
                                if pending_dials.contains(&peer) {
                                    do_dial!(peer);
                                }
                            }
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(kad::Event::OutboundQueryProgressed {
                            result: kad::QueryResult::GetClosestPeers(Err(e)),
                            ..
                        })) => {
                            node_log!(callbacks, 2, "Kademlia GetClosestPeers failed: {:?}", e);
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(kad::Event::RoutingUpdated { peer, addresses, .. })) => {
                            for addr in addresses.iter() {
                                known_addresses.entry(peer).or_default().insert(addr.clone());
                            }
                            if pending_dials.contains(&peer) {
                                do_dial!(peer);
                            }
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::ReqResp(request_response::Event::Message { peer, message })) => {
                            if let request_response::Message::Request { request, channel, .. } = message {
                                if callbacks.on_message != 0 {
                                    let cb: extern "C" fn(*mut c_void, *const c_char, *const u8, usize) = unsafe { std::mem::transmute(callbacks.on_message) };
                                    if let Some(pk) = peerid_to_pubkey(&peer) {
                                        if let Ok(c_str) = CString::new(hex_encode(&pk)) {
                                            cb(callbacks.context as *mut c_void, c_str.as_ptr(), request.as_ptr(), request.len());
                                        } else {
                                            eprintln!("Failed to create CString from peer public key");
                                        }
                                    }
                                }
                                if let Err(e) = swarm.behaviour_mut().req_resp.send_response(channel, vec![]) {
                                    node_log!(callbacks, 1, "Failed to send req_resp response: {:?}", e);
                                }
                            }
                        }
                        _ => {}
                    }
                }
            }
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
        eprintln!("Null pointer passed to node_stop");
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      if let Some(tx) = node.command_tx.take() {
        if tx.send(Command::Stop).is_err() {
          eprintln!("Failed to send Stop command, node might already be stopped");
        }
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
        eprintln!("Null pointer passed to node_free");
        return;
      }
      unsafe {
        drop(Box::from_raw(ptr as *mut Node));
      }
    }),
    (),
  );
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_send_message(
  ptr: *mut c_void,
  peer_id_hex: *const c_char,
  data: *const u8,
  len: usize,
) -> bool {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() || peer_id_hex.is_null() || data.is_null() {
        eprintln!("Null pointers passed to send_message");
        return false;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      let hex_str = match unsafe { CStr::from_ptr(peer_id_hex) }.to_str() {
        Ok(s) => s,
        Err(e) => {
          eprintln!("Invalid UTF-8 in peer_id_hex: {:?}", e);
          return false;
        }
      };
      let peer_bytes = match hex_decode(hex_str) {
        Ok(b) => b,
        Err(_) => {
          eprintln!("Invalid hex string for peer_id_hex");
          return false;
        }
      };
      if peer_bytes.len() != 32 {
        eprintln!("peer_id_hex must be exactly 32 bytes");
        return false;
      }

      let mut arr = [0u8; 32];
      arr.copy_from_slice(&peer_bytes);
      let peer_id = match pubkey_to_peerid(&arr) {
        Some(id) => id,
        None => {
          eprintln!("Failed to parse peer ID from public key bytes");
          return false;
        }
      };
      let payload = unsafe { std::slice::from_raw_parts(data, len) }.to_vec();

      if let Some(tx) = &node.command_tx {
        if tx.send(Command::SendMessage(peer_id, payload)).is_ok() {
          return true;
        } else {
          eprintln!("Failed to send SendMessage command to node task");
        }
      } else {
        eprintln!("Node is not started");
      }
      false
    }),
    false,
  )
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_dial(ptr: *mut c_void, peer_id_hex: *const c_char) {
  run_ffi(
    AssertUnwindSafe(|| {
      if ptr.is_null() || peer_id_hex.is_null() {
        eprintln!("Null pointers passed to dial");
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      let hex_str = match unsafe { CStr::from_ptr(peer_id_hex) }.to_str() {
        Ok(s) => s,
        Err(e) => {
          eprintln!("Invalid UTF-8 in peer_id_hex: {:?}", e);
          return;
        }
      };
      let peer_bytes = match hex_decode(hex_str) {
        Ok(b) => b,
        Err(_) => {
          eprintln!("Invalid hex string for peer_id_hex");
          return;
        }
      };
      if peer_bytes.len() != 32 {
        eprintln!("peer_id_hex must be exactly 32 bytes");
        return;
      }

      let mut arr = [0u8; 32];
      arr.copy_from_slice(&peer_bytes);
      let peer_id = match pubkey_to_peerid(&arr) {
        Some(id) => id,
        None => {
          eprintln!("Failed to parse peer ID from public key bytes");
          return;
        }
      };

      if let Some(tx) = &node.command_tx {
        if let Err(e) = tx.send(Command::Dial(peer_id)) {
          eprintln!("Failed to send Dial command: {:?}", e);
        }
      } else {
        eprintln!("Node is not started");
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
        eprintln!("Null pointers passed to add_bootstrap_node");
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      let addr_str = match unsafe { CStr::from_ptr(multiaddr) }.to_str() {
        Ok(s) => s,
        Err(e) => {
          eprintln!("Invalid UTF-8 in multiaddr: {:?}", e);
          return;
        }
      };
      match Multiaddr::from_str(addr_str) {
        Ok(addr) => {
          if let Some(tx) = &node.command_tx {
            if let Err(e) = tx.send(Command::AddBootstrap(addr)) {
              eprintln!("Failed to send AddBootstrap command: {:?}", e);
            }
          } else {
            eprintln!("Node is not started");
          }
        }
        Err(e) => eprintln!("Invalid multiaddr: {:?}", e),
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
        eprintln!("Null pointers passed to listen");
        return;
      }
      let node = unsafe { &mut *(ptr as *mut Node) };
      let addr_str = match unsafe { CStr::from_ptr(multiaddr) }.to_str() {
        Ok(s) => s,
        Err(e) => {
          eprintln!("Invalid UTF-8 in multiaddr: {:?}", e);
          return;
        }
      };
      match Multiaddr::from_str(addr_str) {
        Ok(addr) => {
          if let Some(tx) = &node.command_tx {
            if let Err(e) = tx.send(Command::Listen(addr)) {
              eprintln!("Failed to send Listen command: {:?}", e);
            }
          } else {
            eprintln!("Node is not started");
          }
        }
        Err(e) => eprintln!("Invalid multiaddr: {:?}", e),
      }
    }),
    (),
  );
}

#[cfg(test)]
mod tests {
  use super::*;

  #[test]
  fn test_peerid_pubkey_conversion() {
    let keypair = identity::ed25519::Keypair::generate();
    let pubkey_bytes = keypair.public().to_bytes();
    let peer_id = identity::PublicKey::from(keypair.public()).to_peer_id();

    let restored_pubkey = peerid_to_pubkey(&peer_id).unwrap();
    assert_eq!(&restored_pubkey[..], &pubkey_bytes[..]);

    let restored_peer_id = pubkey_to_peerid(&restored_pubkey).unwrap();
    assert_eq!(peer_id, restored_peer_id);
  }

  #[test]
  fn test_hex_encode_decode() {
    let bytes = vec![0xde, 0xad, 0xbe, 0xef];
    let hex = hex_encode(&bytes);
    assert_eq!(hex, "deadbeef");
    let decoded = hex_decode(&hex).unwrap();
    assert_eq!(decoded, bytes);

    assert!(hex_decode("invalidhex").is_err());
    assert!(hex_decode("123").is_err());
  }
}
