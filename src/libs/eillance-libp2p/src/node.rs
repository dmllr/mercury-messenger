use crate::types::{Callbacks, Command, NodeRole};
use libp2p::identity;
use tokio::runtime::Runtime;
use tokio::sync::mpsc;

pub struct Node {
  pub runtime: Runtime,
  pub command_tx: Option<mpsc::UnboundedSender<Command>>,
  pub keypair: identity::Keypair,
  pub role: NodeRole,
  pub callbacks: Callbacks,
  pub turn_port: Option<u16>,
  pub turn_port_range: Option<(u16, u16)>,
}
