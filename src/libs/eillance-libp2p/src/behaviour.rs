use crate::types::{NodeRole, TurnAuthRequest, TurnAuthResponse};
use libp2p::{
  PeerId, StreamProtocol, autonat, dcutr, identify,
  kad::{self, Behaviour as Kademlia, Config as KademliaConfig, store::MemoryStore},
  mdns, ping, relay,
  request_response::{self, ProtocolSupport},
  swarm::NetworkBehaviour,
};
use std::time::Duration;

#[derive(NetworkBehaviour)]
pub struct MercuryBehaviour {
  pub relay_client: relay::client::Behaviour,
  pub relay_server: relay::Behaviour,
  pub dcutr: dcutr::Behaviour,
  pub kademlia: Kademlia<MemoryStore>,
  pub chat: request_response::cbor::Behaviour<Vec<u8>, Vec<u8>>,
  pub file: request_response::cbor::Behaviour<Vec<u8>, Vec<u8>>,
  pub turn_auth: request_response::cbor::Behaviour<TurnAuthRequest, TurnAuthResponse>,
  pub autonat: autonat::Behaviour,
  pub mdns: mdns::tokio::Behaviour,
  pub identify: identify::Behaviour,
  pub ping: ping::Behaviour,
}

pub fn build_behaviour(
  key: &libp2p::identity::Keypair,
  relay_client: relay::client::Behaviour,
  role: NodeRole,
) -> MercuryBehaviour {
  let peer_id = PeerId::from(key.public());
  let mut kad_config = KademliaConfig::default();
  kad_config.set_record_ttl(Some(Duration::from_secs(24 * 60 * 60)));

  let is_bootstrap = matches!(role, NodeRole::BootstrapNode);
  if is_bootstrap {
    kad_config.set_provider_publication_interval(Some(Duration::from_secs(45 * 60)));
  } else {
    kad_config.set_provider_publication_interval(Some(Duration::from_secs(10 * 60)));
  }

  kad_config.set_replication_factor(std::num::NonZeroUsize::new(4).unwrap());
  kad_config.set_protocol_names(vec![StreamProtocol::new("/mercury/kad/1.0.0")]);

  let mut kademlia = Kademlia::with_config(peer_id, MemoryStore::new(peer_id), kad_config);

  let is_bootstrap = matches!(role, NodeRole::BootstrapNode);
  if is_bootstrap {
    kademlia.set_mode(Some(kad::Mode::Server));
  } else {
    kademlia.set_mode(Some(kad::Mode::Client));
  }

  let mut mdns_config = mdns::Config::default();
  mdns_config.query_interval = Duration::from_secs(300);
  let mdns = mdns::tokio::Behaviour::new(mdns_config, peer_id).expect("mDNS valid");

  let rr_config = request_response::Config::default().with_request_timeout(Duration::from_secs(30));

  let chat = request_response::cbor::Behaviour::new(
    [(StreamProtocol::new("/mercury/chat/1.0.0"), ProtocolSupport::Full)],
    rr_config.clone(),
  );
  let file = request_response::cbor::Behaviour::new(
    [(StreamProtocol::new("/mercury/file/1.0.0"), ProtocolSupport::Full)],
    rr_config.clone(),
  );
  let turn_auth = request_response::cbor::Behaviour::new(
    [(StreamProtocol::new("/mercury/turn_auth/1.0.0"), ProtocolSupport::Full)],
    rr_config,
  );

  let mut autonat_config = autonat::Config::default();
  autonat_config.retry_interval = Duration::from_secs(90);
  autonat_config.refresh_interval = Duration::from_secs(900);
  autonat_config.boot_delay = Duration::from_secs(15);
  let autonat = autonat::Behaviour::new(peer_id, autonat_config);

  let identify = identify::Behaviour::new(
    identify::Config::new("/mercury/id/1.0.0".into(), key.public()).with_interval(Duration::from_secs(3600)),
  );
  let ping = ping::Behaviour::new(
    ping::Config::new()
      .with_interval(Duration::from_secs(120))
      .with_timeout(Duration::from_secs(30)),
  );

  let relay_config = if is_bootstrap {
    relay::Config {
      max_reservations: 8192,
      max_reservations_per_peer: 1024,
      reservation_rate_limiters: vec![],
      max_circuits: 8192,
      max_circuits_per_peer: 1024,
      circuit_src_rate_limiters: vec![],
      max_circuit_duration: Duration::from_secs(86400 * 7), // 7 days
      max_circuit_bytes: 1_u64 << 40,                       // 1 TB
      reservation_duration: Duration::from_secs(3600),      // 1 hour
      ..Default::default()
    }
  } else {
    relay::Config {
      max_reservations: 0,
      max_reservations_per_peer: 0,
      max_circuits: 0,
      max_circuits_per_peer: 0,
      ..Default::default()
    }
  };
  let relay_server = relay::Behaviour::new(peer_id, relay_config);
  let dcutr = dcutr::Behaviour::new(peer_id);

  MercuryBehaviour {
    relay_client,
    relay_server,
    dcutr,
    kademlia,
    chat,
    file,
    turn_auth,
    autonat,
    mdns,
    identify,
    ping,
  }
}
