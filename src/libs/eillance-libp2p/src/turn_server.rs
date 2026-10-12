// src/libs/eillance-libp2p/src/turn_server.rs
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use tokio::net::UdpSocket;
use turn::server::{config::{ConnConfig, ServerConfig}, Server};
use turn::relay::relay_range::RelayAddressGeneratorRanges;

pub struct TurnAuthenticator {
  pub users: Mutex<HashMap<String, (String, Instant)>>,
}

impl TurnAuthenticator {
  pub fn new() -> Self {
    Self {
      users: Mutex::new(HashMap::new()),
    }
  }

  pub fn add_user(&self, username: String, password: String) {
    let mut lock = self.users.lock().unwrap();
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
  fn auth_handle(
    &self,
    username: &str,
    realm: &str,
    _src_addr: std::net::SocketAddr,
  ) -> Result<Vec<u8>, turn::Error> {
    if let Some(password) = self.auth.get_password(username) {
        Ok(turn::auth::generate_auth_key(username, realm, &password))
    } else {
        Err(std::io::Error::new(std::io::ErrorKind::PermissionDenied, "unauthorized").into())
    }
  }
}

pub struct TurnServerHandle {
    pub close_tx: tokio::sync::oneshot::Sender<()>,
}

pub async fn start_turn_server(public_ip: String, requested_port: u16, auth: Arc<TurnAuthenticator>) -> Result<(u16, TurnServerHandle), String> {
  let is_ipv6 = public_ip.contains(':');
  let bind_ip = if is_ipv6 { "::" } else { "0.0.0.0" };
  let bind_addr = format!("{}:{}", bind_ip, requested_port);
  let udp_socket = match UdpSocket::bind(&bind_addr).await {
      Ok(s) => s,
      Err(_) => UdpSocket::bind(format!("{}:0", bind_ip)).await.map_err(|e| e.to_string())?,
  };
  let port = udp_socket.local_addr().map_err(|e| e.to_string())?.port();
  let conn = Arc::new(udp_socket);

  let config = ServerConfig {
    conn_configs: vec![ConnConfig {
        conn: conn as std::sync::Arc<dyn webrtc_util::Conn + std::marker::Send + std::marker::Sync>,
        relay_addr_generator: Box::new(RelayAddressGeneratorRanges {
            relay_address: public_ip.parse().unwrap_or(std::net::IpAddr::V4(std::net::Ipv4Addr::new(0, 0, 0, 0))),
            address: bind_ip.to_string(),
            min_port: 50000,
            max_port: 50500,
            max_retries: 10,
            net: Arc::new(webrtc_util::vnet::net::Net::new(None)),
        }),
    }],
    realm: "mercury".to_string(),
    auth_handler: Arc::new(MercuryAuthHandler { auth }),
    channel_bind_timeout: std::time::Duration::from_secs(600),
    alloc_close_notify: None,
  };

  let server = Server::new(config).await.map_err(|e| e.to_string())?;
  let (close_tx, close_rx) = tokio::sync::oneshot::channel::<()>();

  tokio::spawn(async move {
     let _ = close_rx.await;
     let _ = server.close().await;
  });

  Ok((port, TurnServerHandle { close_tx }))
}
