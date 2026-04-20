use futures::StreamExt;
use libp2p::{
    identity,
    kad::{store::MemoryStore, Behaviour as Kademlia, Config as KademliaConfig},
    noise, relay,
    swarm::{NetworkBehaviour, SwarmEvent},
    tcp, yamux, PeerId, SwarmBuilder,
};
use std::os::raw::c_void;
use tokio::runtime::Runtime;
use tokio::sync::oneshot;

#[derive(NetworkBehaviour)]
struct MercuryBehaviour {
    relay_client: relay::client::Behaviour,
    kademlia: Kademlia<MemoryStore>,
}

struct Node {
    runtime: Runtime,
    shutdown_tx: Option<oneshot::Sender<()>>,
    keypair: identity::Keypair,
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
        shutdown_tx: None,
        keypair,
    });
    Box::into_raw(node) as *mut c_void
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_start(ptr: *mut c_void) {
    if ptr.is_null() {
        return;
    }
    let node = unsafe { &mut *(ptr as *mut Node) };

    let (tx, mut rx) = oneshot::channel();
    node.shutdown_tx = Some(tx);
    let local_key = node.keypair.clone();

    node.runtime.spawn(async move {
        let local_peer_id = PeerId::from(local_key.public());

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
                MercuryBehaviour { relay_client, kademlia }
            })
            .expect("Failed to create behaviour")
            .with_swarm_config(|c| c.with_idle_connection_timeout(std::time::Duration::from_secs(60)))
            .build();

        let _ = swarm.listen_on("/ip4/0.0.0.0/tcp/0".parse().unwrap());

        loop {
            tokio::select! {
                _ = &mut rx => break,
                event = swarm.select_next_some() => match event {
                    SwarmEvent::NewListenAddr { address, .. } => {
                        println!("[libp2p] Local node is listening on {:?}", address);
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
    if let Some(tx) = node.shutdown_tx.take() {
        let _ = tx.send(());
    }
}

#[no_mangle]
pub extern "C" fn eillance_libp2p_node_free(ptr: *mut c_void) {
    if ptr.is_null() {
        return;
    }
    unsafe {
        // Dropping the Box reclaims the memory and invokes the tokio Runtime destructor
        let _ = Box::from_raw(ptr as *mut Node);
    }
}
