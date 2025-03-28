import { useState, useEffect, useRef, useCallback, useContext } from 'react';
import { SignalingContext } from '@/contexts/SignalingContext';
import { useAuth } from './useAuth';
import {
    PeerMessageType,
    SignalingMessage,
    DataChannelMessage,
    DataChannelMessageType,
    AnswerMessage
} from '@/types/signaling';
import { Config } from '@/types/config';
import { Peer } from '@/types/peer';

// ICE Servers configuration (from C++ code)
const rtcConfiguration: RTCConfiguration = {
    iceServers: [
        { urls: "stun:stun.relay.metered.ca:80" },
        {
            urls: "turn:a.relay.metered.ca:80",
            username: "49880a0f046c8990b1990b35",
            credential: "qZCNW/57EB8GwO3A"
        },
        {
            urls: "turn:a.relay.metered.ca:443",
            username: "49880a0f046c8990b1990b35",
            credential: "qZCNW/57EB8GwO3A"
        }
        // Add more servers if needed
    ]
};

export function useWebRTC(peerId: string | null) {
    const pc = useRef<RTCPeerConnection | null>(null);
    const dataChannel = useRef<RTCDataChannel | null>(null);
    const [stream, setStream] = useState<MediaStream | null>(null);
    const [connectionState, setConnectionState] = useState<RTCPeerConnectionState>('new');
    const [iceConnectionState, setIceConnectionState] = useState<RTCIceConnectionState>('new');
    const [signalingState, setSignalingState] = useState<RTCSignalingState>('stable');
    const [isDataChannelOpen, setIsDataChannelOpen] = useState<boolean>(false);
    const [error, setError] = useState<string | null>(null);
    const [remoteProfile, setRemoteProfile] = useState<Peer['profile'] | null>(null);


    const { idToken } = useAuth();
    const signaling = useContext(SignalingContext);

    // --- Cleanup ---
    const closeConnection = useCallback(() => {
        console.log("Closing WebRTC connection...");
        if (dataChannel.current) {
            try {
                // Optionally send termination message before closing
                if (dataChannel.current.readyState === 'open') {
                    const terminationMsg: DataChannelMessage = { type: DataChannelMessageType.TERMINATION };
                    dataChannel.current.send(JSON.stringify(terminationMsg));
                    console.log("Sent termination message via data channel.");
                }
                dataChannel.current.close();
            } catch (e) {
                console.warn("Error closing data channel:", e);
            }
            dataChannel.current = null;
        }
        if (pc.current) {
            // Remove listeners before closing
            pc.current.onicecandidate = null;
            pc.current.ontrack = null;
            pc.current.onconnectionstatechange = null;
            pc.current.oniceconnectionstatechange = null;
            pc.current.onsignalingstatechange = null;
            pc.current.ondatachannel = null; // Important!

            pc.current.close();
            pc.current = null;
            console.log("RTCPeerConnection closed.");
        }
        setStream(null);
        setConnectionState('closed');
        setIceConnectionState('closed');
        setSignalingState('closed');
        setIsDataChannelOpen(false);
        setError(null);
        setRemoteProfile(null); // Clear profile on close

    }, []); // No dependencies needed for cleanup refs

    // --- Data Channel Handlers ---
    const handleDataChannelMessage = useCallback((event: MessageEvent) => {
        console.log('Data channel message received:', event.data);
        try {
            // Handle Ping/Pong if implemented on streamer side (optional)
            if (typeof event.data === 'string' && event.data.startsWith('Ping')) {
                console.log("Received Ping, sending Pong...");
                if (dataChannel.current?.readyState === 'open') {
                    // Debounce or delay Pong if needed
                    setTimeout(() => dataChannel.current?.send('Pong'), 100); // Small delay
                }
                // Could update a 'last ping received' timestamp state here
                return;
            }

            const message: DataChannelMessage = JSON.parse(event.data);
            switch (message.type) {
                case DataChannelMessageType.PROFILE:
                    console.log("Received profile via data channel:", message.profile);
                    setRemoteProfile(message.profile || []);
                    break;
                case DataChannelMessageType.TERMINATION:
                    console.log("Received termination request via data channel.");
                    closeConnection(); // Close connection gracefully
                    break;
                // Add other data channel message types if needed
                default:
                    console.warn("Unknown data channel message type:", message.type);
            }
        } catch (e) {
            console.error("Failed to parse data channel message:", e);
        }
    }, [closeConnection]);

    const handleDataChannelOpen = useCallback(() => {
        console.log('Data channel is open');
        setIsDataChannelOpen(true);
        // You can now send messages, e.g., request initial profile or capabilities
        // sendDataChannelMessage(JSON.stringify({ type: 'request_profile' }));
    }, []);

    const handleDataChannelClose = useCallback(() => {
        console.log('Data channel is closed');
        setIsDataChannelOpen(false);
        // Maybe trigger cleanup or connection check
    }, []);

    const setupDataChannel = useCallback((dc: RTCDataChannel) => {
        console.log(`Data channel "${dc.label}" received or created.`);
        dataChannel.current = dc;
        dc.onmessage = handleDataChannelMessage;
        dc.onopen = handleDataChannelOpen;
        dc.onclose = handleDataChannelClose;
        dc.onerror = (event) => {
            console.error("Data channel error:", event);
            // Handle error appropriately
        };
    }, [handleDataChannelMessage, handleDataChannelOpen, handleDataChannelClose]);


    // --- Peer Connection Handlers ---
    const handleIceCandidate = useCallback((event: RTCPeerConnectionIceEvent) => {
        if (event.candidate && idToken && peerId) {
            console.log("Sending ICE candidate:", event.candidate);
            const message: SignalingMessage = {
                type: PeerMessageType.ICE_CANDIDATE,
                to: peerId,
                candidate: event.candidate.toJSON(),
                token: idToken,
            };
            signaling?.sendMessage(message);
        }
    }, [idToken, peerId, signaling]);

    const handleTrack = useCallback((event: RTCTrackEvent) => {
        console.log("Track received:", event.track.kind, event.streams);
        // Usually, the stream is attached here
        if (event.streams && event.streams[0]) {
            console.log("Attaching stream:", event.streams[0].id);
            setStream(event.streams[0]);
        } else {
            // Fallback for older browsers might need to construct stream manually
            const newStream = new MediaStream();
            newStream.addTrack(event.track);
            setStream(newStream);
            console.log("Created new stream for track:", newStream.id);
        }
    }, []);

    const handleConnectionStateChange = useCallback(() => {
        if (pc.current) {
            console.log("PeerConnection state changed:", pc.current.connectionState);
            setConnectionState(pc.current.connectionState);
            if (pc.current.connectionState === 'failed' || pc.current.connectionState === 'disconnected' || pc.current.connectionState === 'closed') {
                // Consider cleanup or attempting restart
                // closeConnection(); // Automatically close on failure/disconnect? Or allow retry?
            }
            if (pc.current.connectionState === 'failed') {
                setError("WebRTC connection failed.");
            }
        }
    }, []);

    const handleIceConnectionStateChange = useCallback(() => {
        if (pc.current) {
            console.log("ICE Connection state changed:", pc.current.iceConnectionState);
            setIceConnectionState(pc.current.iceConnectionState);
            if (pc.current.iceConnectionState === 'failed' || pc.current.iceConnectionState === 'disconnected' || pc.current.iceConnectionState === 'closed') {
                // Handle ICE failures
                // setError("ICE connection failed or disconnected.");
                // closeConnection(); // Maybe close connection on ICE failure
            }
        }
    }, []);

    const handleSignalingStateChange = useCallback(() => {
        if (pc.current) {
            console.log("Signaling state changed:", pc.current.signalingState);
            setSignalingState(pc.current.signalingState);
        }
    }, []);


    // --- Create Peer Connection ---
    const createPeerConnection = useCallback(() => {
        console.log("Creating new RTCPeerConnection");
        closeConnection(); // Ensure any previous connection is closed

        try {
            const newPc = new RTCPeerConnection(rtcConfiguration);
            newPc.onicecandidate = handleIceCandidate;
            newPc.ontrack = handleTrack;
            newPc.onconnectionstatechange = handleConnectionStateChange;
            newPc.oniceconnectionstatechange = handleIceConnectionStateChange; // Add ICE state handler
            newPc.onsignalingstatechange = handleSignalingStateChange; // Add Signaling state handler
            // IMPORTANT: Handle incoming data channel from the streamer
            newPc.ondatachannel = (event) => {
                console.log("ondatachannel event received!");
                setupDataChannel(event.channel);
            };


            pc.current = newPc;
            setError(null); // Clear previous errors
            setConnectionState('new'); // Reset state
            setIceConnectionState('new');
            setSignalingState('stable');
            setIsDataChannelOpen(false);
            setRemoteProfile(null); // Clear old profile

        } catch (e: any) {
            console.error("Failed to create RTCPeerConnection:", e);
            setError(`Failed to create PeerConnection: ${e.message}`);
            pc.current = null;
        }
    }, [closeConnection, handleIceCandidate, handleTrack, handleConnectionStateChange, handleIceConnectionStateChange, handleSignalingStateChange, setupDataChannel]);

    // --- Handle Incoming Signaling Messages for THIS peer ---
    const handleSignalingMessage = useCallback(async (message: SignalingMessage) => {
        if (!pc.current && message.type !== PeerMessageType.OFFER) {
            console.warn("Received signaling message but PeerConnection is not initialized:", message.type);
            // Ignore candidates/answers if we haven't processed an offer yet
            return;
        }

        try {
            switch (message.type) {
                case PeerMessageType.OFFER:
                    if (message.sdp && message.from === peerId) {
                        console.log("Received OFFER from", message.from);
                        // Create PC if it doesn't exist (should ideally be created before this)
                        if (!pc.current) {
                            console.warn("Received offer but PC not ready, creating PC first.");
                            createPeerConnection();
                        }
                        if (!pc.current) {
                            throw new Error("Failed to create PeerConnection before handling offer.");
                        }

                        await pc.current.setRemoteDescription(new RTCSessionDescription({ type: 'offer', sdp: message.sdp }));
                        console.log("Remote description (offer) set.");

                        // Create Answer
                        const answer = await pc.current.createAnswer();
                        await pc.current.setLocalDescription(answer);
                        console.log("Local description (answer) set.");

                        // Send Answer back via signaling
                        if (idToken && peerId) {
                            if (answer.sdp) { // Check if sdp exists
                                const answerMsg: AnswerMessage = { // Use specific type
                                    type: PeerMessageType.ANSWER,
                                    to: peerId,
                                    sdp: answer.sdp, // Now guaranteed to be string
                                    token: idToken,
                                };
                                signaling?.sendMessage(answerMsg);
                                console.log("Sent ANSWER");
                            } else {
                                console.error("Failed to create answer with SDP.");
                                // Handle error appropriately, maybe close connection
                                setError("Failed to create valid answer SDP.");
                                closeConnection();
                            }
                        }
                    }
                    break;

                case PeerMessageType.ICE_CANDIDATE:
                    if (message.candidate && message.from === peerId && pc.current) {
                        console.log("Received ICE candidate from", message.from);
                        // Ensure remote description is set before adding candidates
                        if (pc.current.remoteDescription) {
                            await pc.current.addIceCandidate(new RTCIceCandidate(message.candidate));
                            console.log("Added ICE candidate.");
                        } else {
                            console.warn("Received ICE candidate before remote description was set. Queuing or ignoring might be needed.");
                            // Basic approach: ignore if remote desc not set yet. More robust: queue candidates.
                        }
                    }
                    break;

                // Viewer typically doesn't receive ANSWER, but handle just in case
                case PeerMessageType.ANSWER:
                    if (message.sdp && 'from' in message && message.from === peerId && pc.current && pc.current.signalingState === 'have-local-offer') {
                        console.log("Received ANSWER from", message.from);
                        await pc.current.setRemoteDescription(new RTCSessionDescription({ type: 'answer', sdp: message.sdp }));
                        console.log("Remote description (answer) set.");
                    }
                    break;

                default:
                    // Ignore messages not relevant to WebRTC setup for this peer
                    break;
            }
        } catch (e: any) {
            console.error("Error handling signaling message:", message.type, e);
            setError(`Error handling ${message.type}: ${e.message}`);
            closeConnection(); // Close connection on error during setup
        }
    }, [pc, peerId, createPeerConnection, idToken, signaling, closeConnection]);


    // --- Subscribe to relevant signaling messages ---
    useEffect(() => {
        if (!signaling || !peerId) return;

        const unsubscribe = signaling.subscribeToPeerMessages(peerId, handleSignalingMessage);
        const unsubscribeDisconnect = signaling.subscribeToPeerDisconnect(peerId, () => {
            console.log(`Detected disconnect for peer ${peerId} from signaling.`);
            closeConnection(); // Close WebRTC if signaling says peer disconnected
        });

        // Cleanup subscription
        return () => {
            unsubscribe();
            unsubscribeDisconnect();
        };

    }, [signaling, peerId, handleSignalingMessage, closeConnection]);


    // --- Public Functions ---
    const requestStream = useCallback((config: Config) => {
        if (peerId && idToken && signaling) {
            // Create PC *before* sending stream request, ready for the offer
            if (!pc.current) {
                createPeerConnection();
            }
            const message: SignalingMessage = {
                type: PeerMessageType.STREAM_REQUEST,
                to: peerId,
                config: config,
                token: idToken,
            };
            signaling.sendMessage(message);
            console.log("Sent stream request to", peerId, "with config:", config);
        } else {
            console.warn("Cannot send stream request: Missing peerId, token, or signaling connection.");
            setError("Cannot send stream request - connection not ready.");
        }
    }, [peerId, idToken, signaling, createPeerConnection]); // Add createPeerConnection dependency

    const sendDataChannelMessage = useCallback((message: string) => {
        if (dataChannel.current && dataChannel.current.readyState === 'open') {
            try {
                dataChannel.current.send(message);
                console.log("Sent data channel message:", message);
            } catch (e: any) {
                console.error("Failed to send data channel message:", e);
                setError(`Failed to send message: ${e.message}`);
            }
        } else {
            console.warn("Cannot send data channel message: Channel not open or not initialized.");
            setError("Data channel not available.");
        }
    }, []);


    return {
        stream,
        connectionState,
        iceConnectionState, // Expose ICE state
        signalingState, // Expose Signaling state
        isDataChannelOpen, // Expose data channel status
        error,
        remoteProfile, // Expose the received profile
        requestStream,
        closeConnection,
        sendMessage: sendDataChannelMessage, // Expose function to send data channel messages
    };
}