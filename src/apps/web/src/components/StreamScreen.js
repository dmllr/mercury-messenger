// src/components/StreamScreen.js
import React, { useState, useEffect, useRef } from 'react';
import WebRTCService from '../services/webrtc';

const StreamScreen = ({ peer, isFullscreen, setFullscreen, onBack }) => {
    const [statusText, setStatusText] = useState('Connecting...');
    const [rotationDegrees, setRotationDegrees] = useState(0);
    const [videoWidth, setVideoWidth] = useState(0);
    const [videoHeight, setVideoHeight] = useState(0);
    const [cameraOptions, setCameraOptions] = useState(['Default Camera']);
    const [selectedCameraIndex, setSelectedCameraIndex] = useState(0);
    const [resolutionOptions, setResolutionOptions] = useState(['Default Resolution']);
    const [selectedResolutionIndex, setSelectedResolutionIndex] = useState(0);
    const [isShowingControls, setIsShowingControls] = useState(true);

    const videoRef = useRef(null);
    const webrtcServiceRef = useRef(null);
    const controlsTimeoutRef = useRef(null);

    // Initialize WebRTC service
    useEffect(() => {
        webrtcServiceRef.current = new WebRTCService();

        // Set up callbacks
        webrtcServiceRef.current.setStatusCallback((peerId, source, status) => {
            let statusMsg = '';
            switch (status) {
                case 0: statusMsg = 'Unknown'; break;
                case 1: statusMsg = 'Connecting'; break;
                case 2: statusMsg = 'Connected'; break;
                case 3: statusMsg = 'Disconnected'; break;
                case 4: statusMsg = 'Failed'; break;
                default: statusMsg = 'Unknown';
            }
            setStatusText(`${source}: ${statusMsg}`);
        });

        webrtcServiceRef.current.setResolutionCallback((width, height) => {
            setVideoWidth(width);
            setVideoHeight(height);
        });

        // Send initial stream request
        const config = composeConfigurationMessage();
        webrtcServiceRef.current.sendStreamRequest(peer.id, config);

        // Update profile/camera options if available
        if (peer.profile) {
            updateCameraOptions(peer.profile);
        }

        return () => {
            webrtcServiceRef.current.disconnect();
        };
    }, [peer.id]);

    // Handle mouse movement to show/hide controls
    useEffect(() => {
        const handleMouseMove = () => {
            setIsShowingControls(true);

            // Clear existing timeout
            if (controlsTimeoutRef.current) {
                clearTimeout(controlsTimeoutRef.current);
            }

            // Set new timeout to hide controls after 3 seconds
            controlsTimeoutRef.current = setTimeout(() => {
                if (!isFullscreen) return;
                setIsShowingControls(false);
            }, 3000);
        };

        document.addEventListener('mousemove', handleMouseMove);

        return () => {
            document.removeEventListener('mousemove', handleMouseMove);
            if (controlsTimeoutRef.current) {
                clearTimeout(controlsTimeoutRef.current);
            }
        };
    }, [isFullscreen]);

    // Handle remote stream
    useEffect(() => {
        const handleRemoteStream = (stream) => {
            if (videoRef.current && stream) {
                videoRef.current.srcObject = stream;
            }
        };

        if (webrtcServiceRef.current) {
            webrtcServiceRef.current.remoteStreamCallback = handleRemoteStream;
        }

        return () => {
            if (webrtcServiceRef.current) {
                webrtcServiceRef.current.remoteStreamCallback = null;
            }
        };
    }, []);

    // Update camera options when profile changes
    const updateCameraOptions = (profile) => {
        if (!profile || !profile.length) return;

        const names = profile.map(cam => cam.name);
        setCameraOptions(names.length ? names : ['Default Camera']);

        // Update resolution options for first camera
        if (profile.length > 0) {
            updateResolutionOptions(0, profile);
        }
    };

    // Update resolution options when camera selection changes
    const updateResolutionOptions = (cameraIndex, profile) => {
        if (!profile) profile = peer.profile;
        if (!profile || cameraIndex >= profile.length) return;

        const resolutions = profile[cameraIndex].resolutions;
        setResolutionOptions(
            resolutions.map(res => `${res.width}×${res.height}`)
        );
        setSelectedResolutionIndex(0);
    };

    // Create configuration for stream requests
    const composeConfigurationMessage = () => {
        const config = {
            video: {
                cameraId: selectedCameraIndex,
                width: 1280,
                height: 720,
                frameRate: 30,
                bitrate: 2000000
            }
        };

        if (peer.profile && selectedCameraIndex < peer.profile.length) {
            const camera = peer.profile[selectedCameraIndex];

            if (selectedResolutionIndex < camera.resolutions.length) {
                const resolution = camera.resolutions[selectedResolutionIndex];
                config.video.width = resolution.width;
                config.video.height = resolution.height;
            }
        }

        return config;
    };

    // Send configuration request
    const sendConfigurationRequest = () => {
        const config = composeConfigurationMessage();
        webrtcServiceRef.current.sendConfigurationRequest(peer.id, config);
    };

    // Handle camera selection change
    const handleCameraChange = (e) => {
        const index = parseInt(e.target.value);
        setSelectedCameraIndex(index);
        updateResolutionOptions(index);
    };

    // Handle resolution selection change
    const handleResolutionChange = (e) => {
        setSelectedResolutionIndex(parseInt(e.target.value));
    };

    // Rotate video
    const rotateVideo = () => {
        const newRotation = (rotationDegrees + 90) % 360;
        setRotationDegrees(newRotation);
    };

    // Handle back button click
    const handleBack = () => {
        setFullscreen(false);
        onBack();
    };

    // Apply rotation and scaling
    const videoStyle = {
        transform: `rotate(${rotationDegrees}deg)`,
        width: '100%',
        height: '100%',
        objectFit: 'contain'
    };

    return (
        <div className="stream-screen">
            {/* Video element */}
            <video
                ref={videoRef}
                style={videoStyle}
                autoPlay
                playsInline
                onClick={() => setIsShowingControls(!isShowingControls)}
            />

            {/* Status overlay */}
            {statusText && (
                <div className="status-overlay">
                    <div className="status-text">{statusText}</div>
                </div>
            )}

            {/* Controls overlay - visible when isShowingControls is true */}
            <div className={`controls-overlay ${isShowingControls ? 'visible' : 'hidden'}`}>
                {/* Back button */}
                <button className="back-button" onClick={handleBack}>
                    Back
                </button>

                {/* Bottom controls */}
                <div className="bottom-controls">
                    {/* Camera selection */}
                    <div className="control-group">
                        <label htmlFor="camera-select">Camera:</label>
                        <select
                            id="camera-select"
                            value={selectedCameraIndex}
                            onChange={handleCameraChange}
                        >
                            {cameraOptions.map((camera, index) => (
                                <option key={index} value={index}>{camera}</option>
                            ))}
                        </select>
                    </div>

                    {/* Resolution selection */}
                    <div className="control-group">
                        <label htmlFor="resolution-select">Resolution:</label>
                        <select
                            id="resolution-select"
                            value={selectedResolutionIndex}
                            onChange={handleResolutionChange}
                        >
                            {resolutionOptions.map((resolution, index) => (
                                <option key={index} value={index}>{resolution}</option>
                            ))}
                        </select>
                    </div>

                    {/* Apply button */}
                    <button className="apply-button" onClick={sendConfigurationRequest}>
                        Apply
                    </button>

                    {/* Rotate button */}
                    <button className="rotate-button" onClick={rotateVideo}>
                        Rotate
                    </button>

                    {/* Fullscreen button */}
                    <button
                        className="fullscreen-button"
                        onClick={() => setFullscreen(!isFullscreen)}
                    >
                        {isFullscreen ? 'Exit Fullscreen' : 'Fullscreen'}
                    </button>
                </div>
            </div>
        </div>
    );
};

export default StreamScreen;