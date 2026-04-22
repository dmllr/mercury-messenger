use futures::StreamExt;
use libp2p::{
    identity,
    kad::{self, store::MemoryStore, Behaviour as Kademlia, Config as KademliaConfig},
    noise, relay, mdns,
    request_response::{self, ProtocolSupport},
    swarm::{NetworkBehaviour, SwarmEvent},
    tcp, yamux, PeerId, SwarmBuilder, StreamProtocol, Multiaddr,
    identify,
};
use std::os::raw::{c_void, c_char};
use std::ffi::{CStr, CString};
use std::str::FromStr;
use std::time::Duration;
use std::collections::HashSet;
use std::panic::catch_unwind;
use tokio::runtime::Runtime;
use tokio::sync::{mpsc, oneshot};

#[derive(NetworkBehaviour)]
struct MercuryBehaviour {
    relay_client: relay::client::Behaviour,
    kademlia: Kademlia<MemoryStore>,
    req_resp: request_response::cbor::Behaviour<Vec<u8>, Vec<u8>>,
    mdns: mdns::tokio::Behaviour,
    identify: identify::Behaviour,
}

#[derive(Clone, Default)]
struct Callbacks {
    context: usize,
    on_connected: usize,
    on_disconnected: usize,
    on_message: usize,
}
unsafe impl Send for Callbacks {}
unsafe impl Sync for Callbacks {}

enum Command {
    Dial(PeerId),
    AddBootstrap(Multiaddr),
    Listen(Multiaddr),
    SendMessage(PeerId, Vec<u8>, oneshot::Sender<bool>),
    Stop,
}

