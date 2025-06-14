// IP Display Client - Protocol Implementation
// Copyright (c) 2024
// Licensed under MIT

use anyhow::Result;
use bytes::{Buf, BufMut, BytesMut};
use serde::{Deserialize, Serialize};
use std::convert::TryFrom;

// Protocol constants
pub const MAGIC: u32 = 0x49504453; // "IPDS"
pub const VERSION: u32 = 1;
pub const HEADER_SIZE: usize = 32;

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub enum FrameFormat {
    Rgba32 = 0,
    Rgb24 = 1,
    H264 = 2,
    H265 = 3,
}

impl TryFrom<u32> for FrameFormat {
    type Error = anyhow::Error;
    
    fn try_from(value: u32) -> Result<Self> {
        match value {
            0 => Ok(FrameFormat::Rgba32),
            1 => Ok(FrameFormat::Rgb24),
            2 => Ok(FrameFormat::H264),
            3 => Ok(FrameFormat::H265),
            _ => Err(anyhow::anyhow!("Invalid frame format: {}", value)),
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PacketHeader {
    pub magic: u32,
    pub version: u32,
    pub width: u32,
    pub height: u32,
    pub format: FrameFormat,
    pub timestamp: u64,
    pub size: u32,
    pub reserved: u32,
}

impl PacketHeader {
    pub fn new(width: u32, height: u32, format: FrameFormat, size: u32) -> Self {
        Self {
            magic: MAGIC,
            version: VERSION,
            width,
            height,
            format,
            timestamp: std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos() as u64,
            size,
            reserved: 0,
        }
    }
    
    pub fn from_bytes(data: &[u8]) -> Result<Self> {
        if data.len() < HEADER_SIZE {
            return Err(anyhow::anyhow!("Header too short: {} bytes", data.len()));
        }
        
        let mut buf = &data[..HEADER_SIZE];
        
        let magic = buf.get_u32();
        let version = buf.get_u32();
        let width = buf.get_u32();
        let height = buf.get_u32();
        let format_raw = buf.get_u32();
        let timestamp = buf.get_u64();
        let size = buf.get_u32();
        let reserved = buf.get_u32();
        
        if magic != MAGIC {
            return Err(anyhow::anyhow!("Invalid magic number: 0x{:08x}", magic));
        }
        
        if version != VERSION {
            return Err(anyhow::anyhow!("Unsupported version: {}", version));
        }
        
        let format = FrameFormat::try_from(format_raw)?;
        
        Ok(Self {
            magic,
            version,
            width,
            height,
            format,
            timestamp,
            size,
            reserved,
        })
    }
    
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut buf = BytesMut::with_capacity(HEADER_SIZE);
        
        buf.put_u32(self.magic);
        buf.put_u32(self.version);
        buf.put_u32(self.width);
        buf.put_u32(self.height);
        buf.put_u32(self.format as u32);
        buf.put_u64(self.timestamp);
        buf.put_u32(self.size);
        buf.put_u32(self.reserved);
        
        buf.to_vec()
    }
    
    pub fn is_info_packet(&self) -> bool {
        self.size == 0
    }
    
    pub fn validate(&self) -> Result<()> {
        if self.magic != MAGIC {
            return Err(anyhow::anyhow!("Invalid magic number"));
        }
        
        if self.version != VERSION {
            return Err(anyhow::anyhow!("Unsupported version"));
        }
        
        if self.width == 0 || self.height == 0 {
            return Err(anyhow::anyhow!("Invalid dimensions: {}x{}", self.width, self.height));
        }
        
        if self.width > 7680 || self.height > 4320 {
            return Err(anyhow::anyhow!("Dimensions too large: {}x{}", self.width, self.height));
        }
        
        Ok(())
    }
}

#[derive(Debug, Clone)]
pub struct FrameData {
    pub header: PacketHeader,
    pub data: Vec<u8>,
}

impl FrameData {
    pub fn new(header: PacketHeader, data: Vec<u8>) -> Result<Self> {
        if data.len() != header.size as usize {
            return Err(anyhow::anyhow!(
                "Data size mismatch: expected {}, got {}", 
                header.size, data.len()
            ));
        }
        
        Ok(Self { header, data })
    }
    
    pub fn expected_size(&self) -> usize {
        match self.header.format {
            FrameFormat::Rgba32 => (self.header.width * self.header.height * 4) as usize,
            FrameFormat::Rgb24 => (self.header.width * self.header.height * 3) as usize,
            FrameFormat::H264 | FrameFormat::H265 => self.data.len(),
        }
    }
    
    pub fn validate(&self) -> Result<()> {
        self.header.validate()?;
        
        if !self.header.is_info_packet() {
            let expected = self.expected_size();
            if self.data.len() != expected && 
               matches!(self.header.format, FrameFormat::Rgba32 | FrameFormat::Rgb24) {
                return Err(anyhow::anyhow!(
                    "Invalid data size for format {:?}: expected {}, got {}",
                    self.header.format, expected, self.data.len()
                ));
            }
        }
        
        Ok(())
    }
    
    pub fn to_rgba32(&self) -> Result<Vec<u8>> {
        match self.header.format {
            FrameFormat::Rgba32 => Ok(self.data.clone()),
            FrameFormat::Rgb24 => {
                let mut rgba_data = Vec::with_capacity(self.data.len() * 4 / 3);
                for chunk in self.data.chunks_exact(3) {
                    rgba_data.extend_from_slice(&[chunk[0], chunk[1], chunk[2], 255]);
                }
                Ok(rgba_data)
            }
            FrameFormat::H264 | FrameFormat::H265 => {
                Err(anyhow::anyhow!("Codec formats not yet supported"))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    
    #[test]
    fn test_header_serialization() {
        let header = PacketHeader::new(1920, 1080, FrameFormat::Rgba32, 1024);
        let bytes = header.to_bytes();
        let parsed = PacketHeader::from_bytes(&bytes).unwrap();
        
        assert_eq!(header.magic, parsed.magic);
        assert_eq!(header.width, parsed.width);
        assert_eq!(header.height, parsed.height);
        assert_eq!(header.format, parsed.format);
        assert_eq!(header.size, parsed.size);
    }
    
    #[test]
    fn test_frame_validation() {
        let header = PacketHeader::new(1920, 1080, FrameFormat::Rgba32, 1920 * 1080 * 4);
        let data = vec![0u8; 1920 * 1080 * 4];
        let frame = FrameData::new(header, data).unwrap();
        
        assert!(frame.validate().is_ok());
    }
    
    #[test]
    fn test_rgb24_to_rgba32() {
        let header = PacketHeader::new(2, 2, FrameFormat::Rgb24, 12);
        let data = vec![255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255];
        let frame = FrameData::new(header, data).unwrap();
        
        let rgba = frame.to_rgba32().unwrap();
        assert_eq!(rgba.len(), 16);
        assert_eq!(rgba[0..4], [255, 0, 0, 255]);
        assert_eq!(rgba[4..8], [0, 255, 0, 255]);
    }
}
