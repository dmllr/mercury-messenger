import { useEffect, useCallback, useRef, useContext } from 'react';
import { useAuth } from './useAuth';
import { ServiceConnectionState, SignalingMessage, SystemMessageType } from '@/types/signaling';
import { Peer } from '@/types/peer';
import { SignalingContext } from '@/contexts/SignalingContext';
import { usePeers } from '@/contexts/PeersContext';

const MIN_RECONNECT_DELAY = 1000; // 1 second
const MAX_RECONNECT_DELAY = 10000; // 10 seconds
const MAX_CONNECTION_ATTEMPTS = 10;

// Helper function for random string generation (browser-safe)
const generateBrowserSafeRandomString = (length: number): string => {
    const characters = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    let result = '';
    const charactersLength = characters.length;
    // Use crypto.getRandomValues for better randomness if available
    if (window.crypto && window.crypto.getRandomValues) {
        const randomValues = new Uint32Array(length);
        window.crypto.getRandomValues(randomValues);
        for (let i = 0; i < length; i++) {
            result += characters.charAt(randomValues[i] % charactersLength);
        }
    } else {
        // Fallback to Math.random (less secure, but works)
        console.warn("Browser crypto API not available, falling back to Math.random for ID generation.");
        for (let i = 0; i < length; i++) {
            result += characters.charAt(Math.floor(Math.random() * charactersLength));
        }
    }
    return result;
};

// Helper to get or generate a unique viewer ID
const getViewerId = (): string => {
    let viewerId = localStorage.getItem('viewerId');
    if (!viewerId) {
        // Use the browser-safe function
        viewerId = `V-${generateBrowserSafeRandomString(8)}`;
        localStorage.setItem('viewerId', viewerId);
    }
    return viewerId;
};

