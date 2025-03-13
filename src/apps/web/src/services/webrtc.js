// src/services/webrtc.js
import { firestore, getCurrentUserToken } from './firebaseService';

export default class WebRTCService {
    constructor() {
        this.peerConnection = null;
        this.localStream = null;
        this.remoteStream = null;
        this.currentPeerId = null;
        this.statusCallbacks = new Map();
        this.resolutionCallback = null;
        this.isInitiator = false;
    }

    async initializeConnection(peerId, isInitiator = false) {
        this.currentPeerId = peerId;
        this.isInitiator = isInitiator;

        // Create a new RTCPeerConnection
        const configuration = {
            iceServers: [
                { urls: 'stun:stun.l.google.com:19302' },
                { urls: 'stun:stun1.l.google.com:19302' }
            ]
        };

        this.peerConnection = new RTCPeerConnection(configuration);

        // Set up event handlers
        this.peerConnection.onicecandidate = event => {
            if (event.candidate) {
                this.sendIceCandidate(peerId, event.candidate);
            }
        };

        this.peerConnection.ontrack = event => {
            this.remoteStream = event.streams[0];
            this.updateStatus('PeerConnectionState', 2); // Connected

            // Get video track to extract resolution
            const videoTrack = this.remoteStream.getVideoTracks()[0];
            if (videoTrack) {
                const settings = videoTrack.getSettings();
                if (settings.width && settings.height && this.resolutionCallback) {
                    this.resolutionCallback(settings.width, settings.height);
                }
            }
        };

        this.peerConnection.oniceconnectionstatechange = () => {
            console.log('ICE connection state:', this.peerConnection.iceConnectionState);
            switch (this.peerConnection.iceConnectionState) {
                case 'connected':
                    this.updateStatus('PeerConnectionState', 2); // Connected
                    break;
                case 'disconnected':
                    this.updateStatus('PeerConnectionState', 3); // Disconnected
                    break;
                case 'failed':
                    this.updateStatus('PeerConnectionState', 4); // Failed
                    break;
                default:
                    break;
            }
        };

        // Listen for remote ICE candidates
        this.listenForIceCandidates(peerId);

        // Listen for offer/answer based on role
        if (isInitiator) {
            this.listenForAnswer(peerId);
        } else {
            this.listenForOffer(peerId);
        }

        return this.peerConnection;
    }

    async startStreaming(videoConfig) {
        try {
            // Get user media with requested constraints
            const constraints = {
                video: {
                    width: { ideal: videoConfig.width || 1280 },
                    height: { ideal: videoConfig.height || 720 },
                    frameRate: { ideal: videoConfig.frameRate || 30 }
                },
                audio: true
            };

            this.localStream = await navigator.mediaDevices.getUserMedia(constraints);

            // Add tracks to the peer connection
            this.localStream.getTracks().forEach(track => {
                this.peerConnection.addTrack(track, this.localStream);
            });

            // Create and send offer if initiator
            if (this.isInitiator) {
                const offer = await this.peerConnection.createOffer();
                await this.peerConnection.setLocalDescription(offer);
                await this.sendOffer(this.currentPeerId, offer);
            }

            return this.localStream;
        } catch (error) {
            console.error('Error starting stream:', error);
            this.updateStatus('PeerConnectionState', 4); // Failed
            throw error;
        }
    }

    async joinStream(peerId) {
        try {
            this.currentPeerId = peerId;
            await this.initializeConnection(peerId, false);

            // Listen for remote stream
            this.peerConnection.ontrack = event => {
                this.remoteStream = event.streams[0];
                this.updateStatus('PeerConnectionState', 2); // Connected

                // Get video track to extract resolution
                const videoTrack = this.remoteStream.getVideoTracks()[0];
                if (videoTrack) {
                    const settings = videoTrack.getSettings();
                    if (settings.width && settings.height && this.resolutionCallback) {
                        this.resolutionCallback(settings.width, settings.height);
                    }
                }
            };

            return this.peerConnection;
        } catch (error) {
            console.error('Error joining stream:', error);
            this.updateStatus('PeerConnectionState', 4); // Failed
            throw error;
        }
    }

