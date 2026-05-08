use crate::behaviour::{MercuryBehaviour, MercuryBehaviourEvent};
use crate::types::{Callbacks, Command, NodeRole};
use crate::utils::{hex_encode, notify_connected, notify_disconnected, peerid_to_pubkey, resolve_multiaddr};
use futures::StreamExt;
use libp2p::{Multiaddr, PeerId, Swarm, autonat, identify, kad, mdns, ping, request_response, swarm::SwarmEvent};
use std::collections::{HashMap, HashSet};
use std::ffi::CString;
use std::os::raw::{c_char, c_void};
use std::time::Duration;
use tokio::sync::mpsc;

pub struct EventLoopState {
  pub pending_dials: HashSet<PeerId>,
  pub known_addresses: HashMap<PeerId, HashSet<Multiaddr>>,
  pub explicit_bootstrap_peers: HashSet<PeerId>,
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
}

impl EventLoopState {
  pub fn new(role: NodeRole, callbacks: crate::types::Callbacks) -> Self {
    Self {
      pending_dials: HashSet::new(),
      known_addresses: HashMap::new(),
      explicit_bootstrap_peers: HashSet::new(),
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
    }
  }

  pub fn do_dial(&mut self, peer_val: PeerId, swarm: &mut Swarm<MercuryBehaviour>, callbacks: &Callbacks) {
    if !self.pending_dials.contains(&peer_val) {
      node_log!(callbacks, 5, "Initiating DHT get_closest_peers for {}", peer_val);
      swarm.behaviour_mut().kademlia.get_closest_peers(peer_val);
      self.pending_dials.insert(peer_val);
    }

    let mut addrs = Vec::new();
    if let Some(known) = self.known_addresses.get(&peer_val) {
      for addr in known {
        if !addrs.contains(addr) {
          addrs.push(addr.clone());
        }
      }
    }

    for (_, relay_addr) in &self.active_relay_peers {
      let mut relayed = relay_addr.clone();
      relayed.push(libp2p::multiaddr::Protocol::P2pCircuit);
      relayed.push(libp2p::multiaddr::Protocol::P2p(peer_val));
      if !addrs.contains(&relayed) {
        addrs.push(relayed);
      }
    }

    node_log!(callbacks, 5, "Dialing {} with addresses: {:?}", peer_val, addrs);

    let opts = libp2p::swarm::dial_opts::DialOpts::peer_id(peer_val)
      .addresses(addrs)
      .build();
    match swarm.dial(opts) {
      Ok(_) => node_log!(callbacks, 5, "Dial request submitted for {}", peer_val),
      Err(libp2p::swarm::DialError::NoAddresses) => node_log!(
        callbacks,
        4,
        "DialError::NoAddresses for {}, waiting for DHT or relay",
        peer_val
      ),
      Err(libp2p::swarm::DialError::DialPeerConditionFalse(_)) => {
        node_log!(callbacks, 5, "DialError::DialPeerConditionFalse for {}", peer_val);
        if swarm.is_connected(&peer_val) {
          self.pending_dials.remove(&peer_val);
        }
      }
      Err(e) => {
        node_log!(callbacks, 2, "Failed to dial peer {}: {:?}", peer_val, e);
        self.pending_dials.remove(&peer_val);
        notify_disconnected(callbacks, swarm, &peer_val);
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
  let mut keepalive_interval = tokio::time::interval(Duration::from_secs(30));
  let mut stats_interval = tokio::time::interval(Duration::from_secs(3));

  loop {
    tokio::select! {
        _ = keepalive_interval.tick() => {
            handle_keepalive(&mut state, &mut swarm, &callbacks);
        }
        _ = stats_interval.tick() => {
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
                role_str = "relay_node";
            }
            let mut dht_peers = 0;
            for bucket in swarm.behaviour_mut().kademlia.kbuckets() {
                dht_peers += bucket.num_entries();
            }
            state.tick_counter = state.tick_counter.wrapping_add(1);
            node_log!(callbacks, 5, "Emitting network stats: connected={}, bootstrap={}, dht={}, role={}", connected_peers, bootstrap_connected, dht_peers, role_str);
            crate::utils::notify_network_stats(&callbacks, connected_peers as i32, bootstrap_connected as i32, dht_peers as i32, role_str);
        }
        cmd = rx.recv() => {
            match cmd {
                Some(Command::Stop) | None => {
                    if let Some(handle) = state.turn_server_handle.take() {
                        let _ = handle.close_tx.send(());
                        node_log!(callbacks, 3, "Node switches off a TURN server");
                    }
                    break;
                }
                Some(c) => handle_command(c, &mut state, &mut swarm, &tx, &callbacks),
            }
        }
        event = swarm.select_next_some() => {
            handle_swarm_event(event, &mut state, &mut swarm, &tx, &callbacks, role);
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
    if let Some(peer_id) = p2p_peer_id {
      if !swarm.is_connected(&peer_id) {
        node_log!(
          callbacks,
          4,
          "Periodic check: Bootstrap node {} not connected, dialing...",
          peer_id
        );
        let _ = swarm.dial(b_addr.clone());
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
    node_log!(
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
        node_log!(callbacks, 2, "Failed to re-listen on relay: {:?}", e);
      }
    }
  }
}

fn try_start_turn_server(state: &mut EventLoopState, tx: &mpsc::UnboundedSender<Command>, callbacks: &Callbacks) {
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
            node_log!(&callbacks_clone, 2, "Failed to start embedded TURN server: {}", e);
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
      node_log!(callbacks, 5, "Dial command received for {}", peer_id);
      state.do_dial(peer_id, swarm, callbacks);
    }
    Command::AddBootstrap(addr) => {
      let tx_clone = tx.clone();
      tokio::spawn(async move {
        if let Some(resolved) = resolve_multiaddr(addr).await {
          let _ = tx_clone.send(Command::AddBootstrapResolved(resolved));
        } else {
          eprintln!("Failed to resolve bootstrap node");
        }
      });
    }
    Command::AddBootstrapResolved(addr) => {
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
        node_log!(callbacks, 1, "Failed to dial bootstrap node {}: {:?}", addr, e);
      }
    }
    Command::Listen(addr) => {
      if !state.active_relay_listeners.contains_key(&addr) {
        match swarm.listen_on(addr.clone()) {
          Ok(id) => {
            state.active_relay_listeners.insert(addr.clone(), id);
            node_log!(callbacks, 3, "Listening on relay: {}", addr);
          }
          Err(e) => {
            node_log!(callbacks, 1, "Failed to listen on {}: {:?}", addr, e);
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
      node_log!(callbacks, 5, "Disconnect command received for {}", peer_id);
      let _ = swarm.disconnect_peer_id(peer_id);
      state.pending_dials.remove(&peer_id);
    }
    Command::TurnServerStarted {
      port,
      public_ip,
      handle,
    } => {
      state.turn_port = Some(port);
      state.public_ip = Some(public_ip.clone());
      state.turn_server_handle = Some(handle);

      node_log!(callbacks, 3, "Node starts a TURN server on {}:{}", public_ip, port);

      let record_key = libp2p::kad::RecordKey::new(&"mercury/turn_relays");
      if let Err(e) = swarm.behaviour_mut().kademlia.start_providing(record_key) {
        node_log!(callbacks, 2, "Failed to start providing TURN relay record: {:?}", e);
      } else {
        node_log!(callbacks, 3, "Announcing as TURN relay to DHT on port {}", port);
      }
    }
    Command::DiscoverTurnRelays => {
      let key = libp2p::kad::RecordKey::new(&"mercury/turn_relays");
      swarm.behaviour_mut().kademlia.get_providers(key);
      node_log!(callbacks, 5, "Discovering TURN relays...");
    }
    Command::AuthTurnRelay(peer_id) => {
      let req = crate::types::TurnAuthRequest { dummy: 0 };
      let _ = swarm.behaviour_mut().turn_auth.send_request(&peer_id, req);
      node_log!(callbacks, 5, "Authenticating TURN relay {}...", peer_id);
    }
    Command::SetTurnEligibility(eligible) => {
      state.is_turn_eligible = eligible;
      if eligible {
        try_start_turn_server(state, tx, callbacks);
      } else {
        if let Some(handle) = state.turn_server_handle.take() {
          let _ = handle.close_tx.send(());
          node_log!(callbacks, 3, "Node switches off a TURN server");
        }
        state.turn_port = None;
        swarm
          .behaviour_mut()
          .kademlia
          .stop_providing(&libp2p::kad::RecordKey::new(&"mercury/turn_relays"));
      }
    }
    _ => {}
  }
}

fn handle_swarm_event(
  event: SwarmEvent<MercuryBehaviourEvent>,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  tx: &mpsc::UnboundedSender<Command>,
  callbacks: &Callbacks,
  role: NodeRole,
) {
  match event {
    SwarmEvent::NewListenAddr { address, .. } => {
      node_log!(callbacks, 3, "Listen Active. Multiaddr: {}", address);
      if state.public_port.is_none() {
        for protocol in address.iter() {
          match protocol {
            libp2p::multiaddr::Protocol::Tcp(p) | libp2p::multiaddr::Protocol::Udp(p) => {
              if p != 0 {
                state.public_port = Some(p);
              }
            }
            _ => {}
          }
        }
      }
      if address
        .iter()
        .any(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
      {
        node_log!(callbacks, 3, "Node become relay");
      }
    }
    SwarmEvent::ConnectionEstablished { peer_id, endpoint, .. } => {
      let mut remote_addr = endpoint.get_remote_address().clone();

      if !remote_addr.iter().any(|p| {
        matches!(
          p,
          libp2p::multiaddr::Protocol::Ip4(_) | libp2p::multiaddr::Protocol::Ip6(_)
        )
      }) {
        let mut found = false;
        if let Some(addrs) = state.known_addresses.get(&peer_id) {
          if let Some(ip_addr) = addrs.iter().find(|a| {
            a.iter().any(|p| {
              matches!(
                p,
                libp2p::multiaddr::Protocol::Ip4(_) | libp2p::multiaddr::Protocol::Ip6(_)
              )
            })
          }) {
            remote_addr = ip_addr.clone();
            found = true;
          }
        }
        if !found {
          for b_addr in &state.bootstrap_addrs {
            if b_addr
              .iter()
              .any(|p| matches!(p, libp2p::multiaddr::Protocol::P2p(id) if id == peer_id))
            {
              if b_addr.iter().any(|p| {
                matches!(
                  p,
                  libp2p::multiaddr::Protocol::Ip4(_) | libp2p::multiaddr::Protocol::Ip6(_)
                )
              }) {
                remote_addr = b_addr.clone();
                break;
              }
            }
          }
        }
      }
      if !remote_addr
        .iter()
        .any(|p| matches!(p, libp2p::multiaddr::Protocol::P2p(_)))
      {
        remote_addr.push(libp2p::multiaddr::Protocol::P2p(peer_id));
      }

      state
        .known_addresses
        .entry(peer_id)
        .or_default()
        .insert(remote_addr.clone());
      swarm
        .behaviour_mut()
        .kademlia
        .add_address(&peer_id, remote_addr.clone());
      node_log!(callbacks, 3, "Connection established with {}", peer_id);

      let is_circuit = remote_addr
        .iter()
        .any(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit));
      let mut is_bootstrap = state.explicit_bootstrap_peers.contains(&peer_id);
      if !is_bootstrap && !is_circuit {
        for b_addr in &state.bootstrap_addrs {
          // A bootstrap address may lack the P2P protocol postfix prior to the first connection
          let b_addr_len = b_addr
            .iter()
            .filter(|p| !matches!(p, libp2p::multiaddr::Protocol::P2p(_)))
            .count();
          if remote_addr.iter().take(b_addr_len).eq(b_addr.iter().take(b_addr_len)) {
            is_bootstrap = true;
            state.explicit_bootstrap_peers.insert(peer_id);
            break;
          }
        }
      }

      if endpoint.is_dialer() {
        let _ = swarm.behaviour_mut().kademlia.bootstrap();
        if is_bootstrap && !is_circuit {
          let mut relay_listen_addr = remote_addr.clone();
          relay_listen_addr.push(libp2p::multiaddr::Protocol::P2pCircuit);
          state.active_relay_peers.insert(peer_id, remote_addr);
          let tx_clone = tx.clone();
          tokio::spawn(async move {
            tokio::time::sleep(Duration::from_millis(1500)).await;
            let _ = tx_clone.send(Command::Listen(relay_listen_addr));
          });
        }
      }

      state.pending_dials.remove(&peer_id);
      if !is_bootstrap {
        notify_connected(callbacks, &peer_id, is_circuit);
      }
    }
    SwarmEvent::ConnectionClosed { peer_id, .. } => {
      node_log!(callbacks, 3, "Connection closed with {}", peer_id);
      if !swarm.is_connected(&peer_id) {
        state.active_relay_peers.remove(&peer_id);
      }
      if !state.explicit_bootstrap_peers.contains(&peer_id) {
        notify_disconnected(callbacks, swarm, &peer_id);
      }
    }
    SwarmEvent::IncomingConnection {
      connection_id,
      local_addr,
      send_back_addr,
    } => {
      node_log!(
        callbacks,
        5,
        "IncomingConnection {:?} local: {} remote: {}",
        connection_id,
        local_addr,
        send_back_addr
      );
    }
    SwarmEvent::IncomingConnectionError {
      connection_id,
      local_addr,
      send_back_addr,
      error,
    } => {
      node_log!(
        callbacks,
        2,
        "IncomingConnectionError {:?} local: {} remote: {} error: {:?}",
        connection_id,
        local_addr,
        send_back_addr,
        error
      );
    }
    SwarmEvent::Dialing { peer_id, connection_id } => {
      node_log!(callbacks, 5, "Dialing {:?} connection_id: {:?}", peer_id, connection_id);
    }
    SwarmEvent::OutgoingConnectionError {
      peer_id: Some(peer_id),
      error,
      ..
    } => {
      node_log!(callbacks, 4, "Outgoing connection error to {}: {:?}", peer_id, error);
      state.pending_dials.remove(&peer_id);
      if !state.explicit_bootstrap_peers.contains(&peer_id) {
        notify_disconnected(callbacks, swarm, &peer_id);
      }
    }
    SwarmEvent::OutgoingConnectionError {
      peer_id: None, error, ..
    } => {
      node_log!(callbacks, 4, "Outgoing connection error to unknown peer: {:?}", error);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Identify(identify::Event::Received { peer_id, info })) => {
      node_log!(
        callbacks,
        5,
        "Identify received from {}: listen_addrs={:?}",
        peer_id,
        info.listen_addrs
      );
      let _ = swarm.add_external_address(info.observed_addr.clone());
      for addr in info.listen_addrs {
        state.known_addresses.entry(peer_id).or_default().insert(addr.clone());
        swarm.behaviour_mut().kademlia.add_address(&peer_id, addr);
      }
      if let Err(e) = swarm.behaviour_mut().kademlia.bootstrap() {
        node_log!(callbacks, 2, "Kademlia bootstrap failed: {:?}", e);
      }
      if state.pending_dials.contains(&peer_id) {
        state.do_dial(peer_id, swarm, callbacks);
      }
    }
    SwarmEvent::ExternalAddrConfirmed { address } => {
      node_log!(callbacks, 3, "External address confirmed: {}", address);
      if state.public_ip.is_none() {
        let mut public_ip = String::new();
        for protocol in address.iter() {
          match protocol {
            libp2p::multiaddr::Protocol::Ip4(ip) => public_ip = ip.to_string(),
            libp2p::multiaddr::Protocol::Ip6(ip) => public_ip = ip.to_string(),
            libp2p::multiaddr::Protocol::Tcp(p) | libp2p::multiaddr::Protocol::Udp(p) => {
              if state.public_port.is_none() && p != 0 {
                state.public_port = Some(p);
              }
            }
            _ => {}
          }
        }
        if !public_ip.is_empty() {
          state.public_ip = Some(public_ip);
          if matches!(role, NodeRole::BootstrapNode) {
            state.is_public = true;
            try_start_turn_server(state, tx, callbacks);
          }
        }
      }
    }
    SwarmEvent::NewExternalAddrCandidate { address } => {
      node_log!(callbacks, 4, "New external address candidate: {}", address);
      if state.public_ip.is_none() {
        let mut public_ip = String::new();
        for protocol in address.iter() {
          match protocol {
            libp2p::multiaddr::Protocol::Ip4(ip) => public_ip = ip.to_string(),
            libp2p::multiaddr::Protocol::Ip6(ip) => public_ip = ip.to_string(),
            libp2p::multiaddr::Protocol::Tcp(p) | libp2p::multiaddr::Protocol::Udp(p) => {
              if state.public_port.is_none() && p != 0 {
                state.public_port = Some(p);
              }
            }
            _ => {}
          }
        }
        if !public_ip.is_empty() {
          state.public_ip = Some(public_ip);
          if matches!(role, NodeRole::BootstrapNode) {
            state.is_public = true;
            try_start_turn_server(state, tx, callbacks);
          }
        }
      }
    }
    SwarmEvent::NewExternalAddrOfPeer { peer_id, address } => {
      node_log!(callbacks, 5, "NewExternalAddrOfPeer for {}: {}", peer_id, address);
      state
        .known_addresses
        .entry(peer_id)
        .or_default()
        .insert(address.clone());
      swarm.behaviour_mut().kademlia.add_address(&peer_id, address);
      if state.pending_dials.contains(&peer_id) {
        state.do_dial(peer_id, swarm, callbacks);
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Mdns(mdns::Event::Discovered(list))) => {
      for (peer_id, addr) in list {
        state.known_addresses.entry(peer_id).or_default().insert(addr.clone());
        swarm.behaviour_mut().kademlia.add_address(&peer_id, addr);
        if state.pending_dials.contains(&peer_id) {
          state.do_dial(peer_id, swarm, callbacks);
        }
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(kad::Event::OutboundQueryProgressed {
      result: kad::QueryResult::GetClosestPeers(res),
      step,
      ..
    })) => {
      node_log!(
        callbacks,
        5,
        "Kademlia GetClosestPeers progressed: step={:?} res={:?}",
        step,
        res
      );
      let key = match &res {
        Ok(ok) => Some(ok.key.clone()),
        Err(kad::GetClosestPeersError::Timeout { key, .. }) => Some(key.clone()),
        #[allow(unreachable_patterns)]
        _ => None,
      };
      if let Some(key) = key {
        if let Ok(peer) = PeerId::from_bytes(&key) {
          if step.last && state.pending_dials.remove(&peer) {
            if !swarm.is_connected(&peer) {
              let mut addrs = Vec::new();
              if let Some(known) = state.known_addresses.get(&peer) {
                addrs.extend(known.iter().cloned());
              }
              for (_, relay_addr) in &state.active_relay_peers {
                let mut relayed = relay_addr.clone();
                relayed.push(libp2p::multiaddr::Protocol::P2pCircuit);
                relayed.push(libp2p::multiaddr::Protocol::P2p(peer));
                if !addrs.contains(&relayed) {
                  addrs.push(relayed);
                }
              }
              let opts = libp2p::swarm::dial_opts::DialOpts::peer_id(peer)
                .condition(libp2p::swarm::dial_opts::PeerCondition::Disconnected)
                .addresses(addrs)
                .build();
              match swarm.dial(opts) {
                Err(libp2p::swarm::DialError::NoAddresses) => {
                  if !state.explicit_bootstrap_peers.contains(&peer) {
                    node_log!(callbacks, 4, "Failed to discover addresses for {}", peer);
                    notify_disconnected(callbacks, swarm, &peer);
                  }
                }
                Err(e) => {
                  node_log!(callbacks, 2, "Fallback dial error for {}: {:?}", peer, e);
                }
                Ok(_) => {
                  node_log!(callbacks, 4, "Fallback dial submitted for {}", peer);
                }
              }
            }
          }
        }
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(kad::Event::OutboundQueryProgressed {
      result: kad::QueryResult::GetProviders(res),
      ..
    })) => {
      if let Ok(kad::GetProvidersOk::FoundProviders { providers, .. }) = res {
        if callbacks.on_turn_relays_discovered != 0 {
          let mut providers_hex = Vec::new();
          for peer in providers {
            if let Some(pk) = peerid_to_pubkey(&peer) {
              providers_hex.push(crate::utils::hex_encode(&pk));
            }
          }
          if !providers_hex.is_empty() {
            let joined = providers_hex.join(",");
            let cb: extern "C" fn(*mut c_void, *const c_char) =
              unsafe { std::mem::transmute(callbacks.on_turn_relays_discovered) };
            if let Ok(c_str) = CString::new(joined) {
              cb(callbacks.context as *mut c_void, c_str.as_ptr());
            }
          }
        }
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(kad::Event::RoutingUpdated { peer, addresses, .. })) => {
      node_log!(callbacks, 5, "Kademlia RoutingUpdated for {}: {:?}", peer, addresses);
      for addr in addresses.iter() {
        state.known_addresses.entry(peer).or_default().insert(addr.clone());
      }
      if state.pending_dials.contains(&peer) {
        state.do_dial(peer, swarm, callbacks);
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(e)) => {
      node_log!(callbacks, 5, "Kademlia event: {:?}", e);
    }
    SwarmEvent::ListenerClosed {
      listener_id,
      reason,
      addresses,
    } => {
      node_log!(callbacks, 3, "Listener closed: {:?} {:?}", addresses, reason);
      state.active_relay_listeners.retain(|_, id| *id != listener_id);

      let mut lost_relay = false;
      for addr in addresses {
        if addr
          .iter()
          .any(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
        {
          lost_relay = true;
          let tx_clone = tx.clone();
          tokio::spawn(async move {
            tokio::time::sleep(Duration::from_millis(2000)).await;
            let _ = tx_clone.send(Command::Listen(addr));
          });
        }
      }
      if lost_relay {
        node_log!(callbacks, 3, "Node lost relay status");
      }
    }
    SwarmEvent::ListenerError { listener_id, error } => {
      node_log!(callbacks, 2, "Listener error: {:?}", error);
      state.active_relay_listeners.retain(|_, id| *id != listener_id);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::RelayServer(e)) => {
      node_log!(callbacks, 5, "RelayServer event: {:?}", e);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::RelayClient(e)) => {
      node_log!(callbacks, 5, "RelayClient event: {:?}", e);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Dcutr(e)) => {
      node_log!(callbacks, 5, "DCUtR event: {:?}", e);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Chat(request_response::Event::Message { peer, message })) => {
      if let request_response::Message::Request { request, channel, .. } = message {
        if callbacks.on_message != 0 {
          let cb: extern "C" fn(*mut c_void, *const c_char, *const u8, usize) =
            unsafe { std::mem::transmute(callbacks.on_message) };
          if let Some(pk) = peerid_to_pubkey(&peer) {
            if let Ok(c_str) = CString::new(hex_encode(&pk)) {
              cb(
                callbacks.context as *mut c_void,
                c_str.as_ptr(),
                request.as_ptr(),
                request.len(),
              );
            }
          }
        }
        let _ = swarm.behaviour_mut().chat.send_response(channel, vec![]);
      } else if let request_response::Message::Response { .. } = message {
        node_log!(callbacks, 5, "Received Chat Response from {}", peer);
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::File(request_response::Event::Message { peer, message })) => {
      if let request_response::Message::Request { request, channel, .. } = message {
        if callbacks.on_message != 0 {
          let cb: extern "C" fn(*mut c_void, *const c_char, *const u8, usize) =
            unsafe { std::mem::transmute(callbacks.on_message) };
          if let Some(pk) = peerid_to_pubkey(&peer) {
            if let Ok(c_str) = CString::new(hex_encode(&pk)) {
              cb(
                callbacks.context as *mut c_void,
                c_str.as_ptr(),
                request.as_ptr(),
                request.len(),
              );
            }
          }
        }
        let _ = swarm.behaviour_mut().file.send_response(channel, vec![]);
      } else if let request_response::Message::Response { .. } = message {
        node_log!(callbacks, 5, "Received File Response from {}", peer);
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Chat(request_response::Event::ResponseSent { peer, .. })) => {
      node_log!(callbacks, 5, "Chat response sent to {}", peer);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Chat(request_response::Event::OutboundFailure {
      peer,
      error,
      ..
    })) => {
      node_log!(callbacks, 2, "Chat outbound failure to {}: {:?}", peer, error);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Chat(request_response::Event::InboundFailure {
      peer, error, ..
    })) => {
      node_log!(callbacks, 2, "Chat inbound failure from {}: {:?}", peer, error);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::File(request_response::Event::ResponseSent { peer, .. })) => {
      node_log!(callbacks, 5, "File response sent to {}", peer);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::File(request_response::Event::OutboundFailure {
      peer,
      error,
      ..
    })) => {
      node_log!(callbacks, 2, "File outbound failure to {}: {:?}", peer, error);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::File(request_response::Event::InboundFailure {
      peer, error, ..
    })) => {
      node_log!(callbacks, 2, "File inbound failure from {}: {:?}", peer, error);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Autonat(autonat::Event::StatusChanged { old, new })) => {
      node_log!(callbacks, 3, "AutoNAT status changed: {:?} -> {:?}", old, new);
      if let autonat::NatStatus::Public(addr) = new {
        state.is_public = true;
        if state.turn_port.is_none() {
          let mut public_ip = String::new();
          for protocol in addr.iter() {
            match protocol {
              libp2p::multiaddr::Protocol::Ip4(ip) => public_ip = ip.to_string(),
              libp2p::multiaddr::Protocol::Ip6(ip) => public_ip = ip.to_string(),
              libp2p::multiaddr::Protocol::Tcp(p) | libp2p::multiaddr::Protocol::Udp(p) => {
                if state.public_port.is_none() && p != 0 {
                  state.public_port = Some(p);
                }
              }
              _ => {}
            }
          }
          if !public_ip.is_empty() {
            state.public_ip = Some(public_ip);
          }
          try_start_turn_server(state, tx, callbacks);
        }
      } else {
        state.is_public = false;
        if let Some(handle) = state.turn_server_handle.take() {
          let _ = handle.close_tx.send(());
          node_log!(callbacks, 3, "Node switches off a TURN server");
        }
        state.turn_port = None;
        swarm
          .behaviour_mut()
          .kademlia
          .stop_providing(&libp2p::kad::RecordKey::new(&"mercury/turn_relays"));
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Autonat(e)) => {
      node_log!(callbacks, 5, "AutoNAT event: {:?}", e);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::TurnAuth(request_response::Event::Message { peer, message })) => {
      if let request_response::Message::Request {
        request: _, channel, ..
      } = message
      {
        node_log!(callbacks, 5, "Received TurnAuthRequest from {}", peer);

        if state.turn_port.is_none() {
          let response = crate::types::TurnAuthResponse {
            username: String::new(),
            credential: String::new(),
            port: 0,
            public_ip: String::new(),
          };
          let _ = swarm.behaviour_mut().turn_auth.send_response(channel, response);
          return;
        }

        let key: [u8; 16] = rand::random();
        let credential = crate::utils::hex_encode(&key);
        let username = format!(
          "{}_{}",
          peer,
          std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_secs()
        );

        state.turn_auth.add_user(username.clone(), credential.clone());

        let response = crate::types::TurnAuthResponse {
          username,
          credential,
          port: state.turn_port.unwrap_or(0),
          public_ip: state.public_ip.clone().unwrap_or_default(),
        };

        node_log!(
          callbacks,
          4,
          "Sending TurnAuthResponse: ip={}, port={}, user={}",
          response.public_ip,
          response.port,
          response.username
        );

        let _ = swarm.behaviour_mut().turn_auth.send_response(channel, response);
      } else if let request_response::Message::Response { response, .. } = message {
        node_log!(
          callbacks,
          4,
          "[TURN] Received TurnAuthResponse from {}: ip={}, port={}, user={}",
          peer,
          response.public_ip,
          response.port,
          response.username
        );
        if callbacks.on_turn_auth_result != 0 {
          if let Some(pk) = peerid_to_pubkey(&peer) {
            if let Ok(peer_c_str) = CString::new(crate::utils::hex_encode(&pk)) {
              if let Ok(ip_c_str) = CString::new(response.public_ip) {
                if let Ok(user_c_str) = CString::new(response.username) {
                  if let Ok(cred_c_str) = CString::new(response.credential) {
                    let cb: extern "C" fn(
                      *mut c_void,
                      *const c_char,
                      *const c_char,
                      u16,
                      *const c_char,
                      *const c_char,
                    ) = unsafe { std::mem::transmute(callbacks.on_turn_auth_result) };
                    cb(
                      callbacks.context as *mut c_void,
                      peer_c_str.as_ptr(),
                      ip_c_str.as_ptr(),
                      response.port,
                      user_c_str.as_ptr(),
                      cred_c_str.as_ptr(),
                    );
                  }
                }
              }
            }
          }
        }
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::TurnAuth(request_response::Event::OutboundFailure {
      peer,
      error,
      ..
    })) => {
      node_log!(callbacks, 2, "[TURN] TurnAuth request to {} failed: {:?}", peer, error);
      if callbacks.on_turn_auth_result != 0 {
        if let Some(pk) = peerid_to_pubkey(&peer) {
          if let Ok(peer_c_str) = CString::new(crate::utils::hex_encode(&pk)) {
            let cb: extern "C" fn(*mut c_void, *const c_char, *const c_char, u16, *const c_char, *const c_char) =
              unsafe { std::mem::transmute(callbacks.on_turn_auth_result) };
            cb(
              callbacks.context as *mut c_void,
              peer_c_str.as_ptr(),
              std::ptr::null(),
              0,
              std::ptr::null(),
              std::ptr::null(),
            );
          }
        }
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::TurnAuth(request_response::Event::InboundFailure {
      peer,
      error,
      ..
    })) => {
      node_log!(callbacks, 2, "TurnAuth inbound failure from {}: {:?}", peer, error);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::TurnAuth(request_response::Event::ResponseSent { peer, .. })) => {
      node_log!(callbacks, 5, "TurnAuth response sent to {}", peer);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Ping(ping::Event { peer, result, .. })) => match result {
      Ok(rtt) => node_log!(callbacks, 5, "Ping to {} successful, RTT: {:?}", peer, rtt),
      Err(e) => node_log!(callbacks, 5, "Ping to {} failed: {:?}", peer, e),
    },
    event => {
      node_log!(callbacks, 5, "Unhandled SwarmEvent: {:?}", event);
    }
  }
}
