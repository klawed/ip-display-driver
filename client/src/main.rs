// IP Display Client - Main Application
// Copyright (c) 2024
// Licensed under MIT

use anyhow::Result;
use clap::Parser;
use gtk4::prelude::*;
use std::sync::Arc;
use tokio::sync::RwLock;
use tracing::{info, warn, error};

mod protocol;
mod ui;
mod network;
mod renderer;

use protocol::{PacketHeader, MAGIC, VERSION};
use ui::DisplayWindow;
use network::NetworkClient;

#[derive(Parser, Debug)]
#[command(name = "ip-display-client")]
#[command(about = "GTK4 client for IP Display Driver")]
struct Args {
    /// Server IP address
    #[arg(short, long, default_value = "127.0.0.1")]
    server: String,
    
    /// Server port
    #[arg(short, long, default_value = "8080")]
    port: u16,
    
    /// Start in fullscreen mode
    #[arg(short, long)]
    fullscreen: bool,
    
    /// Enable vertical sync
    #[arg(long)]
    vsync: bool,
    
    /// Window width
    #[arg(long, default_value = "1920")]
    width: i32,
    
    /// Window height
    #[arg(long, default_value = "1080")]
    height: i32,
}

#[derive(Debug, Clone)]
pub struct AppState {
    pub connected: bool,
    pub server: String,
    pub port: u16,
    pub display_width: u32,
    pub display_height: u32,
    pub fullscreen: bool,
    pub vsync: bool,
}

impl Default for AppState {
    fn default() -> Self {
        Self {
            connected: false,
            server: "127.0.0.1".to_string(),
            port: 8080,
            display_width: 1920,
            display_height: 1080,
            fullscreen: false,
            vsync: false,
        }
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize tracing
    tracing_subscriber::fmt::init();
    
    // Parse command line arguments
    let args = Args::parse();
    
    info!("Starting IP Display Client v{}", env!("CARGO_PKG_VERSION"));
    info!("Connecting to {}:{}", args.server, args.port);
    
    // Initialize GTK
    gtk4::init()?;
    
    // Create application state
    let state = Arc::new(RwLock::new(AppState {
        server: args.server.clone(),
        port: args.port,
        display_width: args.width as u32,
        display_height: args.height as u32,
        fullscreen: args.fullscreen,
        vsync: args.vsync,
        ..Default::default()
    }));
    
    // Create GTK application
    let app = gtk4::Application::builder()
        .application_id("com.ipdisp.client")
        .build();
    
    let state_clone = Arc::clone(&state);
    app.connect_activate(move |app| {
        let rt = tokio::runtime::Handle::current();
        let state = Arc::clone(&state_clone);
        
        rt.spawn(async move {
            if let Err(e) = run_app(app, state).await {
                error!("Application error: {}", e);
            }
        });
    });
    
    // Run the application
    app.run();
    
    Ok(())
}

async fn run_app(app: &gtk4::Application, state: Arc<RwLock<AppState>>) -> Result<()> {
    // Create main window
    let window = DisplayWindow::new(app, Arc::clone(&state)).await?;
    
    // Create network client
    let network_client = NetworkClient::new(Arc::clone(&state)).await?;
    
    // Connect to server
    let server_addr = {
        let state_guard = state.read().await;
        format!("{}:{}", state_guard.server, state_guard.port)
    };
    
    match network_client.connect(&server_addr).await {
        Ok(_) => {
            info!("Connected to server successfully");
            let mut state_guard = state.write().await;
            state_guard.connected = true;
        }
        Err(e) => {
            warn!("Failed to connect to server: {}", e);
            // Continue anyway - allow user to retry
        }
    }
    
    // Show window
    window.show();
    
    // Start network loop
    let window_weak = window.downgrade();
    let network_client_clone = network_client.clone();
    tokio::spawn(async move {
        if let Err(e) = network_loop(network_client_clone, window_weak).await {
            error!("Network loop error: {}", e);
        }
    });
    
    Ok(())
}

async fn network_loop(
    client: NetworkClient, 
    window: glib::WeakRef<DisplayWindow>
) -> Result<()> {
    loop {
        match client.receive_frame().await {
            Ok(Some((header, data))) => {
                // Update display
                if let Some(window) = window.upgrade() {
                    if let Err(e) = window.update_frame(&header, &data).await {
                        warn!("Failed to update frame: {}", e);
                    }
                }
            }
            Ok(None) => {
                // No data received, continue
                tokio::time::sleep(tokio::time::Duration::from_millis(16)).await;
            }
            Err(e) => {
                error!("Network error: {}", e);
                tokio::time::sleep(tokio::time::Duration::from_secs(1)).await;
            }
        }
    }
}
