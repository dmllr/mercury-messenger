// src/types/webrtc.ts

// You can define specific enums or types related to WebRTC state handling here
// if the standard RTCPeerConnectionState etc. aren't sufficient or need mapping.

// Example: Map native states to custom app states if necessary
export enum AppConnectionState {
    Idle = 'Idle',
    Connecting = 'Connecting',
    Connected = 'Connected',
    Disconnected = 'Disconnected',
    Failed = 'Failed',
    Closed = 'Closed'
}