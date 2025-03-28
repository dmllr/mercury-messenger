import React, { createContext, useState, ReactNode, useCallback, useRef } from 'react';
import { ServiceConnectionState, SignalingMessage } from '@/types/signaling';

// Type for peer message listeners map: { peerId: callback[] }
type PeerMessageListeners = Record<string, ((message: SignalingMessage) => void)[]>;
type PeerDisconnectListeners = Record<string, (()=> void)[]>;


interface SignalingContextProps {
    ws: WebSocket | null;
    setWs: (ws: WebSocket | null) => void;
    connectionState: ServiceConnectionState;
    setConnectionState: (state: ServiceConnectionState) => void;
    error: string | null;
    setError: (error: string | null) => void;
    connectSignaling: () => void;
    disconnectSignaling: () => void;
    sendMessage: (message: SignalingMessage) => void;
    // Functions for WebRTC hook to subscribe/unsubscribe
    subscribeToPeerMessages: (peerId: string, callback: (message: SignalingMessage) => void) => () => void;
    subscribeToPeerDisconnect: (peerId: string, callback: () => void) => () => void;
    // Internal handlers called by useSignaling hook
    handlePeerMessage: (message: SignalingMessage) => void;
    handlePeerDisconnect: (peerId: string) => void;
}

const SIGNALING_SERVER_URL = "wss://app.hela.me:8000"; // Ensure consistency

export const SignalingContext = createContext<SignalingContextProps | undefined>(undefined);

interface SignalingProviderProps {
    children: ReactNode;
}

