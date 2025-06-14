// IP Display Client - UI Implementation
// Copyright (c) 2024
// Licensed under MIT

use anyhow::Result;
use gdk4::prelude::*;
use gdk_pixbuf::Pixbuf;
use gtk4::prelude::*;
use std::sync::Arc;
use tokio::sync::RwLock;
use tracing::{debug, info, warn, error};

use crate::protocol::{PacketHeader, FrameFormat};
use crate::renderer::FrameRenderer;
use crate::AppState;

#[derive(Debug)]
pub struct DisplayWindow {
    window: gtk4::ApplicationWindow,
    drawing_area: gtk4::DrawingArea,
    status_bar: gtk4::Statusbar,
    menu_bar: gtk4::MenuBar,
    state: Arc<RwLock<AppState>>,
    renderer: FrameRenderer,
    context_id: u32,
}

impl DisplayWindow {
    pub async fn new(app: &gtk4::Application, state: Arc<RwLock<AppState>>) -> Result<Arc<Self>> {
        let window = gtk4::ApplicationWindow::builder()
            .application(app)
            .title("IP Display Client")
            .default_width(800)
            .default_height(600)
            .build();
        
        // Create main container
        let vbox = gtk4::Box::new(gtk4::Orientation::Vertical, 0);
        window.set_child(Some(&vbox));
        
        // Create menu bar
        let menu_bar = Self::create_menu_bar(&window);
        vbox.append(&menu_bar);
        
        // Create drawing area
        let drawing_area = gtk4::DrawingArea::new();
        drawing_area.set_hexpand(true);
        drawing_area.set_vexpand(true);
        
        // Set initial size
        {
            let state_guard = state.read().await;
            drawing_area.set_size_request(
                state_guard.display_width as i32,
                state_guard.display_height as i32,
            );
        }
        
        vbox.append(&drawing_area);
        
        // Create status bar
        let status_bar = gtk4::Statusbar::new();
        let context_id = status_bar.context_id("main");
        status_bar.push(context_id, "Ready");
        vbox.append(&status_bar);
        
        // Create renderer
        let renderer = FrameRenderer::new()?;
        
        let display_window = Arc::new(Self {
            window,
            drawing_area,
            status_bar,
            menu_bar,
            state: Arc::clone(&state),
            renderer,
            context_id,
        });
        
        // Setup drawing area callbacks
        let window_weak = Arc::downgrade(&display_window);
        display_window.drawing_area.set_draw_func(move |_, context, width, height| {
            if let Some(window) = window_weak.upgrade() {
                if let Err(e) = window.on_draw(context, width, height) {
                    error!("Draw error: {}", e);
                }
            }
        });
        
        // Setup window callbacks
        let window_weak = Arc::downgrade(&display_window);
        display_window.window.connect_close_request(move |_| {
            if let Some(window) = window_weak.upgrade() {
                window.on_close_request()
            } else {
                glib::Propagation::Proceed
            }
        });
        
        // Setup fullscreen toggle
        let window_weak = Arc::downgrade(&display_window);
        display_window.window.connect_key_pressed(move |_, key, _, _| {
            if let Some(window) = window_weak.upgrade() {
                window.on_key_pressed(key)
            } else {
                glib::Propagation::Proceed
            }
        });
        
        Ok(display_window)
    }
    
    fn create_menu_bar(window: &gtk4::ApplicationWindow) -> gtk4::MenuBar {
        let menu_bar = gtk4::MenuBar::new();
        
        // File menu
        let file_menu = gio::Menu::new();
        file_menu.append(Some("Connect"), Some("app.connect"));
        file_menu.append(Some("Disconnect"), Some("app.disconnect"));
        file_menu.append(Some("Quit"), Some("app.quit"));
        
        // View menu
        let view_menu = gio::Menu::new();
        view_menu.append(Some("Fullscreen"), Some("app.fullscreen"));
        view_menu.append(Some("Fit to Window"), Some("app.fit"));
        view_menu.append(Some("Actual Size"), Some("app.actual-size"));
        
        // Help menu
        let help_menu = gio::Menu::new();
        help_menu.append(Some("About"), Some("app.about"));
        
        // Add menus to menu bar
        menu_bar.append_submenu(Some("File"), &file_menu);
        menu_bar.append_submenu(Some("View"), &view_menu);
        menu_bar.append_submenu(Some("Help"), &help_menu);
        
        menu_bar
    }
    
