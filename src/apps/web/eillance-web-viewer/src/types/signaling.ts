// src/types/signaling.ts
import { Config } from './config';
import { Peer, Profile } from './peer';

// Matches C++ ServiceConnectionState enum
export enum ServiceConnectionState {
    Unknown = 0,
    Establishing = 1,
    Online = 2,
    Failure = 3,
    Disconnected = 4, // Added state for explicit disconnect
}

// Matches C++ EventSource - adapt as needed for web context
export enum EventSource {
    System = 0,
    PeerConnectionState = 1,
    IceState = 2,
    GatheringState = 3,
    SignalingState = 4,
    DataChannel = 5, // Added for data channel events
    Ping = 6, // Added for ping events
}


// --- Message Types ---

// System messages (usually from server, no 'from' or targeted 'to')
export enum SystemMessageType {
    PUBLISH = "publish", // Client -> Server
    STREAMERS = "streamers", // Server -> Client
    ERROR = "error", // Server -> Client
    PEER_DISCONNECTED = "peer_disconnected", // Server -> Client
}

// Peer messages (have 'from' and 'to')
export enum PeerMessageType {
    STREAM_REQUEST = "stream_request", // Viewer -> Streamer (via server)
    CONFIGURATION = "configuration", // Viewer -> Streamer (via DataChannel directly) - NOT a signaling type
    OFFER = "offer", // Streamer -> Viewer (via server)
    ANSWER = "answer", // Viewer -> Streamer (via server)
    ICE_CANDIDATE = "ice-candidate", // Both directions (via server)
}

// Data Channel messages (sent directly peer-to-peer)
export enum DataChannelMessageType {
    CONFIGURATION = "configuration", // Viewer -> Streamer
    PROFILE = "profile", // Streamer -> Viewer
    TERMINATION = "termination", // Streamer -> Viewer (or vice-versa)
    PING = "ping", // Example
    PONG = "pong", // Example
}


// --- Base Message Interface ---
interface BaseMessage {
    type: SystemMessageType | PeerMessageType;
    token?: string; // Auth token often required
}

// --- Specific Message Interfaces ---

// Client -> Server
export interface PublishMessage extends BaseMessage {
    type: SystemMessageType.PUBLISH;
    kind: 'viewer' | 'streamer';
    peer_id: string;
    name: string;
    profile?: Profile; // Optional for viewers
}

// Server -> Client
export interface StreamersMessage extends BaseMessage {
    type: SystemMessageType.STREAMERS;
    peers: Peer[];
}

// Server -> Client
export interface ErrorMessage extends BaseMessage {
    type: SystemMessageType.ERROR;
    error: string;
}

// Server -> Client
export interface PeerDisconnectedMessage extends BaseMessage {
    type: SystemMessageType.PEER_DISCONNECTED;
    id?: string; // ID of the peer that disconnected
}

// Viewer -> Server -> Streamer
export interface StreamRequestMessage extends BaseMessage {
    type: PeerMessageType.STREAM_REQUEST;
    to: string; // Target streamer peer_id
    config: Config;
}

// Streamer -> Server -> Viewer
export interface OfferMessage extends BaseMessage {
    type: PeerMessageType.OFFER;
    from: string; // Sender streamer peer_id
    sdp: string;
}

// Viewer -> Server -> Streamer
export interface AnswerMessage extends BaseMessage {
    type: PeerMessageType.ANSWER;
    to: string; // Target streamer peer_id
    sdp: string;
}

// Both -> Server -> Peer
export interface IceCandidateMessage extends BaseMessage {
    type: PeerMessageType.ICE_CANDIDATE;
    to: string; // Target peer_id
    from?: string; // Sender peer_id might be added by server or needed by client
    candidate: RTCIceCandidateInit | RTCIceCandidate; // Can be object or RTCIceCandidate
}

// Union type for all possible signaling messages
export type SignalingMessage =
    | PublishMessage
    | StreamersMessage
    | ErrorMessage
    | PeerDisconnectedMessage
    | StreamRequestMessage
    | OfferMessage
    | AnswerMessage
    | IceCandidateMessage;


// --- Data Channel Message Interfaces ---
interface BaseDataChannelMessage {
    type: DataChannelMessageType;
}

export interface ConfigurationDataMessage extends BaseDataChannelMessage {
    type: DataChannelMessageType.CONFIGURATION;
    config: Config;
}

export interface ProfileDataMessage extends BaseDataChannelMessage {
    type: DataChannelMessageType.PROFILE;
    profile: Profile;
}

export interface TerminationDataMessage extends BaseDataChannelMessage {
    type: DataChannelMessageType.TERMINATION;
}


// Union type for data channel messages
export type DataChannelMessage =
    | ConfigurationDataMessage
    | ProfileDataMessage
    | TerminationDataMessage; // Add others like Ping/Pong if used