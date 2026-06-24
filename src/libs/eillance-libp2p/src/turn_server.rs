use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::net::UdpSocket;
use turn::relay::RelayAddressGenerator;
use turn::server::{
  Server,
  config::{ConnConfig, ServerConfig},
};

pub struct TurnAuthenticator {
  pub users: Mutex<HashMap<String, (String, Instant)>>,
  pub callbacks: crate::types::Callbacks,
}

impl TurnAuthenticator {
  pub fn new(callbacks: crate::types::Callbacks) -> Self {
    Self {
      users: Mutex::new(HashMap::new()),
      callbacks,
    }
  }

  pub fn add_user(&self, username: String, password: String) {
    let mut lock = self.users.lock().unwrap();
    crate::node_log!(&self.callbacks, 4, "[TURN] Adding authorized user: {}", username);
    lock.insert(username, (password, Instant::now()));

    // Clean up old users (> 24 hours) to prevent memory leaks over time
    lock.retain(|_, (_, time)| time.elapsed() < Duration::from_secs(86400));
  }

  #[allow(dead_code)]
  pub fn get_password(&self, username: &str) -> Option<String> {
    let lock = self.users.lock().unwrap();
    lock.get(username).map(|(pw, _)| pw.clone())
  }
}

struct MercuryAuthHandler {
  auth: Arc<TurnAuthenticator>,
}

impl turn::auth::AuthHandler for MercuryAuthHandler {
  fn auth_handle(&self, username: &str, realm: &str, src_addr: std::net::SocketAddr) -> Result<Vec<u8>, turn::Error> {
    {
      let mut lock = self.auth.users.lock().unwrap();
      lock.retain(|_, (_, time)| time.elapsed() < Duration::from_secs(86400));
    }
    crate::node_log!(
      &self.auth.callbacks,
      5,
      "[TURN] Auth handle requested for user: {} from {}",
      username,
      src_addr
    );
    if let Some(password) = self.auth.get_password(username) {
      crate::node_log!(
        &self.auth.callbacks,
        4,
        "[TURN] Auth request SUCCESS for user: {} from {}",
        username,
        src_addr
      );
      Ok(turn::auth::generate_auth_key(username, realm, &password))
    } else {
      crate::node_log!(
        &self.auth.callbacks,
        2,
        "[TURN] Auth request FAILED for user: {} from {}",
        username,
        src_addr
      );
      Err(std::io::Error::new(std::io::ErrorKind::PermissionDenied, "unauthorized").into())
    }
  }
}

struct MercuryRelayAddressGenerator {
  relay_address: std::net::IpAddr,
  min_port: u16,
  max_port: u16,
  callbacks: crate::types::Callbacks,
}

#[async_trait::async_trait]
impl RelayAddressGenerator for MercuryRelayAddressGenerator {
  fn validate(&self) -> Result<(), turn::Error> {
    Ok(())
  }

  async fn allocate_conn(
    &self,
    use_ipv4: bool,
    requested_port: u16,
  ) -> Result<(Arc<dyn webrtc_util::Conn + Send + Sync>, std::net::SocketAddr), turn::Error> {
    let bind_ip = if use_ipv4 { "0.0.0.0" } else { "::" };
    let mut port = requested_port;
    if port == 0 {
      port = self.min_port + (rand::random::<u16>() % (self.max_port - self.min_port + 1));
    }

    crate::node_log!(
      &self.callbacks,
      5,
      "[TURN] Allocating relay connection: use_ipv4={}, requested_port={}",
      use_ipv4,
      requested_port
    );

    let mut attempts = 0;
    loop {
      let bind_addr = format!("{}:{}", bind_ip, port);
      if let Ok(socket) = UdpSocket::bind(&bind_addr).await {
        if let Ok(local_addr) = socket.local_addr() {
          let relay_addr = std::net::SocketAddr::new(self.relay_address, local_addr.port());
          crate::node_log!(
            &self.callbacks,
            4,
            "[TURN] Allocated relay connection on {} mapped to {}",
            local_addr,
            relay_addr
          );
          return Ok((Arc::new(socket), relay_addr));
        }
      }

      attempts += 1;
      if attempts >= 10 {
        crate::node_log!(
          &self.callbacks,
          1,
          "[TURN] Failed to allocate relay connection after 10 attempts"
        );
        return Err(std::io::Error::new(std::io::ErrorKind::Other, "max retries exceeded").into());
      }

      port = self.min_port + (rand::random::<u16>() % (self.max_port - self.min_port + 1));
    }
  }
}

pub struct TurnServerHandle {
  pub close_tx: tokio::sync::oneshot::Sender<()>,
}

pub async fn start_turn_server(
  public_ip: String,
  requested_port: u16,
  auth: Arc<TurnAuthenticator>,
) -> Result<(u16, TurnServerHandle), String> {
  let is_ipv6 = public_ip.contains(':');
  let bind_ip = if is_ipv6 { "::" } else { "0.0.0.0" };
  let bind_addr = format!("{}:{}", bind_ip, requested_port);
  crate::node_log!(
    &auth.callbacks,
    3,
    "[TURN] Attempting to bind TURN server to {}",
    bind_addr
  );
  let udp_socket = match UdpSocket::bind(&bind_addr).await {
    Ok(s) => s,
    Err(e) => {
      crate::node_log!(&auth.callbacks, 2, "[TURN] Failed to bind to {}: {}", bind_addr, e);
      UdpSocket::bind(format!("{}:0", bind_ip))
        .await
        .map_err(|err| err.to_string())?
    }
  };
  let port = udp_socket.local_addr().map_err(|e| e.to_string())?.port();
  crate::node_log!(
    &auth.callbacks,
    3,
    "[TURN] Successfully bound TURN server to {}:{}",
    bind_ip,
    port
  );
  let conn = Arc::new(udp_socket);

  let parsed_public_ip = public_ip.parse().unwrap_or_else(|_| {
    if is_ipv6 {
      std::net::IpAddr::V6(std::net::Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0))
    } else {
      std::net::IpAddr::V4(std::net::Ipv4Addr::new(0, 0, 0, 0))
    }
  });

  let callbacks_for_gen = auth.callbacks.clone();

  let config = ServerConfig {
    conn_configs: vec![ConnConfig {
      conn: conn as std::sync::Arc<dyn webrtc_util::Conn + std::marker::Send + std::marker::Sync>,
      relay_addr_generator: Box::new(MercuryRelayAddressGenerator {
        relay_address: parsed_public_ip,
        min_port: 50000,
        max_port: 50500,
        callbacks: callbacks_for_gen,
      }),
    }],
    realm: "mercury".to_string(),
    auth_handler: Arc::new(MercuryAuthHandler { auth: auth.clone() }),
    channel_bind_timeout: std::time::Duration::from_secs(600),
    alloc_close_notify: None,
  };

  crate::node_log!(
    &auth.callbacks,
    3,
    "[TURN] Starting server with realm: mercury, public IP: {}",
    parsed_public_ip
  );

  let server = Server::new(config).await.map_err(|e| {
    crate::node_log!(&auth.callbacks, 1, "[TURN] Failed to start TURN server: {}", e);
    e.to_string()
  })?;
  let (close_tx, close_rx) = tokio::sync::oneshot::channel::<()>();

  tokio::spawn(async move {
    let _ = close_rx.await;
    crate::node_log!(&auth.callbacks, 3, "[TURN] Shutting down TURN server");
    let _ = server.close().await;
  });

  Ok((port, TurnServerHandle { close_tx }))
}
