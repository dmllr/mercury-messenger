use crate::behaviour::MercuryBehaviour;
use crate::types::{Callbacks, Command, NodeRole};
use crate::utils::resolve_multiaddr;
use futures::StreamExt;
use libp2p::{Multiaddr, PeerId, Swarm};
use std::collections::{HashMap, HashSet};
use std::time::Duration;
use tokio::sync::mpsc;

pub(crate) fn is_bootstrap_direct_ip(addr: &Multiaddr, bootstrap_addrs: &HashSet<Multiaddr>) -> bool {
  let has_circuit = addr
    .iter()
    .any(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit));
  if has_circuit {
    return false;
  }
  let a_ip = addr.iter().next();
  if a_ip.is_none() {
    return false;
  }
  for b_addr in bootstrap_addrs {
    if a_ip == b_addr.iter().next() {
      return true;
    }
  }
  false
}

pub struct EventLoopState {
  pub pending_dials: HashMap<PeerId, std::time::Instant>,
  pub known_addresses: HashMap<PeerId, HashSet<Multiaddr>>,
  pub explicit_bootstrap_peers: HashSet<PeerId>,
  pub explicit_bootstrap_urls: HashSet<Multiaddr>,
  pub active_relay_peers: HashMap<PeerId, Multiaddr>,
  pub bootstrap_addrs: HashSet<Multiaddr>,
  pub active_relay_listeners: HashMap<Multiaddr, libp2p::core::transport::ListenerId>,
  pub turn_auth: std::sync::Arc<crate::turn_server::TurnAuthenticator>,
  pub turn_port: Option<u16>,
  pub public_ip: Option<String>,
  pub public_port: Option<u16>,
  pub is_turn_eligible: bool,
  pub is_public: bool,
  pub turn_server_handle: Option<crate::turn_server::TurnServerHandle>,
  pub tick_counter: u64,
  pub has_bootstrapped: bool,
  pub providing_keys: HashSet<Vec<u8>>,
}

impl EventLoopState {
  pub fn new(role: NodeRole, callbacks: crate::types::Callbacks) -> Self {
    Self {
      pending_dials: HashMap::new(),
      known_addresses: HashMap::new(),
      explicit_bootstrap_peers: HashSet::new(),
      explicit_bootstrap_urls: HashSet::new(),
      active_relay_peers: HashMap::new(),
      bootstrap_addrs: HashSet::new(),
      active_relay_listeners: HashMap::new(),
      turn_auth: std::sync::Arc::new(crate::turn_server::TurnAuthenticator::new(callbacks)),
      turn_port: None,
      public_ip: None,
      public_port: None,
      is_turn_eligible: matches!(role, NodeRole::BootstrapNode),
      is_public: false,
      turn_server_handle: None,
      tick_counter: 0,
      has_bootstrapped: false,
      providing_keys: HashSet::new(),
    }
  }

  pub fn do_dial(&mut self, peer_val: PeerId, swarm: &mut Swarm<MercuryBehaviour>, callbacks: &Callbacks) {
    if !self.pending_dials.contains_key(&peer_val) {
      crate::node_log!(callbacks, 5, "Initiating DHT get_closest_peers for {}", peer_val);
      swarm.behaviour_mut().kademlia.get_closest_peers(peer_val);
      self.pending_dials.insert(peer_val, std::time::Instant::now());
    }

    let mut explicit_addrs = Vec::new();

    if !swarm.is_connected(&peer_val) {
      if let Some(addrs) = self.known_addresses.get(&peer_val) {
        for mut addr in addrs.clone() {
          let has_target_p2p = addr.iter().any(|p| {
            if let libp2p::multiaddr::Protocol::P2p(id) = p {
              id == peer_val
            } else {
              false
            }
          });
          if !has_target_p2p {
            addr.push(libp2p::multiaddr::Protocol::P2p(peer_val));
          }
          explicit_addrs.push(addr);
        }
      }

      for (_, relay_addr) in &self.active_relay_peers {
        let mut relayed = relay_addr.clone();
        relayed.push(libp2p::multiaddr::Protocol::P2pCircuit);
        relayed.push(libp2p::multiaddr::Protocol::P2p(peer_val));
        explicit_addrs.push(relayed);
      }

      for b_addr in &self.bootstrap_addrs {
        if b_addr.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::P2p(_))) {
          let mut relayed = b_addr.clone();
          relayed.push(libp2p::multiaddr::Protocol::P2pCircuit);
          relayed.push(libp2p::multiaddr::Protocol::P2p(peer_val));
          explicit_addrs.push(relayed);
        }
      }

