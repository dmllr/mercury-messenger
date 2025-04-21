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
    ],
    bundlePolicy: "max-bundle"
};

export function useWebRTC(peerId: string | null) {
    const peerConnectionCreated = useRef<boolean>(false);
    const pc = useRef<RTCPeerConnection | null>(null);
    const dataChannel = useRef<RTCDataChannel | null>(null);
    const [stream, setStream] = useState<MediaStream | null>(null);
    const [connectionState, setConnectionState] = useState<RTCPeerConnectionState>('new');
    const [iceConnectionState, setIceConnectionState] = useState<RTCIceConnectionState>('new');
    const [iceGatheringState, setIceGatheringState] = useState<RTCIceGatheringState>('new');
    const [signalingState, setSignalingState] = useState<RTCSignalingState>('stable');
    const [isDataChannelOpen, setIsDataChannelOpen] = useState<boolean>(false);
    const [error, setError] = useState<string | null>(null);
    const [remoteProfile, setRemoteProfile] = useState<Peer['profile'] | null>(null);

    const messageQueue = useRef<SignalingMessage[]>([]);
    const isProcessingMessage = useRef<boolean>(false);
    const pingTimeoutRef = useRef<number | null>(null);
    const startTimeRef = useRef<number | null>(null);

    const { idToken } = useAuth();
    const signaling = useContext(SignalingContext);

    const getCurrentTimestamp = () => {
        if (startTimeRef.current === null) {
            startTimeRef.current = new Date().getTime();
            return 0;
        } else {
            return new Date().getTime() - startTimeRef.current;
        }
    };

    const processMessageQueue = useCallback(async () => {
        if (isProcessingMessage.current || messageQueue.current.length === 0) {
            return;
        }

        isProcessingMessage.current = true;
        try {
            const message = messageQueue.current.shift();
            if (message) {
                await processSignalingMessage(message);
            }
        } catch (error) {
            console.error("Error processing message from queue:", error);
            setError(`Error processing message: ${error}`);
        } finally {
            isProcessingMessage.current = false;

            if (messageQueue.current.length > 0) {
                setTimeout(processMessageQueue, 0);
            }
        }
    }, []);

    const closeConnection = useCallback(() => {
        console.log("Closing WebRTC connection...");
        messageQueue.current = [];
        isProcessingMessage.current = false;
        // Reset the connection created flag
        peerConnectionCreated.current = false;

        if (pingTimeoutRef.current) {
            clearTimeout(pingTimeoutRef.current);
            pingTimeoutRef.current = null;
        }

        if (dataChannel.current) {
            try {
                if (dataChannel.current.readyState === 'open') {
                    const terminationMsg: DataChannelMessage = { type: DataChannelMessageType.TERMINATION };
                    dataChannel.current.send(JSON.stringify(terminationMsg));
                }
                dataChannel.current.close();
            } catch (e) {
                console.warn("Error closing data channel:", e);
            }
            dataChannel.current = null;
        }

        if (pc.current) {
            // Close transceivers
            if (pc.current.getTransceivers) {
                pc.current.getTransceivers().forEach(function(transceiver) {
                    if (transceiver.stop) {
                        transceiver.stop();
                    }
                });
            }

            // Close local tracks
            pc.current.getSenders().forEach(function(sender) {
                const track = sender.track;
                if (track !== null) {
                    // @ts-ignore
                    sender.track.stop();
                }
            });

            setTimeout(() => {
                if (pc.current) {
                    pc.current.close();
                    pc.current = null;
                }
            }, 500);
        }

        setStream(null);
        setConnectionState('closed');
        setIceConnectionState('closed');
        setIceGatheringState('complete');
        setSignalingState('closed');
        setIsDataChannelOpen(false);
        setError(null);
        setRemoteProfile(null);
        startTimeRef.current = null;
    }, []);

    const handleDataChannelMessage = useCallback((event: MessageEvent) => {
        console.log('Data channel message received:', event.data);
        try {
            if (typeof event.data === 'string' && event.data === "Ping") {
                console.log("Received Ping, sending Pong...");
                if (pingTimeoutRef.current) {
                    clearTimeout(pingTimeoutRef.current);
                }

                pingTimeoutRef.current = window.setTimeout(() => {
                    if (dataChannel.current?.readyState === 'open') {
                        const reply = `Pong ${getCurrentTimestamp()}`;
                        console.log("Sending pong response:", reply);
                        dataChannel.current.send(reply);
                    }
                    pingTimeoutRef.current = null;
                }, 1000) as unknown as number;
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
                    closeConnection();
                    break;
                default:
                    console.warn("Unknown data channel message type:", message.type);
            }
        } catch (e) {
            console.error("Failed to parse data channel message:", e);
        }
    }, [closeConnection, getCurrentTimestamp]);

    const handleDataChannelOpen = useCallback(() => {
        console.log('Data channel is open');
        setIsDataChannelOpen(true);
    }, []);

    const handleDataChannelClose = useCallback(() => {
        console.log('Data channel is closed');
        setIsDataChannelOpen(false);
    }, []);

    const setupDataChannel = useCallback((dc: RTCDataChannel) => {
        console.log(`Data channel "${dc.label}" received or created.`);
        dataChannel.current = dc;
        dc.onmessage = handleDataChannelMessage;
        dc.onopen = handleDataChannelOpen;
        dc.onclose = handleDataChannelClose;
        dc.onerror = (event) => {
            console.error("Data channel error:", event);
        };
    }, [handleDataChannelMessage, handleDataChannelOpen, handleDataChannelClose]);

    const createPeerConnection = useCallback(() => {
        console.log("Creating new RTCPeerConnection");
        if (pc.current) {
            closeConnection();
        }

        try {
            const newPc = new RTCPeerConnection(rtcConfiguration);

            newPc.addEventListener('icecandidate', (event) => {
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
            });

            newPc.addEventListener('track', (event) => {
                console.log("Track received:", event.track.kind, event.streams);
                if (event.streams && event.streams[0]) {
                    console.log("Attaching stream:", event.streams[0].id);
                    setStream(event.streams[0]);
                } else {
                    const newStream = new MediaStream();
                    newStream.addTrack(event.track);
                    setStream(newStream);
                    console.log("Created new stream for track:", newStream.id);
                }
            });

            newPc.addEventListener('connectionstatechange', () => {
                console.log("PeerConnection state changed:", newPc.connectionState);
                setConnectionState(newPc.connectionState);
                if (newPc.connectionState === 'failed') {
                    setError("WebRTC connection failed.");
                }
            });

            newPc.addEventListener('iceconnectionstatechange', () => {
                console.log("ICE Connection state changed:", newPc.iceConnectionState);
                setIceConnectionState(newPc.iceConnectionState);
            });

            newPc.addEventListener('icegatheringstatechange', () => {
                console.log("ICE Gathering state changed:", newPc.iceGatheringState);
                setIceGatheringState(newPc.iceGatheringState);

                if (newPc.iceGatheringState === 'complete') {
                    processMessageQueue();
                }
            });

            newPc.addEventListener('signalingstatechange', () => {
                console.log("Signaling state changed:", newPc.signalingState);
                setSignalingState(newPc.signalingState);

                if (newPc.signalingState === 'stable') {
                    setTimeout(processMessageQueue, 0);
                }
            });

            newPc.addEventListener('datachannel', (event) => {
                console.log("ondatachannel event received!");
                setupDataChannel(event.channel);
            });

            pc.current = newPc;
            setError(null);
            setConnectionState('new');
            setIceConnectionState('new');
            setIceGatheringState('new');
            setSignalingState('stable');
            setIsDataChannelOpen(false);
            setRemoteProfile(null);
            startTimeRef.current = null;

        } catch (e: any) {
            console.error("Failed to create RTCPeerConnection:", e);
            setError(`Failed to create PeerConnection: ${e.message}`);
            pc.current = null;
        }
    }, [closeConnection, idToken, peerId, signaling, processMessageQueue, setupDataChannel]);

    const waitForIceGatheringComplete = useCallback((peerConnection: RTCPeerConnection): Promise<void> => {
        return new Promise((resolve) => {
            if (peerConnection.iceGatheringState === 'complete') {
                resolve();
            } else {
                const checkState = () => {
                    if (peerConnection.iceGatheringState === 'complete') {
                        peerConnection.removeEventListener('icegatheringstatechange', checkState);
                        resolve();
                    }
                };
                peerConnection.addEventListener('icegatheringstatechange', checkState);
            }
        });
    }, []);

    const processSignalingMessage = useCallback(async (message: SignalingMessage) => {
        if (!pc.current && message.type !== PeerMessageType.OFFER) {
            console.warn("Received signaling message but PeerConnection is not initialized:", message.type);
            return;
        }

        try {
            switch (message.type) {
                case PeerMessageType.OFFER:
                    if (message.sdp && message.from === peerId) {
                        console.log("Processing OFFER from", message.from);

                        if (!pc.current) {
                            console.warn("Received offer but PC not ready, creating PC first.");
                            createPeerConnection();
                        }
                        if (!pc.current) {
                            throw new Error("Failed to create PeerConnection before handling offer.");
                        }

                        if (pc.current.signalingState === 'stable' ||
                            pc.current.signalingState === 'have-local-pranswer') {

                            await pc.current.setRemoteDescription(new RTCSessionDescription({
                                type: 'offer',
                                sdp: message.sdp
                            }));
                            console.log("Remote description (offer) set.");

                            const answer = await pc.current.createAnswer();
                            await pc.current.setLocalDescription(answer);
                            console.log("Local description (answer) set.");

                            // Wait for ICE gathering to complete
                            await waitForIceGatheringComplete(pc.current);
                            console.log("ICE gathering completed, sending answer");

                            if (idToken && peerId) {
                                if (pc.current.localDescription?.sdp) {
                                    const answerMsg: AnswerMessage = {
                                        type: PeerMessageType.ANSWER,
                                        to: peerId,
                                        sdp: pc.current.localDescription.sdp,
                                        token: idToken,
                                    };
                                    signaling?.sendMessage(answerMsg);
                                    console.log("Sent ANSWER");
                                } else {
                                    console.error("Failed to create answer with SDP.");
                                    setError("Failed to create valid answer SDP.");
                                    closeConnection();
                                }
                            }
                        } else {
                            console.warn(`Cannot set remote offer in current signaling state: ${pc.current.signalingState}`);
                            messageQueue.current.unshift(message);
                        }
                    }
                    break;

                case PeerMessageType.ICE_CANDIDATE:
                    if (message.candidate && message.from === peerId && pc.current) {
                        console.log("Processing ICE candidate from", message.from);
                        if (pc.current.remoteDescription) {
                            await pc.current.addIceCandidate(new RTCIceCandidate(message.candidate));
                            console.log("Added ICE candidate.");
                        } else {
                            console.warn("Received ICE candidate before remote description was set. Queuing for later.");
                            messageQueue.current.push(message);
                        }
                    }
                    break;

                case PeerMessageType.ANSWER:
                    if (message.sdp && 'from' in message && message.from === peerId && pc.current) {
                        console.log("Processing ANSWER from", message.from);

                        if (pc.current.signalingState === 'have-local-offer') {
                            await pc.current.setRemoteDescription(new RTCSessionDescription({
                                type: 'answer',
                                sdp: message.sdp
                            }));
                            console.log("Remote description (answer) set.");
                        } else {
                            console.warn(`Cannot set remote answer in current signaling state: ${pc.current.signalingState}`);
                            messageQueue.current.push(message);
                        }
                    }
                    break;

                default:
                    break;
            }
        } catch (e: any) {
            console.error("Error processing signaling message:", message.type, e);
            setError(`Error processing ${message.type}: ${e.message}`);
            if (e.message.includes("Cannot set local answer when createAnswer has not been called") ||
                e.message.includes("Cannot create answer in signaling state")) {
                console.error("Fatal signaling error, closing connection");
                closeConnection();
            }
        }
    }, [peerId, createPeerConnection, idToken, signaling, closeConnection, waitForIceGatheringComplete]);

    const handleSignalingMessage = useCallback((message: SignalingMessage) => {
        messageQueue.current.push(message);

        if (!isProcessingMessage.current) {
            processMessageQueue();
        }
    }, [processMessageQueue]);

    useEffect(() => {
        if (!signaling || !peerId) return;

        const unsubscribe = signaling.subscribeToPeerMessages(peerId, handleSignalingMessage);
        const unsubscribeDisconnect = signaling.subscribeToPeerDisconnect(peerId, () => {
            console.log(`Detected disconnect for peer ${peerId} from signaling.`);
            closeConnection();
        });

        return () => {
            unsubscribe();
            unsubscribeDisconnect();
        };
    }, [signaling, peerId, handleSignalingMessage, closeConnection]);

    const requestStream = useCallback((config: Config) => {
        if (peerId && idToken && signaling) {
            if (!pc.current && !peerConnectionCreated.current) {
                peerConnectionCreated.current = true;
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
    }, [peerId, idToken, signaling]);

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
        iceConnectionState,
        iceGatheringState,
        signalingState,
        isDataChannelOpen,
        error,
        remoteProfile,
        requestStream,
        closeConnection,
        sendMessage: sendDataChannelMessage,
    };
}