    async sendStreamRequest(peerId, config) {
        try {
            await this.joinStream(peerId);

            // Send stream request with configuration
            const token = await getCurrentUserToken();
            await firestore.collection('peers').doc(peerId).collection('requests').add({
                type: 'stream',
                config: config,
                token: token,
                timestamp: new Date()
            });

            this.updateStatus('PeerConnectionState', 1); // Connecting
        } catch (error) {
            console.error('Error sending stream request:', error);
            this.updateStatus('PeerConnectionState', 4); // Failed
        }
    }

    async sendConfigurationRequest(peerId, config) {
        try {
            // Send configuration update
            await firestore.collection('peers').doc(peerId).collection('requests').add({
                type: 'config',
                config: config,
                timestamp: new Date()
            });
        } catch (error) {
            console.error('Error sending configuration request:', error);
        }
    }

    // ICE candidate handling
    async sendIceCandidate(peerId, candidate) {
        try {
            await firestore.collection('peers').doc(peerId).collection('candidates').add({
                candidate: candidate.toJSON(),
                sender: this.isInitiator ? 'initiator' : 'receiver',
                timestamp: new Date()
            });
        } catch (error) {
            console.error('Error sending ICE candidate:', error);
        }
    }

    listenForIceCandidates(peerId) {
        return firestore.collection('peers').doc(peerId).collection('candidates')
            .where('sender', '==', this.isInitiator ? 'receiver' : 'initiator')
            .onSnapshot(snapshot => {
                snapshot.docChanges().forEach(async change => {
                    if (change.type === 'added') {
                        const data = change.doc.data();
                        try {
                            await this.peerConnection.addIceCandidate(new RTCIceCandidate(data.candidate));
                        } catch (error) {
                            console.error('Error adding ICE candidate:', error);
                        }
                    }
                });
            });
    }

    // Offer/Answer handling
    async sendOffer(peerId, offer) {
        try {
            await firestore.collection('peers').doc(peerId).collection('signals').doc('offer').set({
                type: offer.type,
                sdp: offer.sdp,
                timestamp: new Date()
            });
        } catch (error) {
            console.error('Error sending offer:', error);
        }
    }

    async sendAnswer(peerId, answer) {
        try {
            await firestore.collection('peers').doc(peerId).collection('signals').doc('answer').set({
                type: answer.type,
                sdp: answer.sdp,
                timestamp: new Date()
            });
        } catch (error) {
            console.error('Error sending answer:', error);
        }
    }

    listenForOffer(peerId) {
        return firestore.collection('peers').doc(peerId).collection('signals').doc('offer')
            .onSnapshot(async snapshot => {
                if (snapshot.exists) {
                    const data = snapshot.data();
                    try {
                        await this.peerConnection.setRemoteDescription(new RTCSessionDescription({
                            type: data.type,
                            sdp: data.sdp
                        }));

                        // Create and send answer
                        const answer = await this.peerConnection.createAnswer();
                        await this.peerConnection.setLocalDescription(answer);
                        await this.sendAnswer(peerId, answer);
                    } catch (error) {
                        console.error('Error processing offer:', error);
                    }
                }
            });
    }

    listenForAnswer(peerId) {
        return firestore.collection('peers').doc(peerId).collection('signals').doc('answer')
            .onSnapshot(async snapshot => {
                if (snapshot.exists) {
                    const data = snapshot.data();
                    try {
                        await this.peerConnection.setRemoteDescription(new RTCSessionDescription({
                            type: data.type,
                            sdp: data.sdp
                        }));
                    } catch (error) {
                        console.error('Error processing answer:', error);
                    }
                }
            });
    }

    // Status handling
    setStatusCallback(callback) {
        this.statusCallbacks.set('status', callback);
    }

    setResolutionCallback(callback) {
        this.resolutionCallback = callback;
    }

    updateStatus(source, status) {
        const callback = this.statusCallbacks.get('status');
        if (callback) {
            callback(this.currentPeerId, source, status);
        }
    }

    // Cleanup
    disconnect() {
        if (this.localStream) {
            this.localStream.getTracks().forEach(track => track.stop());
        }

        if (this.peerConnection) {
            this.peerConnection.close();
        }

        this.localStream = null;
        this.remoteStream = null;
        this.peerConnection = null;
        this.currentPeerId = null;
    }
}