struct Node {
    runtime: Runtime,
    command_tx: Option<mpsc::UnboundedSender<Command>>,
    keypair: identity::Keypair,
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
    if s.len() % 2 != 0 { return Err(()); }
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
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_new(secret_key: *const u8) -> *mut c_void {
    catch_unwind(|| {
        if secret_key.is_null() { return std::ptr::null_mut(); }
        let mut key_bytes = [0u8; 32];
        unsafe { std::ptr::copy_nonoverlapping(secret_key, key_bytes.as_mut_ptr(), 32) };

        let secret = match identity::ed25519::SecretKey::try_from_bytes(key_bytes) {
            Ok(s) => s,
            Err(_) => return std::ptr::null_mut(),
        };
        let keypair = identity::Keypair::from(identity::ed25519::Keypair::from(secret));

        let rt = match Runtime::new() {
            Ok(rt) => rt,
            Err(_) => return std::ptr::null_mut(),
        };
        
        let node = Box::new(Node {
            runtime: rt,
            command_tx: None,
            keypair,
            callbacks: Callbacks::default(),
        });
        Box::into_raw(node) as *mut c_void
    }).unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_set_callbacks(
    ptr: *mut c_void,
    context: *mut c_void,
    on_connected: *const c_void,
    on_disconnected: *const c_void,
    on_message: *const c_void,
) {
    let _ = catch_unwind(|| {
        if ptr.is_null() { return; }
        let node = unsafe { &mut *(ptr as *mut Node) };
        node.callbacks = Callbacks {
            context: context as usize,
            on_connected: on_connected as usize,
            on_disconnected: on_disconnected as usize,
            on_message: on_message as usize,
        };
    });
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_start(ptr: *mut c_void) {
    let _ = catch_unwind(|| {
        if ptr.is_null() { return; }
        let node = unsafe { &mut *(ptr as *mut Node) };
        if node.command_tx.is_some() { return; }

        let (tx, mut rx) = mpsc::unbounded_channel();
        node.command_tx = Some(tx);
        let local_key = node.keypair.clone();
        let callbacks = node.callbacks.clone();

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
                            kademlia.set_mode(Some(kad::Mode::Server));

                            let mdns = mdns::tokio::Behaviour::new(mdns::Config::default(), peer_id).expect("mDNS valid");
                            let req_resp = request_response::cbor::Behaviour::new([(StreamProtocol::new("/mercury/1.0.0"), ProtocolSupport::Full)],
                                                                                  request_response::Config::default(),
                            );
                            let identify = identify::Behaviour::new(identify::Config::new(
                                "/mercury/id/1.0.0".into(),
                                key.public(),
                            ));

                            MercuryBehaviour { relay_client, kademlia, req_resp, mdns, identify }
                        }) {
                            Ok(b) => b.with_swarm_config(|c| c.with_idle_connection_timeout(Duration::from_secs(60))).build(),
                            Err(_) => return,
                        },
                        Err(_) => return,
                    },
                    Err(_) => return,
            };

            let mut pending_dials = HashSet::new();

            loop {
                tokio::select! {
                    cmd = rx.recv() => match cmd {
                        Some(Command::Stop) | None => break,
                        Some(Command::Dial(peer_id)) => {
                            match swarm.dial(peer_id) {
                                Ok(_) => {}
                                Err(libp2p::swarm::DialError::NoAddresses) => {
                                    swarm.behaviour_mut().kademlia.get_closest_peers(peer_id);
                                    pending_dials.insert(peer_id);
                                }
                                Err(libp2p::swarm::DialError::DialPeerConditionFalse(_)) => {
                                    pending_dials.remove(&peer_id);
                                    notify_connected(&callbacks, &peer_id);
                                }
                                Err(_) => {
                                    pending_dials.remove(&peer_id);
                                    notify_disconnected(&callbacks, &swarm, &peer_id);
                                }
                            }
                        }
                        Some(Command::AddBootstrap(addr)) => {
                            let mut p2p_peer_id = None;
                            for protocol in addr.iter() {
                                if let libp2p::multiaddr::Protocol::P2p(peer_id) = protocol {
                                    p2p_peer_id = Some(peer_id);
                                }
                            }
                            if let Some(peer_id) = p2p_peer_id {
                                swarm.behaviour_mut().kademlia.add_address(&peer_id, addr.clone());
                            }
                            let _ = swarm.dial(addr);
                        }
                        Some(Command::Listen(addr)) => {
                            let _ = swarm.listen_on(addr);
                        }
                        Some(Command::SendMessage(peer_id, data, resp)) => {
                            swarm.behaviour_mut().req_resp.send_request(&peer_id, data);
                            let _ = resp.send(true);
                        }
                    },
                    event = swarm.select_next_some() => match event {
                        SwarmEvent::ConnectionEstablished { peer_id, .. } => {
                            pending_dials.remove(&peer_id);
                            notify_connected(&callbacks, &peer_id);
                        }
                        SwarmEvent::ConnectionClosed { peer_id, .. } => {
                            notify_disconnected(&callbacks, &swarm, &peer_id);
                        }
                        SwarmEvent::OutgoingConnectionError { peer_id: Some(peer_id), .. } => {
                            pending_dials.remove(&peer_id);
                            notify_disconnected(&callbacks, &swarm, &peer_id);
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::Identify(identify::Event::Received { peer_id, info })) => {
                            for addr in info.listen_addrs {
                                swarm.behaviour_mut().kademlia.add_address(&peer_id, addr);
                            }
                            let _ = swarm.behaviour_mut().kademlia.bootstrap();
                            if pending_dials.contains(&peer_id) {
                                let _ = swarm.dial(peer_id);
                            }
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::Mdns(mdns::Event::Discovered(list))) => {
                            for (peer_id, addr) in list {
                                swarm.behaviour_mut().kademlia.add_address(&peer_id, addr);
                                if pending_dials.contains(&peer_id) {
                                    let _ = swarm.dial(peer_id);
                                }
                            }
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(kad::Event::OutboundQueryProgressed {
                            result: kad::QueryResult::GetClosestPeers(Ok(res)),
                            ..
                        })) => {
                            for peer in res.peers {
                                if pending_dials.contains(&peer) {
                                    let _ = swarm.dial(peer);
                                }
                            }
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(kad::Event::RoutingUpdated { peer, .. })) => {
                            if pending_dials.contains(&peer) {
                                let _ = swarm.dial(peer);
                            }
                        }
                        SwarmEvent::Behaviour(MercuryBehaviourEvent::ReqResp(request_response::Event::Message { peer, message })) => {
                            if let request_response::Message::Request { request, channel, .. } = message {
                                if callbacks.on_message != 0 {
                                    let cb: extern "C" fn(*mut c_void, *const c_char, *const u8, usize) = unsafe { std::mem::transmute(callbacks.on_message) };
                                    if let Some(pk) = peerid_to_pubkey(&peer) {
                                        if let Ok(c_str) = CString::new(hex_encode(&pk)) {
                                            cb(callbacks.context as *mut c_void, c_str.as_ptr(), request.as_ptr(), request.len());
                                        }
                                    }
                                }
                                let _ = swarm.behaviour_mut().req_resp.send_response(channel, vec![]);
                            }
                        }
                        _ => {}
                    }
                }
            }
        });
    });
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_stop(ptr: *mut c_void) {
    let _ = catch_unwind(|| {
        if ptr.is_null() { return; }
        let node = unsafe { &mut *(ptr as *mut Node) };
        if let Some(tx) = node.command_tx.take() {
            let _ = tx.send(Command::Stop);
        }
    });
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_free(ptr: *mut c_void) {
    let _ = catch_unwind(|| {
        if ptr.is_null() { return; }
        unsafe { let _ = Box::from_raw(ptr as *mut Node); }
    });
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_send_message(
    ptr: *mut c_void,
    peer_id_hex: *const c_char,
    data: *const u8,
    len: usize,
) -> bool {
    catch_unwind(|| {
        if ptr.is_null() || peer_id_hex.is_null() || data.is_null() { return false; }
        let node = unsafe { &mut *(ptr as *mut Node) };
        let hex_str = unsafe { CStr::from_ptr(peer_id_hex) }.to_str().unwrap_or("");
        let peer_bytes = hex_decode(hex_str).unwrap_or_default();
        if peer_bytes.len() != 32 { return false; }

        let mut arr = [0u8; 32];
        arr.copy_from_slice(&peer_bytes);
        let peer_id = match pubkey_to_peerid(&arr) {
            Some(id) => id,
            None => return false,
        };
        let payload = unsafe { std::slice::from_raw_parts(data, len) }.to_vec();

        if let Some(tx) = &node.command_tx {
            let (resp_tx, resp_rx) = oneshot::channel();
            if tx.send(Command::SendMessage(peer_id, payload, resp_tx)).is_ok() {
                return node.runtime.block_on(async { resp_rx.await.unwrap_or(false) });
            }
        }
        false
    }).unwrap_or(false)
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_dial(ptr: *mut c_void, peer_id_hex: *const c_char) {
    let _ = catch_unwind(|| {
        if ptr.is_null() || peer_id_hex.is_null() { return; }
        let node = unsafe { &mut *(ptr as *mut Node) };
        let hex_str = unsafe { CStr::from_ptr(peer_id_hex) }.to_str().unwrap_or("");
        let peer_bytes = hex_decode(hex_str).unwrap_or_default();
        if peer_bytes.len() != 32 { return; }

        let mut arr = [0u8; 32];
        arr.copy_from_slice(&peer_bytes);
        let peer_id = match pubkey_to_peerid(&arr) {
            Some(id) => id,
            None => return,
        };

        if let Some(tx) = &node.command_tx {
            let _ = tx.send(Command::Dial(peer_id));
        }
    });
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_add_bootstrap_node(ptr: *mut c_void, multiaddr: *const c_char) {
    let _ = catch_unwind(|| {
        if ptr.is_null() || multiaddr.is_null() { return; }
        let node = unsafe { &mut *(ptr as *mut Node) };
        let addr_str = unsafe { CStr::from_ptr(multiaddr) }.to_str().unwrap_or("");
        if let Ok(addr) = Multiaddr::from_str(addr_str) {
            if let Some(tx) = &node.command_tx {
                let _ = tx.send(Command::AddBootstrap(addr));
            }
        }
    });
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_listen(ptr: *mut c_void, multiaddr: *const c_char) {
    let _ = catch_unwind(|| {
        if ptr.is_null() || multiaddr.is_null() { return; }
        let node = unsafe { &mut *(ptr as *mut Node) };
        let addr_str = unsafe { CStr::from_ptr(multiaddr) }.to_str().unwrap_or("");
        if let Ok(addr) = Multiaddr::from_str(addr_str) {
            if let Some(tx) = &node.command_tx {
                let _ = tx.send(Command::Listen(addr));
            }
        }
    });
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
