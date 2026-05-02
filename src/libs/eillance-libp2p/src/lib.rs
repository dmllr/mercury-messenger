#[macro_use]
mod utils;

mod behaviour;
mod event_loop;
mod ffi;
mod node;
mod turn_server;
mod types;

#[cfg(test)]
mod tests {
  use crate::utils::*;
  use libp2p::identity;

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
