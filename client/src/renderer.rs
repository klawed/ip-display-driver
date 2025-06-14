// IP Display Client - Frame Renderer
// Copyright (c) 2024
// Licensed under MIT

use anyhow::Result;
use cairo::{ImageSurface, Format};
use std::sync::{Arc, Mutex};
use tracing::{debug, error};

#[derive(Debug)]
pub struct FrameRenderer {
    surface: Arc<Mutex<Option<ImageSurface>>>,
    width: Arc<Mutex<u32>>,
    height: Arc<Mutex<u32>>,
}

impl FrameRenderer {
    pub fn new() -> Result<Self> {
        Ok(Self {
            surface: Arc::new(Mutex::new(None)),
            width: Arc::new(Mutex::new(0)),
            height: Arc::new(Mutex::new(0)),
        })
    }
    
    pub fn update_frame(&self, width: u32, height: u32, rgba_data: &[u8]) -> Result<()> {
        debug!("Updating frame: {}x{} with {} bytes", width, height, rgba_data.len());
        
        let expected_size = (width * height * 4) as usize;
        if rgba_data.len() != expected_size {
            return Err(anyhow::anyhow!(
                "Invalid data size: expected {}, got {}",
                expected_size, rgba_data.len()
            ));
        }
        
        // Create Cairo surface from RGBA data
        let surface = self.create_surface_from_rgba(width, height, rgba_data)?;
        
        // Update stored surface
        {
            let mut surf_guard = self.surface.lock().unwrap();
            *surf_guard = Some(surface);
        }
        
        // Update dimensions
        {
            let mut width_guard = self.width.lock().unwrap();
            *width_guard = width;
        }
        {
            let mut height_guard = self.height.lock().unwrap();
            *height_guard = height;
        }
        
        debug!("Frame updated successfully");
        Ok(())
    }
    
    pub fn get_surface(&self) -> Option<ImageSurface> {
        let surf_guard = self.surface.lock().unwrap();
        surf_guard.clone()
    }
    
    pub fn get_dimensions(&self) -> (u32, u32) {
        let width = *self.width.lock().unwrap();
        let height = *self.height.lock().unwrap();
        (width, height)
    }
    
    fn create_surface_from_rgba(&self, width: u32, height: u32, rgba_data: &[u8]) -> Result<ImageSurface> {
        // Convert RGBA to Cairo's ARGB32 format
        let mut argb_data = Vec::with_capacity(rgba_data.len());
        
        for chunk in rgba_data.chunks_exact(4) {
            let r = chunk[0];
            let g = chunk[1];
            let b = chunk[2];
            let a = chunk[3];
            
            // Cairo uses premultiplied alpha in ARGB32 format
            // and expects BGRA byte order on little-endian systems
            let alpha_f = a as f32 / 255.0;
            let r_pre = ((r as f32 * alpha_f) as u8).min(a);
            let g_pre = ((g as f32 * alpha_f) as u8).min(a);
            let b_pre = ((b as f32 * alpha_f) as u8).min(a);
            
            // BGRA order for little-endian
            argb_data.push(b_pre);
            argb_data.push(g_pre);
            argb_data.push(r_pre);
            argb_data.push(a);
        }
        
        // Create Cairo image surface
        let surface = ImageSurface::create_for_data(
            argb_data,
            Format::ARgb32,
            width as i32,
            height as i32,
            width as i32 * 4,
        )?;
        
        Ok(surface)
    }
    
    pub fn clear(&self) {
        let mut surf_guard = self.surface.lock().unwrap();
        *surf_guard = None;
        
        let mut width_guard = self.width.lock().unwrap();
        *width_guard = 0;
        
        let mut height_guard = self.height.lock().unwrap();
        *height_guard = 0;
    }
    
    pub fn create_test_pattern(&self, width: u32, height: u32) -> Result<()> {
        debug!("Creating test pattern: {}x{}", width, height);
        
        // Create test pattern data
        let mut rgba_data = Vec::with_capacity((width * height * 4) as usize);
        
        for y in 0..height {
            for x in 0..width {
                let r = ((x * 255) / width) as u8;
                let g = ((y * 255) / height) as u8;
                let b = ((x + y) * 255 / (width + height)) as u8;
                let a = 255u8;
                
                rgba_data.extend_from_slice(&[r, g, b, a]);
            }
        }
        
        self.update_frame(width, height, &rgba_data)
    }
}

impl Clone for FrameRenderer {
    fn clone(&self) -> Self {
        Self {
            surface: Arc::clone(&self.surface),
            width: Arc::clone(&self.width),
            height: Arc::clone(&self.height),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_renderer_creation() {
        let renderer = FrameRenderer::new().unwrap();
        let (width, height) = renderer.get_dimensions();
        assert_eq!(width, 0);
        assert_eq!(height, 0);
        assert!(renderer.get_surface().is_none());
    }
    
    #[test]
    fn test_frame_update() {
        let renderer = FrameRenderer::new().unwrap();
        let width = 2;
        let height = 2;
        let rgba_data = vec![
            255, 0, 0, 255,    // Red
            0, 255, 0, 255,    // Green
            0, 0, 255, 255,    // Blue
            255, 255, 255, 255 // White
        ];
        
        renderer.update_frame(width, height, &rgba_data).unwrap();
        
        let (w, h) = renderer.get_dimensions();
        assert_eq!(w, width);
        assert_eq!(h, height);
        assert!(renderer.get_surface().is_some());
    }
    
    #[test]
    fn test_test_pattern() {
        let renderer = FrameRenderer::new().unwrap();
        renderer.create_test_pattern(16, 16).unwrap();
        
        let (width, height) = renderer.get_dimensions();
        assert_eq!(width, 16);
        assert_eq!(height, 16);
        assert!(renderer.get_surface().is_some());
    }
}
