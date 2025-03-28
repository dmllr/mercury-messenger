// src/types/peer.ts

export interface Resolution {
    width: number; // uint16_t maps to number
    height: number; // uint16_t maps to number
}

export interface Camera {
    id: string;
    name: string;
    resolutions: Resolution[];
}

export type Profile = Camera[]; // Profile is an array of Cameras

export interface Peer {
    id: string;
    name: string;
    profile?: Profile; // Profile might be optional initially
    // Add other relevant peer properties if needed from signaling message
    kind?: 'streamer' | 'viewer'; // Might be present in 'streamers' message
}