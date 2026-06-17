use crate::behaviour::{MercuryBehaviour, MercuryBehaviourEvent};
use crate::event_loop::{EventLoopState, is_bootstrap_direct_ip, try_start_turn_server};
use crate::types::{Callbacks, Command, NodeRole};
use crate::utils::{force_notify_disconnected, hex_encode, notify_connected, notify_disconnected, peerid_to_pubkey};
use libp2p::{Multiaddr, PeerId, Swarm, autonat, identify, kad, mdns, ping, request_response, swarm::SwarmEvent};
use std::ffi::CString;
use std::os::raw::{c_char, c_void};
use tokio::sync::mpsc;

pub(crate) fn handle_swarm_event(
  event: SwarmEvent<MercuryBehaviourEvent>,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  tx: &mpsc::UnboundedSender<Command>,
  callbacks: &Callbacks,
  role: NodeRole,
) {
  match event {
    SwarmEvent::NewListenAddr { address, .. } => handle_new_listen_addr(address, state, callbacks, role),
    SwarmEvent::ConnectionEstablished { peer_id, endpoint, .. } => {
      handle_connection_established(peer_id, endpoint, state, swarm, tx, callbacks, role)
    }
    SwarmEvent::ConnectionClosed { peer_id, .. } => handle_connection_closed(peer_id, state, swarm, callbacks),
    SwarmEvent::IncomingConnection {
      connection_id,
      local_addr,
      send_back_addr,
    } => {
      crate::node_log!(
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
      crate::node_log!(
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
      crate::node_log!(callbacks, 5, "Dialing {:?} connection_id: {:?}", peer_id, connection_id);
    }
    SwarmEvent::OutgoingConnectionError {
      peer_id: Some(peer_id),
      error,
      ..
    } => {
      crate::node_log!(callbacks, 5, "Outgoing connection error to {}: {:?}", peer_id, error);
      state.pending_dials.remove(&peer_id);
      notify_disconnected(callbacks, swarm, &peer_id);
    }
    SwarmEvent::OutgoingConnectionError {
      peer_id: None, error, ..
    } => {
      crate::node_log!(callbacks, 5, "Outgoing connection error to unknown peer: {:?}", error);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Identify(e)) => handle_identify(e, state, swarm, callbacks),
    SwarmEvent::ExternalAddrConfirmed { address } => {
      handle_external_addr_confirmed(address, state, callbacks, role, tx)
    }
    SwarmEvent::NewExternalAddrCandidate { address } => {
      handle_new_external_addr_candidate(address, state, callbacks, role, tx)
    }
    SwarmEvent::NewExternalAddrOfPeer { peer_id, address } => {
      handle_new_external_addr_of_peer(peer_id, address, state, swarm, callbacks)
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Mdns(e)) => handle_mdns(e, state, swarm, callbacks),
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Kademlia(e)) => handle_kademlia(e, state, swarm, callbacks),
    SwarmEvent::ListenerClosed {
      listener_id,
      reason,
      addresses,
    } => handle_listener_closed(listener_id, reason, addresses, state, callbacks),
    SwarmEvent::ListenerError { listener_id, error } => handle_listener_error(listener_id, error, state, callbacks),
    SwarmEvent::Behaviour(MercuryBehaviourEvent::RelayServer(e)) => {
      crate::node_log!(callbacks, 5, "RelayServer event: {:?}", e);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::RelayClient(e)) => {
      crate::node_log!(callbacks, 5, "RelayClient event: {:?}", e);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Dcutr(e)) => {
      crate::node_log!(callbacks, 5, "DCUtR event: {:?}", e);
    }
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Chat(e)) => handle_chat(e, swarm, callbacks),
    SwarmEvent::Behaviour(MercuryBehaviourEvent::File(e)) => handle_file(e, swarm, callbacks),
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Autonat(e)) => handle_autonat(e, state, swarm, tx, callbacks, role),
    SwarmEvent::Behaviour(MercuryBehaviourEvent::TurnAuth(e)) => handle_turn_auth(e, state, swarm, callbacks),
    SwarmEvent::Behaviour(MercuryBehaviourEvent::Ping(e)) => handle_ping(e, swarm, callbacks),
    event => {
      crate::node_log!(callbacks, 5, "Unhandled SwarmEvent: {:?}", event);
    }
  }
}