    pub fn show(&self) {
        self.window.present();
    }
    
    pub fn downgrade(&self) -> glib::WeakRef<Self> {
        // Note: This is a simplified implementation
        // In a real application, you'd want to use proper weak references
        glib::WeakRef::new()
    }
    
    pub async fn update_frame(&self, header: &PacketHeader, data: &[u8]) -> Result<()> {
        debug!("Updating frame: {}x{} {} bytes", header.width, header.height, data.len());
        
        // Convert frame data to displayable format
        let rgba_data = match header.format {
            FrameFormat::Rgba32 => data.to_vec(),
            FrameFormat::Rgb24 => {
                let mut rgba = Vec::with_capacity(data.len() * 4 / 3);
                for chunk in data.chunks_exact(3) {
                    rgba.extend_from_slice(&[chunk[0], chunk[1], chunk[2], 255]);
                }
                rgba
            }
            FrameFormat::H264 | FrameFormat::H265 => {
                warn!("Codec formats not yet supported");
                return Ok(());
            }
        };
        
        // Update renderer
        self.renderer.update_frame(header.width, header.height, &rgba_data)?;
        
        // Update status
        let status = format!("Frame: {}x{} - {} bytes", header.width, header.height, data.len());
        self.status_bar.push(self.context_id, &status);
        
        // Trigger redraw
        self.drawing_area.queue_draw();
        
        Ok(())
    }
    
    fn on_draw(&self, context: &cairo::Context, width: i32, height: i32) -> Result<()> {
        // Clear background
        context.set_source_rgb(0.0, 0.0, 0.0);
        context.paint()?;
        
        // Draw frame if available
        if let Some(surface) = self.renderer.get_surface() {
            let surface_width = surface.width() as f64;
            let surface_height = surface.height() as f64;
            
            // Calculate scaling to fit in window
            let scale_x = width as f64 / surface_width;
            let scale_y = height as f64 / surface_height;
            let scale = scale_x.min(scale_y);
            
            // Center the image
            let x = (width as f64 - surface_width * scale) / 2.0;
            let y = (height as f64 - surface_height * scale) / 2.0;
            
            context.save()?;
            context.translate(x, y);
            context.scale(scale, scale);
            context.set_source_surface(&surface, 0.0, 0.0)?;
            context.paint()?;
            context.restore()?;
        } else {
            // Draw placeholder text
            context.set_source_rgb(0.5, 0.5, 0.5);
            context.select_font_face("Arial", cairo::FontSlant::Normal, cairo::FontWeight::Normal);
            context.set_font_size(24.0);
            
            let text = "Waiting for connection...";
            let text_extents = context.text_extents(text)?;
            let x = (width as f64 - text_extents.width()) / 2.0;
            let y = (height as f64 + text_extents.height()) / 2.0;
            
            context.move_to(x, y);
            context.show_text(text)?;
        }
        
        Ok(())
    }
    
    fn on_close_request(&self) -> glib::Propagation {
        info!("Close request received");
        glib::Propagation::Proceed
    }
    
    fn on_key_pressed(&self, key: gdk4::Key) -> glib::Propagation {
        match key {
            gdk4::Key::F11 => {
                // Toggle fullscreen
                if self.window.is_fullscreen() {
                    self.window.unfullscreen();
                } else {
                    self.window.fullscreen();
                }
                glib::Propagation::Stop
            }
            gdk4::Key::Escape => {
                if self.window.is_fullscreen() {
                    self.window.unfullscreen();
                    glib::Propagation::Stop
                } else {
                    glib::Propagation::Proceed
                }
            }
            _ => glib::Propagation::Proceed,
        }
    }
    
    pub async fn set_status(&self, message: &str) {
        self.status_bar.push(self.context_id, message);
    }
    
    pub async fn set_connected(&self, connected: bool) {
        let status = if connected {
            "Connected"
        } else {
            "Disconnected"
        };
        self.set_status(status).await;
    }
}