export function useSignaling() {
    const context = useContext(SignalingContext);
    if (!context) {
        throw new Error('useSignaling must be used within a SignalingProvider');
    }
    const {
        ws, setWs, connectionState, setConnectionState,
        error, setError, connectSignaling, disconnectSignaling, sendMessage
    } = context;

    const { idToken } = useAuth(); // Get token for authentication
    const { setPeers, setLoading: setPeersLoading } = usePeers();
    const reconnectAttempts = useRef(0);
    const reconnectTimeout = useRef<number | null>(null);
    const viewerId = useRef<string>(getViewerId()); // Get or generate viewer ID
    const viewerName = useRef<string>(localStorage.getItem('viewerName') || `Web Viewer ${viewerId.current.substring(2)}`); // Get or set viewer name

    // Store or update viewer name if needed
    useEffect(() => {
        localStorage.setItem('viewerName', viewerName.current);
    }, []);


    // --- WebSocket Event Handlers ---

    const handleOpen = useCallback(() => {
        console.log("WebSocket connected, signaling ready");
        setError(null);
        setConnectionState(ServiceConnectionState.Online);
        reconnectAttempts.current = 0; // Reset attempts on successful connection

        // Send publish message immediately after connection
        if (idToken) {
            const publishMsg: SignalingMessage = {
                type: SystemMessageType.PUBLISH,
                kind: "viewer", // Identify as viewer
                peer_id: viewerId.current, // Use the unique viewer ID
                name: viewerName.current, // Use viewer name
                token: idToken,
                // profile: [], // Viewers typically don't publish a profile
            };
            sendMessage(publishMsg);
            console.log("Sent publish message:", publishMsg);
        } else {
            console.warn("No auth token available when WebSocket opened. Cannot publish.");
            // Consider disconnecting or waiting for token
        }

    }, [setConnectionState, setError, idToken, sendMessage]);

    const handleClose = useCallback((event: CloseEvent) => {
        console.warn(`WebSocket closed: Code=${event.code}, Reason=${event.reason}`);
        setWs(null); // Clear WebSocket instance

        // Determine if reconnect should be attempted
        // Don't reconnect if explicitly disconnected or if max attempts reached without success between opens
        if (connectionState !== ServiceConnectionState.Disconnected && reconnectAttempts.current < MAX_CONNECTION_ATTEMPTS) {
            setConnectionState(ServiceConnectionState.Establishing); // Show as trying to reconnect
            reconnectAttempts.current++;
            const delay = Math.min(MAX_RECONNECT_DELAY, MIN_RECONNECT_DELAY * Math.pow(2, reconnectAttempts.current -1));
            console.log(`Attempting reconnect #${reconnectAttempts.current} in ${delay / 1000}s...`);

            if (reconnectTimeout.current) clearTimeout(reconnectTimeout.current); // Clear existing timeout
            reconnectTimeout.current = window.setTimeout(() => {
                connectSignaling(); // Attempt to reconnect
            }, delay);
        } else {
            if (connectionState !== ServiceConnectionState.Disconnected) {
                console.error("Max reconnection attempts reached. Stopping.");
                setError("Max reconnection attempts reached.");
                setConnectionState(ServiceConnectionState.Failure);
            } else {
                console.log("WebSocket closed intentionally or after max attempts.");
                // State is already Disconnected or Failure
            }
        }

    }, [setConnectionState, setWs, connectSignaling, connectionState]);

    const handleError = useCallback((event: Event) => {
        console.error("WebSocket error:", event);
        setError("WebSocket connection error occurred.");
        setConnectionState(ServiceConnectionState.Failure); // Move to failure state on error
        // The 'close' event will likely follow, potentially triggering reconnect logic there.
    }, [setConnectionState, setError]);

    const handleMessage = useCallback((event: MessageEvent) => {
        try {
            const message: SignalingMessage = JSON.parse(event.data);
            console.log("Received message:", message);

            if ('from' in message && message.from) {
                // --- Peer Message Handling ---
                context.handlePeerMessage(message);
            } else {
                // --- System Message Handling ---
                switch (message.type) {
                    case SystemMessageType.STREAMERS:
                        setPeersLoading(true);
                        // Assuming message.peers is the array of Peer objects
                        setPeers((message.peers || []) as Peer[]);
                        setPeersLoading(false);
                        console.log("Updated peer list:", message.peers);
                        break;
                    case SystemMessageType.ERROR:
                        console.error("Signaling server error:", message.error);
                        setError(`Signaling Error: ${message.error || 'Unknown error'}`);
                        // Decide if this error is fatal
                        // setConnectionState(ServiceConnectionState.Failure);
                        // disconnectSignaling(); // Maybe disconnect on critical errors
                        break;
                    case SystemMessageType.PEER_DISCONNECTED:
                        console.log(`Peer ${message.id} disconnected.`);
                        // Update peer list state by removing the disconnected peer
                        setPeers(prevPeers => prevPeers.filter(p => p.id !== message.id));
                        // Also notify useWebRTC if this is the currently connected peer
                        context.handlePeerDisconnect(message.id ?? '');
                        break;
                    default:
                        console.warn("Received unknown system message type:", message.type, message);
                }
            }

        } catch (e) {
            console.error("Failed to parse signaling message:", event.data, e);
            // Handle parsing error, maybe ignore the message
        }
    }, [setPeers, setPeersLoading, setError, context]); // Add context to dependency array


    // --- Setup WebSocket Connection ---
    useEffect(() => {
        if (!idToken || connectionState === ServiceConnectionState.Online || connectionState === ServiceConnectionState.Establishing) {
            // Don't connect if no token, or already connected/connecting
            return;
        }

        // Initiate connection
        connectSignaling();

        // Cleanup function
        return () => {
            if (reconnectTimeout.current) {
                clearTimeout(reconnectTimeout.current);
                reconnectTimeout.current = null;
            }
            // Don't automatically call disconnectSignaling here on unmount
            // Let the context provider manage explicit disconnection if needed
        };
        // Only re-run connect if idToken becomes available and not already connected/connecting
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [idToken]);

    // --- Attach handlers when WebSocket instance is ready ---
    useEffect(() => {
        if (ws) {
            ws.addEventListener('open', handleOpen);
            ws.addEventListener('message', handleMessage);
            ws.addEventListener('error', handleError);
            ws.addEventListener('close', handleClose);

            // Cleanup event listeners when WebSocket instance changes or component unmounts
            return () => {
                console.log("Cleaning up WebSocket event listeners.");
                ws.removeEventListener('open', handleOpen);
                ws.removeEventListener('message', handleMessage);
                ws.removeEventListener('error', handleError);
                ws.removeEventListener('close', handleClose);
            };
        }
    }, [ws, handleOpen, handleMessage, handleError, handleClose]);


    return {
        connectionState,
        error,
        connectSignaling,
        disconnectSignaling,
        sendMessage, // Expose the context's sendMessage
        viewerId: viewerId.current, // Expose viewerId
    };
}