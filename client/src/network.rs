// IP Display Client - Network Implementation
// Copyright (c) 2024
// Licensed under MIT

use anyhow::Result;
use std::sync::Arc;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::sync::RwLock;
use tracing::{debug, info, warn, error};

use crate::protocol::{PacketHeader, FrameData, HEADER_SIZE};
use crate::AppState;

#[derive(Debug, Clone)]
pub struct NetworkClient {
    state: Arc<RwLock<AppState>>,
    connection: Arc<RwLock<Option<TcpStream>>>,
}

impl NetworkClient {
    pub async fn new(state: Arc<RwLock<AppState>>) -> Result<Self> {
        Ok(Self {
            state,
            connection: Arc::new(RwLock::new(None)),
        })
    }
    
    pub async fn connect(&self, addr: &str) -> Result<()> {
        info!("Connecting to {}", addr);
        
        let stream = TcpStream::connect(addr).await?;
        debug!("TCP connection established");
        
        // Store connection
        {
            let mut conn = self.connection.write().await;
            *conn = Some(stream);
        }
        
        // Update state
        {
            let mut state = self.state.write().await;
            state.connected = true;
        }
        
        info!("Successfully connected to server");
        Ok(())
    }
    
    pub async fn disconnect(&self) -> Result<()> {
        info!("Disconnecting from server");
        
        // Close connection
        {
            let mut conn = self.connection.write().await;
            if let Some(mut stream) = conn.take() {
                let _ = stream.shutdown().await;
            }
        }
        
        // Update state
        {
            let mut state = self.state.write().await;
            state.connected = false;
        }
        
        info!("Disconnected from server");
        Ok(())
    }
    
    pub async fn is_connected(&self) -> bool {
        let conn = self.connection.read().await;
        conn.is_some()
    }
    
    pub async fn receive_frame(&self) -> Result<Option<(PacketHeader, Vec<u8>)>> {
        let mut conn = self.connection.write().await;
        let stream = match conn.as_mut() {
            Some(s) => s,
            None => return Ok(None),
        };
        
        // Read header
        let mut header_buf = vec![0u8; HEADER_SIZE];
        match stream.read_exact(&mut header_buf).await {
            Ok(()) => {}
            Err(e) if e.kind() == tokio::io::ErrorKind::UnexpectedEof => {
                warn!("Connection closed by server");
                *conn = None;
                return Ok(None);
            }
            Err(e) => {
                error!("Failed to read header: {}", e);
                *conn = None;
                return Err(e.into());
            }
        }
        
        // Parse header
        let header = match PacketHeader::from_bytes(&header_buf) {
            Ok(h) => h,
            Err(e) => {
                error!("Invalid packet header: {}", e);
                return Err(e);
            }
        };
        
        debug!("Received header: {}x{} format={:?} size={}", 
               header.width, header.height, header.format, header.size);
        
        // Validate header
        if let Err(e) = header.validate() {
            error!("Header validation failed: {}", e);
            return Err(e);
        }
        
        // Handle info packets (no data payload)
        if header.is_info_packet() {
            info!("Received display info: {}x{}", header.width, header.height);
            
            // Update display dimensions in state
            {
                let mut state = self.state.write().await;
                state.display_width = header.width;
                state.display_height = header.height;
            }
            
            return Ok(Some((header, Vec::new())));
        }
        
        // Read frame data
        let mut data = vec![0u8; header.size as usize];
        match stream.read_exact(&mut data).await {
            Ok(()) => {}
            Err(e) if e.kind() == tokio::io::ErrorKind::UnexpectedEof => {
                warn!("Connection closed while reading frame data");
                *conn = None;
                return Ok(None);
            }
            Err(e) => {
                error!("Failed to read frame data: {}", e);
                *conn = None;
                return Err(e.into());
            }
        }
        
        debug!("Received frame data: {} bytes", data.len());
        
        // Validate frame data
        let frame = FrameData::new(header.clone(), data.clone())?;
        if let Err(e) = frame.validate() {
            error!("Frame validation failed: {}", e);
            return Err(e);
        }
        
        Ok(Some((header, data)))
    }
    
    pub async fn send_command(&self, command: &[u8]) -> Result<()> {
        let mut conn = self.connection.write().await;
        let stream = match conn.as_mut() {
            Some(s) => s,
            None => return Err(anyhow::anyhow!("Not connected")),
        };
        
        stream.write_all(command).await?;
        stream.flush().await?;
        
        Ok(())
    }
}

impl Drop for NetworkClient {
    fn drop(&mut self) {
        // Note: We can't use async in Drop, but the connection will be closed
        // when the TcpStream is dropped
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::AppState;
    
    #[tokio::test]
    async fn test_network_client_creation() {
        let state = Arc::new(RwLock::new(AppState::default()));
        let client = NetworkClient::new(state).await.unwrap();
        
        assert!(!client.is_connected().await);
    }
}
