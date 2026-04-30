use crate::behaviour::{MercuryBehaviour, MercuryBehaviourEvent};
use crate::types::{Callbacks, Command};
use crate::utils::{hex_encode, notify_connected, notify_disconnected, peerid_to_pubkey, resolve_multiaddr};
use futures::StreamExt;
use libp2p::{Multiaddr, PeerId, Swarm, autonat, identify, kad, mdns, request_response, swarm::SwarmEvent};
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
}

impl EventLoopState {
  pub fn new() -> Self {
    Self {
      pending_dials: HashSet::new(),
      known_addresses: HashMap::new(),
      explicit_bootstrap_peers: HashSet::new(),
      active_relay_peers: HashMap::new(),
      bootstrap_addrs: HashSet::new(),
      active_relay_listeners: HashMap::new(),
      turn_auth: std::sync::Arc::new(crate::turn_server::TurnAuthenticator::new()),
      turn_port: None,
      public_ip: None,
    }
  }

  pub fn do_dial(&mut self, peer_val: PeerId, swarm: &mut Swarm<MercuryBehaviour>, callbacks: &Callbacks) {
    if !self.pending_dials.contains(&peer_val) {
      node_log!(callbacks, 4, "Initiating DHT get_closest_peers for {}", peer_val);
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

    node_log!(callbacks, 4, "Dialing {} with addresses: {:?}", peer_val, addrs);

    let opts = libp2p::swarm::dial_opts::DialOpts::peer_id(peer_val)
      .addresses(addrs)
      .build();
    match swarm.dial(opts) {
      Ok(_) => node_log!(callbacks, 4, "Dial request submitted for {}", peer_val),
      Err(libp2p::swarm::DialError::NoAddresses) => node_log!(
        callbacks,
        3,
        "DialError::NoAddresses for {}, waiting for DHT or relay",
        peer_val
      ),
      Err(libp2p::swarm::DialError::DialPeerConditionFalse(_)) => {
        node_log!(callbacks, 4, "DialError::DialPeerConditionFalse for {}", peer_val);
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
) {
  let mut state = EventLoopState::new();
  let mut keepalive_interval = tokio::time::interval(Duration::from_secs(30));

  loop {
    tokio::select! {
        _ = keepalive_interval.tick() => {
            handle_keepalive(&mut state, &mut swarm, &callbacks);
        }
        cmd = rx.recv() => {
            match cmd {
                Some(Command::Stop) | None => break,
                Some(c) => handle_command(c, &mut state, &mut swarm, &tx, &callbacks),
            }
        }
        event = swarm.select_next_some() => {
            handle_swarm_event(event, &mut state, &mut swarm, &tx, &callbacks);
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
          3,
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
      3,
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

fn handle_command(
  cmd: Command,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  tx: &mpsc::UnboundedSender<Command>,
  callbacks: &Callbacks,
) {
  match cmd {
    Command::Dial(peer_id) => {
      node_log!(callbacks, 4, "Dial command received for {}", peer_id);
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
      node_log!(callbacks, 4, "Disconnect command received for {}", peer_id);
      let _ = swarm.disconnect_peer_id(peer_id);
      state.pending_dials.remove(&peer_id);
    }
    Command::TurnServerStarted { port, public_ip } => {
      state.turn_port = Some(port);
      state.public_ip = Some(public_ip);

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
      node_log!(callbacks, 4, "Discovering TURN relays...");
    }
    Command::AuthTurnRelay(peer_id) => {
      let req = crate::types::TurnAuthRequest { dummy: 0 };
      let _ = swarm.behaviour_mut().turn_auth.send_request(&peer_id, req);
      node_log!(callbacks, 4, "Authenticating TURN relay {}...", peer_id);
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
) {
  match event {
    SwarmEvent::NewListenAddr { address, .. } => {
      node_log!(callbacks, 3, "Listen Active. Multiaddr: {}", address);
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
          let b_addr_len = b_addr.iter().count();
          if remote_addr.iter().take(b_addr_len).eq(b_addr.iter()) {
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
    SwarmEvent::OutgoingConnectionError {
      peer_id: Some(peer_id),
      error,
      ..
    } => {
      node_log!(callbacks, 1, "Outgoing connection error to {}: {:?}", peer_id, error);
      state.pending_dials.remove(&peer_id);
      if !state.explicit_bootstrap_peers.contains(&peer_id) {
        notify_disconnected(callbacks, swarm, &peer_id);
      }
    }
    SwarmEvent::OutgoingConnectionError {
      peer_id: None, error, ..
    } => {
      node_log!(callbacks, 1, "Outgoing connection error to unknown peer: {:?}", error);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Identify(identify::Event::Received { peer_id, info })) => {
      node_log!(
        callbacks,
        4,
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
    }
    SwarmEvent::NewExternalAddrOfPeer { peer_id, address } => {
      node_log!(callbacks, 4, "NewExternalAddrOfPeer for {}: {}", peer_id, address);
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
        4,
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
                    node_log!(callbacks, 3, "Failed to discover addresses for {}", peer);
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
      step,
      ..
    })) => {
      if let Ok(ok) = res {
        if step.last && callbacks.on_turn_relays_discovered != 0 {
          let mut providers_hex = Vec::new();
          for peer in ok.providers {
            if let Some(pk) = peerid_to_pubkey(&peer) {
              providers_hex.push(crate::utils::hex_encode(&pk));
            }
          }
          let joined = providers_hex.join(",");
          let cb: extern "C" fn(*mut c_void, *const c_char) = unsafe { std::mem::transmute(callbacks.on_turn_relays_discovered) };
          if let Ok(c_str) = CString::new(joined) {
            cb(callbacks.context as *mut c_void, c_str.as_ptr());
          }
        }
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(kad::Event::RoutingUpdated { peer, addresses, .. })) => {
      node_log!(callbacks, 4, "Kademlia RoutingUpdated for {}: {:?}", peer, addresses);
      for addr in addresses.iter() {
        state.known_addresses.entry(peer).or_default().insert(addr.clone());
      }
      if state.pending_dials.contains(&peer) {
        state.do_dial(peer, swarm, callbacks);
      }
    }
    SwarmEvent::ListenerClosed {
      listener_id,
      reason,
      addresses,
    } => {
      node_log!(callbacks, 3, "Listener closed: {:?} {:?}", addresses, reason);
      state.active_relay_listeners.retain(|_, id| *id != listener_id);

      for addr in addresses {
        if addr
          .iter()
          .any(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
        {
          let tx_clone = tx.clone();
          tokio::spawn(async move {
            tokio::time::sleep(Duration::from_millis(2000)).await;
            let _ = tx_clone.send(Command::Listen(addr));
          });
        }
      }
    }
    SwarmEvent::ListenerError { listener_id, error } => {
      node_log!(callbacks, 2, "Listener error: {:?}", error);
      state.active_relay_listeners.retain(|_, id| *id != listener_id);
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
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Autonat(autonat::Event::StatusChanged { old, new })) => {
      node_log!(callbacks, 3, "AutoNAT status changed: {:?} -> {:?}", old, new);
      if let autonat::NatStatus::Public(addr) = new {
        if state.turn_port.is_none() {
          let mut public_ip = String::new();
          for protocol in addr.iter() {
            match protocol {
              libp2p::multiaddr::Protocol::Ip4(ip) => public_ip = ip.to_string(),
              libp2p::multiaddr::Protocol::Ip6(ip) => public_ip = ip.to_string(),
              _ => {}
            }
          }
          if !public_ip.is_empty() {
            let turn_auth = state.turn_auth.clone();
            let tx_clone = tx.clone();
            let callbacks_clone = callbacks.clone();

            tokio::spawn(async move {
              match crate::turn_server::start_turn_server(public_ip.clone(), turn_auth).await {
                Ok(port) => {
                  let _ = tx_clone.send(Command::TurnServerStarted { port, public_ip });
                }
                Err(e) => {
                  node_log!(&callbacks_clone, 2, "Failed to start embedded TURN server: {}", e);
                }
              }
            });
          }
        }
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::TurnAuth(request_response::Event::Message { peer, message })) => {
      if let request_response::Message::Request { request: _, channel, .. } = message {
        node_log!(callbacks, 4, "Received TurnAuthRequest from {}", peer);

        let key: [u8; 16] = rand::random();
        let credential = crate::utils::hex_encode(&key);
        let username = format!("{}_{}", peer, std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs());

        state.turn_auth.add_user(username.clone(), credential.clone());

        let response = crate::types::TurnAuthResponse {
          username,
          credential,
          port: state.turn_port.unwrap_or(0),
          public_ip: state.public_ip.clone().unwrap_or_default(),
        };

        let _ = swarm.behaviour_mut().turn_auth.send_response(channel, response);
      } else if let request_response::Message::Response { response, .. } = message {
        if callbacks.on_turn_auth_result != 0 {
          if let Some(pk) = peerid_to_pubkey(&peer) {
            if let Ok(peer_c_str) = CString::new(crate::utils::hex_encode(&pk)) {
              if let Ok(ip_c_str) = CString::new(response.public_ip) {
                if let Ok(user_c_str) = CString::new(response.username) {
                  if let Ok(cred_c_str) = CString::new(response.credential) {
                    let cb: extern "C" fn(*mut c_void, *const c_char, *const c_char, u16, *const c_char, *const c_char) = unsafe { std::mem::transmute(callbacks.on_turn_auth_result) };
                    cb(callbacks.context as *mut c_void, peer_c_str.as_ptr(), ip_c_str.as_ptr(), response.port, user_c_str.as_ptr(), cred_c_str.as_ptr());
                  }
                }
              }
            }
          }
        }
      }
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::TurnAuth(request_response::Event::OutboundFailure { peer, .. })) => {
      if callbacks.on_turn_auth_result != 0 {
        if let Some(pk) = peerid_to_pubkey(&peer) {
          if let Ok(peer_c_str) = CString::new(crate::utils::hex_encode(&pk)) {
            let cb: extern "C" fn(*mut c_void, *const c_char, *const c_char, u16, *const c_char, *const c_char) = unsafe { std::mem::transmute(callbacks.on_turn_auth_result) };
            cb(callbacks.context as *mut c_void, peer_c_str.as_ptr(), std::ptr::null(), 0, std::ptr::null(), std::ptr::null());
          }
        }
      }
    }
    event => {
      node_log!(callbacks, 4, "Unhandled SwarmEvent: {:?}", event);
    }
  }
}
