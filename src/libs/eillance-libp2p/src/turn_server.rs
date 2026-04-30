use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use tokio::net::UdpSocket;

pub struct TurnAuthenticator {
  pub users: Mutex<HashMap<String, String>>,
}

impl TurnAuthenticator {
  pub fn new() -> Self {
    Self {
      users: Mutex::new(HashMap::new()),
    }
  }

  pub fn add_user(&self, username: String, password: String) {
    let mut lock = self.users.lock().unwrap();
    // In a production environment, we should clean up expired users.
    lock.insert(username, password);
  }

  #[allow(dead_code)]
  pub fn get_password(&self, username: &str) -> Option<String> {
    let lock = self.users.lock().unwrap();
    lock.get(username).cloned()
  }
}

pub async fn start_turn_server(_public_ip: String, _auth: Arc<TurnAuthenticator>) -> Result<u16, String> {
  // Bind to an ephemeral port
  let socket = UdpSocket::bind("0.0.0.0:0").await.map_err(|e| e.to_string())?;
  let port = socket.local_addr().map_err(|e| e.to_string())?.port();

  // Spawn a dummy task to keep the socket alive.
  // In full implementation, this is where `turn::server::Server::new(config)` is spawned.
  tokio::spawn(async move {
    let _socket = socket; // keep alive
    let mut buf =[0u8; 1024];
    loop {
      if let Ok(_) = _socket.recv_from(&mut buf).await {
        // Ignored. Stored safely to retain OS resources.
      }
    }
  });

  Ok(port)
}
