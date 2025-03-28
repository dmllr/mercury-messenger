// src/types/config.ts

export interface VideoConfig {
    cameraId?: number; // Use number, corresponds to index
    width?: number;
    height?: number;
    frameRate?: number;
    bitrate?: number;
}

export interface AudioConfig {
    channels?: number; // Example property
    // Add other audio properties if needed
}

export interface Config {
    video: VideoConfig;
    audio: AudioConfig;
}