export const SignalingProvider: React.FC<SignalingProviderProps> = ({ children }) => {
    const [ws, setWsInternal] = useState<WebSocket | null>(null);
    const [connectionState, setConnectionState] = useState<ServiceConnectionState>(ServiceConnectionState.Unknown);
    const [error, setError] = useState<string | null>(null);
    const wsRef = useRef<WebSocket | null>(null); // Ref to hold the WebSocket instance

    // Store listeners in refs to avoid context re-renders on listener changes
    const peerMessageListenersRef = useRef<PeerMessageListeners>({});
    const peerDisconnectListenersRef = useRef<PeerDisconnectListeners>({});


    // Update both state and ref
    const setWs = useCallback((newWs: WebSocket | null) => {
        wsRef.current = newWs;
        setWsInternal(newWs);
    }, []);

    const connectSignaling = useCallback(() => {
        if (wsRef.current && (wsRef.current.readyState === WebSocket.OPEN || wsRef.current.readyState === WebSocket.CONNECTING)) {
            console.log("WebSocket already open or connecting.");
            return;
        }
        console.log("Attempting to connect WebSocket...");
        setError(null);
        setConnectionState(ServiceConnectionState.Establishing);
        try {
            const newWs = new WebSocket(SIGNALING_SERVER_URL);
            setWs(newWs); // Store the new instance
            // Event listeners will be attached in the useSignaling hook effect based on 'ws' state change
        } catch (e: any) {
            console.error("WebSocket connection failed immediately:", e);
            setError(`Failed to create WebSocket: ${e.message}`);
            setConnectionState(ServiceConnectionState.Failure);
            setWs(null);
        }
    }, [setWs]); // Include setWs

    const disconnectSignaling = useCallback(() => {
        if (wsRef.current) {
            console.log("Disconnecting WebSocket...");
            setConnectionState(ServiceConnectionState.Disconnected); // Set state *before* closing
            wsRef.current.close(1000, "Client disconnected"); // Normal closure
            setWs(null); // Clear instance immediately after initiating close
        }
    }, [setWs]); // Include setWs

    const sendMessage = useCallback((message: SignalingMessage) => {
        if (wsRef.current && wsRef.current.readyState === WebSocket.OPEN) {
            try {
                const messageString = JSON.stringify(message);
                wsRef.current.send(messageString);
                // console.log("Sent message:", message); // Can be noisy, log selectively if needed
            } catch (e) {
                console.error("Failed to send message:", message, e);
                setError("Failed to send message.");
                // Consider connection status check or reconnection attempt
            }
        } else {
            console.warn("Cannot send message, WebSocket not open. State:", wsRef.current?.readyState);
            // setError("Cannot send message: Connection not open.");
            // Don't set error here, might be transient during connection setup
        }
    }, []); // No dependencies needed as wsRef is used

    // Function for useWebRTC to subscribe to messages for a specific peer
    const subscribeToPeerMessages = useCallback((peerId: string, callback: (message: SignalingMessage) => void): (() => void) => {
        console.log(`Subscribing to messages for peer ${peerId}`);
        if (!peerMessageListenersRef.current[peerId]) {
            peerMessageListenersRef.current[peerId] = [];
        }
        peerMessageListenersRef.current[peerId].push(callback);

        // Return an unsubscribe function
        return () => {
            console.log(`Unsubscribing from messages for peer ${peerId}`);
            if (peerMessageListenersRef.current[peerId]) {
                peerMessageListenersRef.current[peerId] = peerMessageListenersRef.current[peerId].filter(cb => cb !== callback);
                if (peerMessageListenersRef.current[peerId].length === 0) {
                    delete peerMessageListenersRef.current[peerId]; // Clean up if no listeners left
                }
            }
        };
    }, []); // No dependencies, uses ref

    // Function for useWebRTC to subscribe to disconnect events for a specific peer
    const subscribeToPeerDisconnect = useCallback((peerId: string, callback: () => void): (() => void) => {
        console.log(`Subscribing to disconnect for peer ${peerId}`);
        if (!peerDisconnectListenersRef.current[peerId]) {
            peerDisconnectListenersRef.current[peerId] = [];
        }
        peerDisconnectListenersRef.current[peerId].push(callback);

        return () => {
            console.log(`Unsubscribing from disconnect for peer ${peerId}`);
            if (peerDisconnectListenersRef.current[peerId]) {
                peerDisconnectListenersRef.current[peerId] = peerDisconnectListenersRef.current[peerId].filter(cb => cb !== callback);
                if (peerDisconnectListenersRef.current[peerId].length === 0) {
                    delete peerDisconnectListenersRef.current[peerId];
                }
            }
        };
    }, []);


    // Called by useSignaling when a peer message arrives
    const handlePeerMessage = useCallback((message: SignalingMessage) => {
        if ('from' in message && message.from) { // Check if 'from' exists and is truthy
            const targetPeerId = message.from;
            if (peerMessageListenersRef.current[targetPeerId]) {
                console.log(`Dispatching message from ${targetPeerId} to listeners.`);
                peerMessageListenersRef.current[targetPeerId].forEach(callback => {
                    try {
                        callback(message);
                    } catch (e) {
                        console.error(`Error in peer message listener for ${targetPeerId}:`, e);
                    }
                });
            }
        } else {
            // console.warn(`Received message from ${targetPeerId}, but no listeners registered.`);
        }
    }, []); // No dependencies, uses ref

    // Called by useSignaling when a peer_disconnected system message arrives
    const handlePeerDisconnect = useCallback((disconnectedPeerId: string) => {
        if (disconnectedPeerId && peerDisconnectListenersRef.current[disconnectedPeerId]) {
            console.log(`Dispatching disconnect for ${disconnectedPeerId} to listeners.`);
            // Call listeners and remove them after calling
            const listeners = [...peerDisconnectListenersRef.current[disconnectedPeerId]]; // Copy listeners
            delete peerDisconnectListenersRef.current[disconnectedPeerId]; // Remove entry
            listeners.forEach(callback => {
                try {
                    callback();
                } catch (e) {
                    console.error(`Error in peer disconnect listener for ${disconnectedPeerId}:`, e);
                }
            });

            // Also clean up message listeners for the disconnected peer
            delete peerMessageListenersRef.current[disconnectedPeerId];
        }
    }, []);


    return (
        <SignalingContext.Provider value={{
            ws: ws, // Use state for consumers that need reactivity
            setWs,
            connectionState,
            setConnectionState,
            error,
            setError,
            connectSignaling,
            disconnectSignaling,
            sendMessage,
            subscribeToPeerMessages,
            subscribeToPeerDisconnect,
            handlePeerMessage,
            handlePeerDisconnect,
        }}>
            {children}
        </SignalingContext.Provider>
    );
};