      explicit_addrs.sort();
      explicit_addrs.dedup();
    }

    let opts = libp2p::swarm::dial_opts::DialOpts::peer_id(peer_val)
      .addresses(explicit_addrs)
      .condition(libp2p::swarm::dial_opts::PeerCondition::Disconnected)
      .build();

    match swarm.dial(opts) {
      Ok(_) => crate::node_log!(callbacks, 5, "Dial request submitted for {}", peer_val),
      Err(libp2p::swarm::DialError::NoAddresses) => {
        crate::node_log!(callbacks, 3, "DialError::NoAddresses for {}", peer_val);
      }
      Err(libp2p::swarm::DialError::DialPeerConditionFalse(_)) => {
        crate::node_log!(callbacks, 5, "DialError::DialPeerConditionFalse for {}", peer_val);
        if swarm.is_connected(&peer_val) {
          self.pending_dials.remove(&peer_val);
        }
      }
      Err(e) => {
        crate::node_log!(callbacks, 2, "Failed to dial peer {}: {:?}", peer_val, e);
      }
    }
  }
}

pub async fn run_event_loop(
  mut swarm: Swarm<MercuryBehaviour>,
  mut rx: mpsc::UnboundedReceiver<Command>,
  tx: mpsc::UnboundedSender<Command>,
  callbacks: Callbacks,
  role: NodeRole,
) {
  let mut state = EventLoopState::new(role, callbacks.clone());
  let mut keepalive_interval = tokio::time::interval(Duration::from_secs(60));
  let mut stats_interval = tokio::time::interval(Duration::from_secs(15));

  loop {
    tokio::select! {
        _ = keepalive_interval.tick() => {
            handle_keepalive(&mut state, &mut swarm, &callbacks);
        }
        _ = stats_interval.tick() => {
            let now = std::time::Instant::now();
            state.pending_dials.retain(|_, time| now.duration_since(*time).as_secs() < 120);

            if state.known_addresses.len() > 2000 {
                let mut to_remove = Vec::new();
                for (peer, _) in state.known_addresses.iter() {
                    if !state.explicit_bootstrap_peers.contains(peer) && !swarm.is_connected(peer) {
                        to_remove.push(*peer);
                    }
                    if to_remove.len() >= 500 {
                        break;
                    }
                }
                for peer in to_remove {
                    state.known_addresses.remove(&peer);
                }
            }

            let connected_peers = swarm.network_info().connection_counters().num_established();
            let mut bootstrap_connected = 0;
            for b in &state.explicit_bootstrap_peers {
                if swarm.is_connected(b) {
                    bootstrap_connected += 1;
                }
            }

            let mut role_str = "simple_node";
            if state.turn_port.is_some() {
                role_str = "turn_server";
            } else if !state.active_relay_listeners.is_empty() {
                role_str = "relayed_client";
            }
            let mut dht_peers = 0;
            for bucket in swarm.behaviour_mut().kademlia.kbuckets() {
                dht_peers += bucket.num_entries();
            }
            state.tick_counter = state.tick_counter.wrapping_add(1);

            if state.tick_counter % 240 == 0 || (bootstrap_connected < state.explicit_bootstrap_urls.len() && state.tick_counter % 4 == 0) {
                for addr in &state.explicit_bootstrap_urls {
                    let tx_clone = tx.clone();
                    let addr_clone = addr.clone();
                    tokio::spawn(async move {
                        if let Some(resolved) = resolve_multiaddr(addr_clone).await {
                            let _ = tx_clone.send(Command::AddBootstrapResolved(resolved));
                        }
                    });
                }
            }

            if state.tick_counter % 20 == 0 && state.has_bootstrapped {
                let _ = swarm.behaviour_mut().kademlia.bootstrap();
            }

            if state.tick_counter % 240 == 0 && state.has_bootstrapped {
                for key_bytes in &state.providing_keys {
                    let key = libp2p::kad::RecordKey::new(key_bytes);
                    let _ = swarm.behaviour_mut().kademlia.start_providing(key);
                }
            }
            crate::utils::notify_network_stats(&callbacks, connected_peers as i32, bootstrap_connected as i32, dht_peers as i32, role_str);
        }
        cmd = rx.recv() => {
            match cmd {
                Some(Command::Stop) | None => {
                    if let Some(handle) = state.turn_server_handle.take() {
                        let _ = handle.close_tx.send(());
                        crate::node_log!(callbacks, 3, "Node switches off a TURN server");
                    }
                    break;
                }
                Some(c) => handle_command(c, &mut state, &mut swarm, &tx, &callbacks),
            }
        }
        event = swarm.select_next_some() => {
            crate::swarm_events::handle_swarm_event(event, &mut state, &mut swarm, &tx, &callbacks, role);
        }
    }
  }
}

