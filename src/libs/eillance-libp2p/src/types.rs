use libp2p::{Multiaddr, PeerId};
use serde::{Deserialize, Serialize};

#[repr(i32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NodeRole {
  BootstrapNode = 0,
  DesktopClient = 1,
  MobileClient = 2,
  Bot = 3,
}

impl From<i32> for NodeRole {
  fn from(v: i32) -> Self {
    match v {
      0 => NodeRole::BootstrapNode,
      1 => NodeRole::DesktopClient,
      2 => NodeRole::MobileClient,
      3 => NodeRole::Bot,
      _ => NodeRole::DesktopClient,
    }
  }
}

#[derive(Clone, Default)]
pub struct Callbacks {
  pub context: usize,
  pub on_connected: usize,
  pub on_disconnected: usize,
  pub on_message: usize,
  pub on_log: usize,
  pub on_turn_relays_discovered: usize,
  pub on_turn_auth_result: usize,
  pub on_network_stats: usize,
  pub on_providers_discovered: usize,
}
unsafe impl Send for Callbacks {}
unsafe impl Sync for Callbacks {}

pub enum Command {
  Dial(PeerId),
  AddBootstrap(Multiaddr),
  AddBootstrapResolved(Multiaddr),
  Listen(Multiaddr),
  SendChat(PeerId, Vec<u8>),
  SendFile(PeerId, Vec<u8>),
  Disconnect(PeerId),
  Reconnect,
  Stop,
  TurnServerStarted {
    port: u16,
    public_ip: String,
    handle: crate::turn_server::TurnServerHandle,
  },
  DiscoverTurnRelays,
  AuthTurnRelay(PeerId),
  SetTurnEligibility(bool),
  StartProviding(Vec<u8>),
  GetProviders(Vec<u8>),
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TurnAuthRequest {
  pub dummy: u8,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TurnAuthResponse {
  pub username: String,
  pub credential: String,
  pub port: u16,
  pub public_ip: String,
}
