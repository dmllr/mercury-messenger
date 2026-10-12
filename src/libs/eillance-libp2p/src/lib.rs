use futures::StreamExt;
use libp2p::{
    identity,
    kad::{store::MemoryStore, Behaviour as Kademlia, Config as KademliaConfig},
    noise, relay,
    request_response::{self, ProtocolSupport},
    swarm::{NetworkBehaviour, SwarmEvent},
    tcp, yamux, PeerId, SwarmBuilder, StreamProtocol, Multiaddr
};
use std::os::raw::{c_void, c_char};
use std::ffi::{CStr, CString};
use std::str::FromStr;
use tokio::runtime::Runtime;
use tokio::sync::{mpsc, oneshot};

#[derive(NetworkBehaviour)]
struct MercuryBehaviour {
    relay_client: relay::client::Behaviour,
    kademlia: Kademlia<MemoryStore>,
    req_resp: request_response::cbor::Behaviour<Vec<u8>, Vec<u8>>,
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

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_new(secret_key: *const u8) -> *mut c_void {
    if secret_key.is_null() {
        return std::ptr::null_mut();
    }

    let mut key_bytes = [0u8; 32];
    key_bytes.copy_from_slice(unsafe { std::slice::from_raw_parts(secret_key, 32) });

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
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_set_callbacks(
    ptr: *mut c_void,
    context: *mut c_void,
    on_connected: *const c_void,
    on_disconnected: *const c_void,
    on_message: *const c_void,
) {
    if ptr.is_null() { return; }
    let node = unsafe { &mut *(ptr as *mut Node) };
    node.callbacks.context = context as usize;
    node.callbacks.on_connected = on_connected as usize;
    node.callbacks.on_disconnected = on_disconnected as usize;
    node.callbacks.on_message = on_message as usize;
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_start(ptr: *mut c_void) {
    if ptr.is_null() {
        return;
    }
    let node = unsafe { &mut *(ptr as *mut Node) };
    if node.command_tx.is_some() { return; }

    let (tx, mut rx) = mpsc::unbounded_channel();
    node.command_tx = Some(tx);
    let local_key = node.keypair.clone();
    let callbacks = node.callbacks.clone();

    node.runtime.spawn(async move {
        let mut swarm = SwarmBuilder::with_existing_identity(local_key)
            .with_tokio()
            .with_tcp(tcp::Config::default(), noise::Config::new, yamux::Config::default)
            .expect("Failed to configure TCP")
            .with_relay_client(noise::Config::new, yamux::Config::default)
            .expect("Failed to configure Relay Client")
            .with_behaviour(|key, relay_client| {
                let local_peer_id = PeerId::from(key.public());
                let store = MemoryStore::new(local_peer_id);
                let kademlia = Kademlia::with_config(local_peer_id, store, KademliaConfig::default());
                let req_resp = request_response::cbor::Behaviour::<Vec<u8>, Vec<u8>>::new(
                    [(StreamProtocol::new("/mercury/data/1.0.0"), ProtocolSupport::Full)],
                    request_response::Config::default(),
                );
                MercuryBehaviour { relay_client, kademlia, req_resp }
            })
            .expect("Failed to create behaviour")
            .with_swarm_config(|c| c.with_idle_connection_timeout(std::time::Duration::from_secs(60)))
            .build();

        let _ = swarm.listen_on("/ip4/0.0.0.0/tcp/0".parse().unwrap());

        loop {
            tokio::select! {
                cmd = rx.recv() => match cmd {
                    Some(Command::Stop) | None => break,
                    Some(Command::Dial(peer_id)) => {
                        let _ = swarm.dial(peer_id);
                    }
                    Some(Command::AddBootstrap(addr)) => {
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
                    SwarmEvent::NewListenAddr { address, .. } => {
                        println!("[libp2p] Local node is listening on {:?}", address);
                    }
                    SwarmEvent::ConnectionEstablished { peer_id, .. } => {
                        if callbacks.on_connected != 0 {
                            let cb: extern "C" fn(*mut c_void, *const c_char) = unsafe { std::mem::transmute(callbacks.on_connected) };
                            let hex = hex_encode(&peer_id.to_bytes());
                            if let Ok(c_str) = CString::new(hex) {
                                cb(callbacks.context as *mut c_void, c_str.as_ptr());
                            }
                        }
                    }
                    SwarmEvent::ConnectionClosed { peer_id, .. } => {
                        if callbacks.on_disconnected != 0 {
                            let cb: extern "C" fn(*mut c_void, *const c_char) = unsafe { std::mem::transmute(callbacks.on_disconnected) };
                            let hex = hex_encode(&peer_id.to_bytes());
                            if let Ok(c_str) = CString::new(hex) {
                                cb(callbacks.context as *mut c_void, c_str.as_ptr());
                            }
                        }
                    }
                    SwarmEvent::Behaviour(MercuryBehaviourEvent::ReqResp(request_response::Event::Message { peer, message })) => {
                        match message {
                            request_response::Message::Request { request, .. } => {
                                if callbacks.on_message != 0 {
                                    let cb: extern "C" fn(*mut c_void, *const c_char, *const u8, usize) = unsafe { std::mem::transmute(callbacks.on_message) };
                                    let hex = hex_encode(&peer.to_bytes());
                                    if let Ok(c_str) = CString::new(hex) {
                                        cb(callbacks.context as *mut c_void, c_str.as_ptr(), request.as_ptr(), request.len());
                                    }
                                }
                            }
                            request_response::Message::Response { .. } => {}
                        }
                    }
                    _ => {}
                }
            }
        }
    });
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_stop(ptr: *mut c_void) {
    if ptr.is_null() {
        return;
    }
    let node = unsafe { &mut *(ptr as *mut Node) };
    if let Some(tx) = node.command_tx.take() {
        let _ = tx.send(Command::Stop);
    }
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_listen(ptr: *mut c_void, multiaddr: *const c_char) {
    if ptr.is_null() || multiaddr.is_null() { return; }
    let node = unsafe { &mut *(ptr as *mut Node) };
    
    let c_str = unsafe { CStr::from_ptr(multiaddr) };
    let addr_str = match c_str.to_str() { Ok(s) => s, Err(_) => return };
    let addr = match Multiaddr::from_str(addr_str) { Ok(a) => a, Err(_) => return };

    if let Some(tx) = &node.command_tx {
        let _ = tx.send(Command::Listen(addr));
    }
}
#[no_mangle]
pub extern "C" fn eillance_libp2p_node_free(ptr: *mut c_void) {
    if ptr.is_null() {
        return;
    }
    unsafe { let _ = Box::from_raw(ptr as *mut Node); }
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_send_message(
    ptr: *mut c_void,
    peer_id_hex: *const c_char,
    data: *const u8,
    len: usize,
) -> bool {
    if ptr.is_null() || peer_id_hex.is_null() || data.is_null() { return false; }
    let node = unsafe { &mut *(ptr as *mut Node) };
    
    let c_str = unsafe { CStr::from_ptr(peer_id_hex) };
    let hex_str = match c_str.to_str() {
        Ok(s) => s,
        Err(_) => return false,
    };
    let peer_bytes = match hex_decode(hex_str) {
        Ok(b) => b,
        Err(_) => return false,
    };
    let peer_id = match PeerId::from_bytes(&peer_bytes) {
        Ok(p) => p,
        Err(_) => return false,
    };

    let payload = unsafe { std::slice::from_raw_parts(data, len) }.to_vec();

    if let Some(tx) = &node.command_tx {
        let (resp_tx, resp_rx) = oneshot::channel();
        if tx.send(Command::SendMessage(peer_id, payload, resp_tx)).is_ok() {
            return node.runtime.block_on(async { resp_rx.await.unwrap_or(false) });
        }
    }
    false
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_dial(ptr: *mut c_void, peer_id_hex: *const c_char) {
    if ptr.is_null() || peer_id_hex.is_null() { return; }
    let node = unsafe { &mut *(ptr as *mut Node) };
    
    let c_str = unsafe { CStr::from_ptr(peer_id_hex) };
    let hex_str = match c_str.to_str() { Ok(s) => s, Err(_) => return };
    let peer_bytes = match hex_decode(hex_str) { Ok(b) => b, Err(_) => return };
    let peer_id = match PeerId::from_bytes(&peer_bytes) { Ok(p) => p, Err(_) => return };

    if let Some(tx) = &node.command_tx {
        let _ = tx.send(Command::Dial(peer_id));
    }
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_add_bootstrap_node(ptr: *mut c_void, multiaddr: *const c_char) {
    if ptr.is_null() || multiaddr.is_null() { return; }
    let node = unsafe { &mut *(ptr as *mut Node) };
    
    let c_str = unsafe { CStr::from_ptr(multiaddr) };
    let addr_str = match c_str.to_str() { Ok(s) => s, Err(_) => return };
    let addr = match Multiaddr::from_str(addr_str) { Ok(a) => a, Err(_) => return };

    if let Some(tx) = &node.command_tx {
        let _ = tx.send(Command::AddBootstrap(addr));
    }
}