fn handle_keepalive(state: &mut EventLoopState, swarm: &mut Swarm<MercuryBehaviour>, callbacks: &Callbacks) {
  for b_addr in &state.bootstrap_addrs {
    let mut p2p_peer_id = None;
    for protocol in b_addr.iter() {
      if let libp2p::multiaddr::Protocol::P2p(peer_id) = protocol {
        p2p_peer_id = Some(peer_id);
      }
    }
    let should_dial = match p2p_peer_id {
      Some(peer_id) => !swarm.is_connected(&peer_id),
      None => true, // Missing PeerId suffix; dial anyway to attempt connection
    };
    if should_dial {
      crate::node_log!(
        callbacks,
        4,
        "Periodic check: Bootstrap node {:?} not connected, dialing...",
        b_addr
      );
      if let Err(e) = swarm.dial(b_addr.clone()) {
        crate::node_log!(callbacks, 2, "Failed to dial bootstrap node {:?}: {:?}", b_addr, e);
      }
    }
  }

  let mut to_listen = Vec::new();
  for (relay_peer_id, relay_addr) in &state.active_relay_peers {
    if swarm.is_connected(relay_peer_id) {
      let mut relay_listen_addr = relay_addr.clone();
      relay_listen_addr.push(libp2p::multiaddr::Protocol::P2pCircuit);
      if !state.active_relay_listeners.contains_key(&relay_listen_addr) {
        to_listen.push(relay_listen_addr);
      }
    }
  }

  for relay_listen_addr in to_listen {
    crate::node_log!(
      callbacks,
      4,
      "Periodic check: Re-establishing relay listener on {}",
      relay_listen_addr
    );
    match swarm.listen_on(relay_listen_addr.clone()) {
      Ok(id) => {
        state.active_relay_listeners.insert(relay_listen_addr, id);
      }
      Err(e) => {
        crate::node_log!(callbacks, 2, "Failed to re-listen on relay: {:?}", e);
      }
    }
  }
}