fn handle_new_listen_addr(address: Multiaddr, state: &mut EventLoopState, callbacks: &Callbacks, _role: NodeRole) {
  crate::node_log!(callbacks, 3, "Listen Active. Multiaddr: {}", address);
  if address
    .iter()
    .any(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
  {
    crate::node_log!(callbacks, 3, "Node became relayed client");
    return;
  }
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
}

fn handle_connection_established(
  peer_id: PeerId,
  endpoint: libp2p::core::ConnectedPoint,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  tx: &mpsc::UnboundedSender<Command>,
  callbacks: &Callbacks,
  role: NodeRole,
) {
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

  let circuit_count = remote_addr
    .iter()
    .filter(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
    .count();
  let is_circuit = circuit_count > 0;
  if circuit_count <= 1 {
    state
      .known_addresses
      .entry(peer_id)
      .or_default()
      .insert(remote_addr.clone());
    swarm
      .behaviour_mut()
      .kademlia
      .add_address(&peer_id, remote_addr.clone());
  }
  crate::node_log!(callbacks, 3, "Connection established with {}", peer_id);

  let mut is_bootstrap = state.explicit_bootstrap_peers.contains(&peer_id);
  if !is_bootstrap && !is_circuit {
    let mut new_b_addrs = Vec::new();
    for b_addr in &state.bootstrap_addrs {
      let b_addr_len = b_addr
        .iter()
        .filter(|p| !matches!(p, libp2p::multiaddr::Protocol::P2p(_)))
        .count();
      if remote_addr.iter().take(b_addr_len).eq(b_addr.iter().take(b_addr_len)) {
        is_bootstrap = true;
        state.explicit_bootstrap_peers.insert(peer_id);
        if b_addr.iter().all(|p| !matches!(p, libp2p::multiaddr::Protocol::P2p(_))) {
          let mut updated_b_addr = b_addr.clone();
          updated_b_addr.push(libp2p::multiaddr::Protocol::P2p(peer_id));
          new_b_addrs.push((b_addr.clone(), updated_b_addr));
        }
        break;
      }
    }
    for (old, new) in new_b_addrs {
      state.bootstrap_addrs.remove(&old);
      state.bootstrap_addrs.insert(new);
    }
  }

  if endpoint.is_dialer() {
    if is_bootstrap && !is_circuit && !matches!(role, NodeRole::BootstrapNode) {
      let mut relay_listen_addr = remote_addr.clone();
      relay_listen_addr.push(libp2p::multiaddr::Protocol::P2pCircuit);
      state.active_relay_peers.insert(peer_id, remote_addr);
      let tx_clone = tx.clone();
      tokio::spawn(async move {
        tokio::time::sleep(std::time::Duration::from_millis(1500)).await;
        let _ = tx_clone.send(Command::Listen(relay_listen_addr));
      });
    }
  }

  state.pending_dials.remove(&peer_id);
  if !is_bootstrap {
    notify_connected(callbacks, &peer_id, is_circuit);
  }
}

fn handle_connection_closed(
  peer_id: PeerId,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  callbacks: &Callbacks,
) {
  crate::node_log!(callbacks, 3, "Connection closed with {}", peer_id);
  if !swarm.is_connected(&peer_id) {
    state.active_relay_peers.remove(&peer_id);
    notify_disconnected(callbacks, swarm, &peer_id);
  }
}

fn handle_identify(
  event: identify::Event,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  callbacks: &Callbacks,
) {
  if let identify::Event::Received { peer_id, info } = event {
    crate::node_log!(
      callbacks,
      5,
      "Identify received from {}: listen_addrs={:?}",
      peer_id,
      info.listen_addrs
    );
    let obs_circuit_count = info
      .observed_addr
      .iter()
      .filter(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
      .count();
    if obs_circuit_count <= 1 && !is_bootstrap_direct_ip(&info.observed_addr, &state.bootstrap_addrs) {
      let _ = swarm.add_external_address(info.observed_addr.clone());
    }
    for addr in info.listen_addrs {
      let circuit_count = addr
        .iter()
        .filter(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
        .count();
      if circuit_count <= 1 && !is_bootstrap_direct_ip(&addr, &state.bootstrap_addrs) {
        state.known_addresses.entry(peer_id).or_default().insert(addr.clone());
        swarm.behaviour_mut().kademlia.add_address(&peer_id, addr);
      }
    }
    if !state.has_bootstrapped {
      if let Err(e) = swarm.behaviour_mut().kademlia.bootstrap() {
        crate::node_log!(callbacks, 5, "Kademlia bootstrap not ready: {:?}", e);
      } else {
        state.has_bootstrapped = true;
        crate::node_log!(callbacks, 3, "Kademlia bootstrap initiated");
        for key_bytes in &state.providing_keys {
          let key = libp2p::kad::RecordKey::new(key_bytes);
          let _ = swarm.behaviour_mut().kademlia.start_providing(key);
        }
      }
    }
    if state.pending_dials.contains(&peer_id) {
      state.do_dial(peer_id, swarm, callbacks);
    }
  }
}

fn check_and_update_public_ip(
  address: Multiaddr,
  state: &mut EventLoopState,
  callbacks: &Callbacks,
  role: NodeRole,
  tx: &mpsc::UnboundedSender<Command>,
) {
  let circuit_count = address
    .iter()
    .filter(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
    .count();
  if circuit_count > 1 || is_bootstrap_direct_ip(&address, &state.bootstrap_addrs) {
    return;
  }
  if circuit_count == 0 && state.public_ip.is_none() {
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

fn handle_external_addr_confirmed(
  address: Multiaddr,
  state: &mut EventLoopState,
  callbacks: &Callbacks,
  role: NodeRole,
  tx: &mpsc::UnboundedSender<Command>,
) {
  crate::node_log!(callbacks, 3, "External address confirmed: {}", address);
  check_and_update_public_ip(address, state, callbacks, role, tx);
}

fn handle_new_external_addr_candidate(
  address: Multiaddr,
  state: &mut EventLoopState,
  callbacks: &Callbacks,
  role: NodeRole,
  tx: &mpsc::UnboundedSender<Command>,
) {
  crate::node_log!(callbacks, 5, "New external address candidate: {}", address);
  check_and_update_public_ip(address, state, callbacks, role, tx);
}

fn handle_new_external_addr_of_peer(
  peer_id: PeerId,
  address: Multiaddr,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  callbacks: &Callbacks,
) {
  let circuit_count = address
    .iter()
    .filter(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
    .count();
  if circuit_count <= 1 && !is_bootstrap_direct_ip(&address, &state.bootstrap_addrs) {
    crate::node_log!(callbacks, 5, "NewExternalAddrOfPeer for {}: {}", peer_id, address);
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
}

fn handle_mdns(
  event: mdns::Event,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  callbacks: &Callbacks,
) {
  if let mdns::Event::Discovered(list) = event {
    for (peer_id, addr) in list {
      state.known_addresses.entry(peer_id).or_default().insert(addr.clone());
      swarm.behaviour_mut().kademlia.add_address(&peer_id, addr);
      if state.pending_dials.contains(&peer_id) {
        state.do_dial(peer_id, swarm, callbacks);
      }
    }
  }
}

fn handle_kademlia(
  event: kad::Event,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  callbacks: &Callbacks,
) {
  match event {
    kad::Event::OutboundQueryProgressed {
      result: kad::QueryResult::GetClosestPeers(res),
      step,
      ..
    } => {
      crate::node_log!(
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
              for b_addr in &state.bootstrap_addrs {
                if b_addr.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::P2p(_))) {
                  let mut relayed = b_addr.clone();
                  relayed.push(libp2p::multiaddr::Protocol::P2pCircuit);
                  relayed.push(libp2p::multiaddr::Protocol::P2p(peer));
                  if !addrs.contains(&relayed) {
                    addrs.push(relayed);
                  }
                }
              }
              for b_addr in &state.bootstrap_addrs {
                if b_addr.iter().any(|p| matches!(p, libp2p::multiaddr::Protocol::P2p(_))) {
                  let mut relayed = b_addr.clone();
                  relayed.push(libp2p::multiaddr::Protocol::P2pCircuit);
                  relayed.push(libp2p::multiaddr::Protocol::P2p(peer));
                  if !addrs.contains(&relayed) {
                    addrs.push(relayed);
                  }
                }
              }

              crate::node_log!(
                callbacks,
                4,
                "kademlia dial fallback: {} with {} active relays, {} bootstrap addrs, {} total addrs",
                peer,
                state.active_relay_peers.len(),
                state.bootstrap_addrs.len(),
                addrs.len()
              );

              let opts = if !addrs.is_empty() {
                libp2p::swarm::dial_opts::DialOpts::peer_id(peer)
                  .condition(libp2p::swarm::dial_opts::PeerCondition::Disconnected)
                  .addresses(addrs.clone())
                  .build()
              } else {
                libp2p::swarm::dial_opts::DialOpts::peer_id(peer)
                  .condition(libp2p::swarm::dial_opts::PeerCondition::Disconnected)
                  .build()
              };

              match swarm.dial(opts) {
                Err(libp2p::swarm::DialError::NoAddresses) => {
                  crate::node_log!(
                    callbacks,
                    3,
                    "Failed to discover addresses for {} (addrs.len = {})",
                    peer,
                    addrs.len()
                  );
                  notify_disconnected(callbacks, swarm, &peer);
                }
                Err(e) => {
                  crate::node_log!(callbacks, 2, "Fallback dial error for {}: {:?}", peer, e);
                  notify_disconnected(callbacks, swarm, &peer);
                }
                Ok(_) => {
                  crate::node_log!(callbacks, 4, "Fallback dial submitted for {}", peer);
                }
              }
            }
          }
        }
      }
    }
    kad::Event::OutboundQueryProgressed {
      result: kad::QueryResult::GetProviders(res),
      ..
    } => {
      if let Ok(kad::GetProvidersOk::FoundProviders { key, providers, .. }) = res {
        if key.as_ref() == b"mercury/turn_relays" {
          if callbacks.on_turn_relays_discovered != 0 {
            let mut providers_hex = Vec::new();
            for peer in providers {
              if let Some(pk) = peerid_to_pubkey(&peer) {
                providers_hex.push(hex_encode(&pk));
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
        } else {
          if callbacks.on_providers_discovered != 0 {
            let mut providers_hex = Vec::new();
            for peer in providers {
              if let Some(pk) = peerid_to_pubkey(&peer) {
                providers_hex.push(hex_encode(&pk));
              }
            }
            if !providers_hex.is_empty() {
              let joined = providers_hex.join(",");
              let cb: extern "C" fn(*mut c_void, *const c_char, *const c_char) =
                unsafe { std::mem::transmute(callbacks.on_providers_discovered) };
              if let Ok(c_str_key) = CString::new(hex_encode(key.as_ref())) {
                if let Ok(c_str_prov) = CString::new(joined) {
                  cb(
                    callbacks.context as *mut c_void,
                    c_str_key.as_ptr(),
                    c_str_prov.as_ptr(),
                  );
                }
              }
            }
          }
        }
      }
    }
    kad::Event::RoutingUpdated { peer, addresses, .. } => {
      crate::node_log!(callbacks, 5, "Kademlia RoutingUpdated for {}: {:?}", peer, addresses);
      for addr in addresses.iter() {
        let circuit_count = addr
          .iter()
          .filter(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
          .count();
        if circuit_count <= 1 && !is_bootstrap_direct_ip(addr, &state.bootstrap_addrs) {
          state.known_addresses.entry(peer).or_default().insert(addr.clone());
        }
      }
      if state.pending_dials.contains(&peer) {
        state.do_dial(peer, swarm, callbacks);
      }
    }
    e => {
      crate::node_log!(callbacks, 5, "Kademlia event: {:?}", e);
    }
  }
}

fn handle_listener_closed(
  listener_id: libp2p::core::transport::ListenerId,
  reason: Result<(), std::io::Error>,
  addresses: Vec<Multiaddr>,
  state: &mut EventLoopState,
  callbacks: &Callbacks,
) {
  crate::node_log!(callbacks, 3, "Listener closed: {:?} {:?}", addresses, reason);

  let mut original_addr = None;
  for (addr, id) in &state.active_relay_listeners {
    if *id == listener_id {
      original_addr = Some(addr.clone());
      break;
    }
  }

  state.active_relay_listeners.retain(|_, id| *id != listener_id);

  if let Some(orig) = original_addr {
    if orig
      .iter()
      .any(|p| matches!(p, libp2p::multiaddr::Protocol::P2pCircuit))
    {
      crate::node_log!(callbacks, 3, "Node lost relayed connection");
    }
  }
}

fn handle_listener_error(
  listener_id: libp2p::core::transport::ListenerId,
  error: std::io::Error,
  state: &mut EventLoopState,
  callbacks: &Callbacks,
) {
  crate::node_log!(callbacks, 2, "Listener error: {:?}", error);
  state.active_relay_listeners.retain(|_, id| *id != listener_id);
}

fn handle_chat(
  event: request_response::Event<Vec<u8>, Vec<u8>>,
  swarm: &mut Swarm<MercuryBehaviour>,
  callbacks: &Callbacks,
) {
  match event {
    request_response::Event::Message { peer, message } => {
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
        crate::node_log!(callbacks, 5, "Received Chat Response from {}", peer);
      }
    }
    request_response::Event::ResponseSent { peer, .. } => {
      crate::node_log!(callbacks, 5, "Chat response sent to {}", peer);
    }
    request_response::Event::OutboundFailure { peer, error, .. } => {
      crate::node_log!(callbacks, 2, "Chat outbound failure to {}: {:?}", peer, error);
      let _ = swarm.disconnect_peer_id(peer);
      force_notify_disconnected(callbacks, &peer);
    }
    request_response::Event::InboundFailure { peer, error, .. } => {
      crate::node_log!(callbacks, 2, "Chat inbound failure from {}: {:?}", peer, error);
    }
  }
}

fn handle_file(
  event: request_response::Event<Vec<u8>, Vec<u8>>,
  swarm: &mut Swarm<MercuryBehaviour>,
  callbacks: &Callbacks,
) {
  match event {
    request_response::Event::Message { peer, message } => {
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
        crate::node_log!(callbacks, 5, "Received File Response from {}", peer);
      }
    }
    request_response::Event::ResponseSent { peer, .. } => {
      crate::node_log!(callbacks, 5, "File response sent to {}", peer);
    }
    request_response::Event::OutboundFailure { peer, error, .. } => {
      crate::node_log!(callbacks, 2, "File outbound failure to {}: {:?}", peer, error);
      let _ = swarm.disconnect_peer_id(peer);
      force_notify_disconnected(callbacks, &peer);
    }
    request_response::Event::InboundFailure { peer, error, .. } => {
      crate::node_log!(callbacks, 2, "File inbound failure from {}: {:?}", peer, error);
    }
  }
}

fn handle_autonat(
  event: autonat::Event,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  tx: &mpsc::UnboundedSender<Command>,
  callbacks: &Callbacks,
  _role: NodeRole,
) {
  match event {
    autonat::Event::StatusChanged { old, new } => {
      crate::node_log!(callbacks, 3, "AutoNAT status changed: {:?} -> {:?}", old, new);
      if let autonat::NatStatus::Public(addr) = new {
        if is_bootstrap_direct_ip(&addr, &state.bootstrap_addrs) {
          return;
        }
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
          crate::node_log!(callbacks, 3, "Node switches off a TURN server");
        }
        state.turn_port = None;
        swarm
          .behaviour_mut()
          .kademlia
          .stop_providing(&libp2p::kad::RecordKey::new(&"mercury/turn_relays"));
      }
    }
    e => {
      crate::node_log!(callbacks, 5, "AutoNAT event: {:?}", e);
    }
  }
}

fn handle_turn_auth(
  event: request_response::Event<crate::types::TurnAuthRequest, crate::types::TurnAuthResponse>,
  state: &mut EventLoopState,
  swarm: &mut Swarm<MercuryBehaviour>,
  callbacks: &Callbacks,
) {
  match event {
    request_response::Event::Message { peer, message } => {
      if let request_response::Message::Request { channel, .. } = message {
        crate::node_log!(callbacks, 5, "Received TurnAuthRequest from {}", peer);

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
        let credential = hex_encode(&key);
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

        crate::node_log!(
          callbacks,
          4,
          "Sending TurnAuthResponse: ip={}, port={}, user={}",
          response.public_ip,
          response.port,
          response.username
        );

        let _ = swarm.behaviour_mut().turn_auth.send_response(channel, response);
      } else if let request_response::Message::Response { response, .. } = message {
        crate::node_log!(
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
            if let Ok(peer_c_str) = CString::new(hex_encode(&pk)) {
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
    request_response::Event::OutboundFailure { peer, error, .. } => {
      crate::node_log!(callbacks, 2, "[TURN] TurnAuth request to {} failed: {:?}", peer, error);
      if callbacks.on_turn_auth_result != 0 {
        if let Some(pk) = peerid_to_pubkey(&peer) {
          if let Ok(peer_c_str) = CString::new(hex_encode(&pk)) {
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
    request_response::Event::InboundFailure { peer, error, .. } => {
      crate::node_log!(callbacks, 2, "TurnAuth inbound failure from {}: {:?}", peer, error);
    }
    request_response::Event::ResponseSent { peer, .. } => {
      crate::node_log!(callbacks, 5, "TurnAuth response sent to {}", peer);
    }
  }
}

fn handle_ping(event: ping::Event, swarm: &mut Swarm<MercuryBehaviour>, callbacks: &Callbacks) {
  match event.result {
    Ok(_) => {
      crate::node_log!(callbacks, 5, "Ping success for {}", event.peer);
    }
    Err(e) => {
      crate::node_log!(callbacks, 3, "Ping failure for {}: {:?}", event.peer, e);
      let _ = swarm.disconnect_peer_id(event.peer);
      force_notify_disconnected(callbacks, &event.peer);
    }
  }
}