pub(crate) fn try_start_turn_server(
  state: &mut EventLoopState,
  tx: &mpsc::UnboundedSender<Command>,
  callbacks: &Callbacks,
) {
  if state.is_turn_eligible && state.is_public && state.turn_port.is_none() {
    if let Some(public_ip) = state.public_ip.clone() {
      let turn_auth = state.turn_auth.clone();
      let tx_clone = tx.clone();
      let callbacks_clone = callbacks.clone();
      let requested_port = state.public_port.unwrap_or(0);

      tokio::spawn(async move {
        match crate::turn_server::start_turn_server(public_ip.clone(), requested_port, turn_auth).await {
          Ok((port, handle)) => {
            let _ = tx_clone.send(Command::TurnServerStarted {
              port,
              public_ip,
              handle,
            });
          }
          Err(e) => {
            crate::node_log!(&callbacks_clone, 2, "Failed to start embedded TURN server: {}", e);
          }
        }
      });
    }
  }
}

fn handle_command(
  cmd: Command,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  tx: &mpsc::UnboundedSender<Command>,
  callbacks: &Callbacks,
) {
  match cmd {
    Command::Dial(peer_id) => {
      crate::node_log!(callbacks, 5, "Dial command received for {}", peer_id);
      state.do_dial(peer_id, swarm, callbacks);
    }
    Command::AddBootstrap(addr) => {
      state.explicit_bootstrap_urls.insert(addr.clone());
      let tx_clone = tx.clone();
      tokio::spawn(async move {
        if let Some(resolved) = resolve_multiaddr(addr.clone()).await {
          let _ = tx_clone.send(Command::AddBootstrapResolved(resolved));
        }
      });
    }
    Command::AddBootstrapResolved(addr) => {
      let mut is_already_known = false;
      let addr_without_p2p: Vec<_> = addr
        .iter()
        .filter(|p| !matches!(p, libp2p::multiaddr::Protocol::P2p(_)))
        .collect();
      for b in &state.bootstrap_addrs {
        let b_without_p2p: Vec<_> = b
          .iter()
          .filter(|p| !matches!(p, libp2p::multiaddr::Protocol::P2p(_)))
          .collect();
        if b_without_p2p == addr_without_p2p {
          is_already_known = true;
          break;
        }
      }

      if !is_already_known {
        state.bootstrap_addrs.insert(addr.clone());
        let mut p2p_peer_id = None;
        for protocol in addr.iter() {
          if let libp2p::multiaddr::Protocol::P2p(peer_id) = protocol {
            p2p_peer_id = Some(peer_id);
          }
        }
        if let Some(peer_id) = p2p_peer_id {
          state.explicit_bootstrap_peers.insert(peer_id);
          state.known_addresses.entry(peer_id).or_default().insert(addr.clone());
          swarm.behaviour_mut().kademlia.add_address(&peer_id, addr.clone());
        }
        if let Err(e) = swarm.dial(addr.clone()) {
          crate::node_log!(callbacks, 1, "Failed to dial bootstrap node {}: {:?}", addr, e);
        }
      }
    }
    Command::Listen(addr) => {
      if !state.active_relay_listeners.contains_key(&addr) {
        match swarm.listen_on(addr.clone()) {
          Ok(id) => {
            state.active_relay_listeners.insert(addr.clone(), id);
            crate::node_log!(callbacks, 3, "Listening on relay: {}", addr);
          }
          Err(e) => {
            crate::node_log!(callbacks, 1, "Failed to listen on {}: {:?}", addr, e);
          }
        }
      }
    }
    Command::SendChat(peer_id, data) => {
      swarm.behaviour_mut().chat.send_request(&peer_id, data);
    }
    Command::SendFile(peer_id, data) => {
      swarm.behaviour_mut().file.send_request(&peer_id, data);
    }
    Command::Disconnect(peer_id) => {
      if state.explicit_bootstrap_peers.contains(&peer_id) {
        crate::node_log!(
          callbacks,
          5,
          "Ignoring disconnect command for bootstrap node {}",
          peer_id
        );
        return;
      }
      crate::node_log!(callbacks, 5, "Disconnect command received for {}", peer_id);
      let _ = swarm.disconnect_peer_id(peer_id);
      state.pending_dials.remove(&peer_id);
    }
    Command::Reconnect => {
      crate::node_log!(callbacks, 3, "Reconnect command received. Disconnecting all peers.");
      let peers: Vec<PeerId> = swarm.connected_peers().copied().collect();
      for peer in peers {
        let _ = swarm.disconnect_peer_id(peer);
      }
      state.pending_dials.clear();
      state.active_relay_listeners.clear();
      state.active_relay_peers.clear();
      state.bootstrap_addrs.clear();
      state.explicit_bootstrap_peers.clear();
      state.explicit_bootstrap_urls.clear();
      state.known_addresses.clear();
      state.public_ip = None;
      state.public_port = None;
      state.is_public = false;
      state.has_bootstrapped = false;
      if let Some(handle) = state.turn_server_handle.take() {
        let _ = handle.close_tx.send(());
        crate::node_log!(callbacks, 3, "Node switches off a TURN server (network change)");
      }
      let key_bytes = b"mercury/turn_relays".to_vec();
      state.providing_keys.remove(&key_bytes);
      state.turn_port = None;
      swarm
        .behaviour_mut()
        .kademlia
        .stop_providing(&libp2p::kad::RecordKey::new(&"mercury/turn_relays"));
    }
    Command::TurnServerStarted {
      port,
      public_ip,
      handle,
    } => {
      state.turn_port = Some(port);
      state.public_ip = Some(public_ip.clone());
      state.turn_server_handle = Some(handle);

      crate::node_log!(callbacks, 3, "Node starts a TURN server on {}:{}", public_ip, port);

      let record_key = libp2p::kad::RecordKey::new(&"mercury/turn_relays");
      state.providing_keys.insert(b"mercury/turn_relays".to_vec());

      if let Err(e) = swarm.behaviour_mut().kademlia.start_providing(record_key) {
        crate::node_log!(callbacks, 2, "Failed to start providing TURN relay record: {:?}", e);
      } else {
        crate::node_log!(callbacks, 3, "Announcing as TURN relay to DHT on port {}", port);
      }
    }
    Command::DiscoverTurnRelays => {
      let key = libp2p::kad::RecordKey::new(&"mercury/turn_relays");
      swarm.behaviour_mut().kademlia.get_providers(key);
      crate::node_log!(callbacks, 5, "Discovering TURN relays...");
    }
    Command::AuthTurnRelay(peer_id) => {
      let req = crate::types::TurnAuthRequest { dummy: 0 };
      let _ = swarm.behaviour_mut().turn_auth.send_request(&peer_id, req);
      crate::node_log!(callbacks, 5, "Authenticating TURN relay {}...", peer_id);
    }
    Command::SetTurnEligibility(eligible) => {
      state.is_turn_eligible = eligible;
      if eligible {
        try_start_turn_server(state, tx, callbacks);
      } else {
        if let Some(handle) = state.turn_server_handle.take() {
          let _ = handle.close_tx.send(());
          crate::node_log!(callbacks, 3, "Node switches off a TURN server");
        }
        state.turn_port = None;
        let key_bytes = b"mercury/turn_relays".to_vec();
        state.providing_keys.remove(&key_bytes);
        swarm
          .behaviour_mut()
          .kademlia
          .stop_providing(&libp2p::kad::RecordKey::new(&"mercury/turn_relays"));
      }
    }
    Command::StartProviding(key_bytes) => {
      state.providing_keys.insert(key_bytes.clone());
      let key = libp2p::kad::RecordKey::new(&key_bytes);
      if let Err(e) = swarm.behaviour_mut().kademlia.start_providing(key) {
        crate::node_log!(callbacks, 2, "Failed to start providing custom DHT key: {:?}", e);
      }
    }
    Command::GetProviders(key_bytes) => {
      let key = libp2p::kad::RecordKey::new(&key_bytes);
      swarm.behaviour_mut().kademlia.get_providers(key);
    }
    _ => {}
  }
}
