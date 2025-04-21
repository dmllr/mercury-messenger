import { useRef, useState, useEffect, useCallback } from 'react';
import {
    Box, IconButton, Typography, CircularProgress, Select, MenuItem,
    FormControl, InputLabel, SelectChangeEvent, Tooltip, Paper, Chip
} from '@mui/material';
import {
    Fullscreen as FullscreenIcon, FullscreenExit as FullscreenExitIcon,
    RotateRight as RotateRightIcon, ErrorOutline as ErrorIcon, CloudOff as CloudOffIcon,
    SettingsRemote as SettingsRemoteIcon,
    SignalWifiStatusbar4Bar as PingStrongIcon // Example for ping
} from '@mui/icons-material';
import { Peer, Resolution } from '@/types/peer';
import { useWebRTC } from '@/hooks/useWebRTC';
import { Config } from '@/types/config'; // Assuming Config types are defined
import { DataChannelMessageType } from '@/types/signaling'; // Import DataChannelMessageType

interface StreamViewProps {
    peer: Peer;
    onBack: () => void; // Function to go back to peer list
}

const RESOLUTION_SEPARATOR = "×";

function StreamView({ peer, onBack }: StreamViewProps) {
    const videoRef = useRef<HTMLVideoElement>(null);
    const containerRef = useRef<HTMLDivElement>(null);
    const streamRequestedRef = useRef<boolean>(false);
    const [isFullscreen, setIsFullscreen] = useState(false);
    const [rotation, setRotation] = useState(0); // Local rotation state
    const [showControls, setShowControls] = useState(true); // Show controls initially
    // Initialize selections based on the initial peer profile
    const [selectedCameraId, setSelectedCameraId] = useState<string | undefined>(peer.profile?.[0]?.id);
    const [selectedResolution, setSelectedResolution] = useState<Resolution | undefined>(peer.profile?.[0]?.resolutions?.[0]);
    const [localProfile, setLocalProfile] = useState<Peer['profile']>(peer.profile || []); // Store profile locally
    const controlsTimeoutRef = useRef<number | null>(null);

    const {
        stream,
        connectionState,
        error,
        sendMessage: sendDataChannelMessage,
        requestStream,
        closeConnection,
        isDataChannelOpen,
        remoteProfile, // Profile received over data channel
    } = useWebRTC(peer.id);

    // Helper to compose configuration based on current selections
    const composeConfiguration = useCallback((): Config => {
        const config: Config = { video: {}, audio: {} }; // Initialize with empty objects
        const selectedCam = localProfile?.find(cam => cam.id === selectedCameraId);
        if (selectedCam && selectedResolution) {
            // Find camera index - IMPORTANT: Signaling expects index, not ID
            const camIndex = localProfile?.findIndex(cam => cam.id === selectedCameraId) ?? 0;

            config.video = {
                cameraId: camIndex >= 0 ? camIndex : 0, // Use index
                width: selectedResolution.width,
                height: selectedResolution.height,
                // frameRate and bitrate can be omitted or set to defaults if needed
                // frameRate: 30,
                // bitrate: 1000000,
            };
        } else {
            // Send default or empty config if nothing selected
            // Based on typical streamer implementations, sending { cameraId: 0, width: 0, height: 0 }
            // might be interpreted as 'stop stream' or 'use default'. Clarify with streamer implementation.
            // For now, let's send the specific selections if they exist.
            if (selectedCameraId !== undefined && selectedResolution !== undefined) {
                // This case might indicate an issue in selection logic, but compose defensively.
                // Or if a camera/resolution was selected but then disappeared from profile.
                console.warn("Attempted to compose config with seemingly invalid selections:", { selectedCameraId, selectedResolution });
                // Fallback to requesting the first available camera/resolution if profile exists
                const firstCam = localProfile?.[0];
                if (firstCam) {
                    config.video = {
                        cameraId: 0,
                        width: firstCam.resolutions?.[0]?.width ?? 0,
                        height: firstCam.resolutions?.[0]?.height ?? 0,
                    };
                } else {
                    // No profile, send minimal video config? Or audio only?
                    config.video = { cameraId: 0, width: 0, height: 0 };
                }
            } else {
                // This happens when nothing is selected initially or after profile update doesn't match current selections
                // We might not want to send a config in this specific case, unless we are sure a valid default should be sent.
                // Let the useEffect below handle the send when valid selections ARE present.
                // For now, return an empty config if selections are not valid.
                return { video: {}, audio: {} };
            }
        }
        // Audio config is usually empty for viewer->streamer requests based on C++
        config.audio = {};
        return config;
    }, [selectedCameraId, selectedResolution, localProfile]);


    const sendConfigurationRequest = useCallback(() => {
        // Only send if data channel is open and we have valid selections
        if (!isDataChannelOpen || selectedCameraId === undefined || selectedResolution === undefined) {
            console.warn("Data channel not open or selections incomplete, cannot send configuration.");
            return;
        }
        const config = composeConfiguration();
        // Avoid sending empty config if selections are valid
        if (Object.keys(config.video).length === 0) {
            console.warn("Composed configuration is empty, not sending.");
            return;
        }

        const message = {
            type: DataChannelMessageType.CONFIGURATION, // Use DataChannelMessageType
            config: config,
        };
        console.log("Sending configuration request:", message);
        sendDataChannelMessage(JSON.stringify(message));
    }, [isDataChannelOpen, selectedCameraId, selectedResolution, composeConfiguration, sendDataChannelMessage]); // Dependencies include items used in composeConfiguration and the check

    // --- Effects ---

    // Request stream on mount or peer change
    useEffect(() => {
        // Reset selections and profile when peer changes
        setSelectedCameraId(peer.profile?.[0]?.id);
        setSelectedResolution(peer.profile?.[0]?.resolutions?.[0]);
        setLocalProfile(peer.profile || []);

        if (peer.id && !streamRequestedRef.current) {
            streamRequestedRef.current = true;
            console.log("Initial stream request for peer:", peer.id);
            // The initial config will be sent by the useEffect watching selections and data channel state
            requestStream({video: {}, audio: {}}); // Send an empty config on initial request, streamer might use default
        }

        // Only clean up on component unmount or when peer.id changes significantly
        return () => {
            // Use peer.id in dependency array and clean up when it changes
            console.log("Cleaning up WebRTC connection for peer change or unmount:", peer.id);
            closeConnection();
            // Reset the ref for the next mount/peer change
            streamRequestedRef.current = false;
        };
    }, [peer.id, requestStream, closeConnection]); // Depend on peer.id, requestStream, closeConnection

    // Effect to send configuration when selections or data channel state changes
    useEffect(() => {
        // Send configuration only if data channel is open AND a camera/resolution are selected
        if (isDataChannelOpen && selectedCameraId !== undefined && selectedResolution !== undefined) {
            console.log("Selections or Data Channel state changed, sending configuration...");
            // Add a small delay? Sometimes necessary if state takes a moment to propagate
            const handler = setTimeout(sendConfigurationRequest, 50); // Add a small delay

            return () => clearTimeout(handler); // Cleanup timeout
        } else {
            console.log("Data channel not open or selections incomplete. Not sending config yet.", { isDataChannelOpen, selectedCameraId, selectedResolution });
        }
        // Dependencies: selections, data channel state, and the send function
    }, [selectedCameraId, selectedResolution, isDataChannelOpen, sendConfigurationRequest]);


    // Update local profile when received over data channel
    useEffect(() => {
        if (remoteProfile && remoteProfile.length > 0) {
            console.log("Received updated profile:", remoteProfile);
            setLocalProfile(remoteProfile);

            // Try to maintain the current selection if the camera still exists
            const currentCamStillExists = remoteProfile.find(cam => cam.id === selectedCameraId);

            if (!currentCamStillExists) {
                // If current camera is gone, select the first one in the new profile
                const firstCam = remoteProfile[0];
                setSelectedCameraId(firstCam?.id);
                setSelectedResolution(firstCam?.resolutions?.[0]);
            } else {
                // If current camera still exists, check if the current resolution exists for it
                const currentCam = remoteProfile.find(cam => cam.id === selectedCameraId); // Find it again in the new profile
                const currentResStillExists = currentCam?.resolutions.find(res =>
                    res.width === selectedResolution?.width && res.height === selectedResolution?.height
                );
                if (!currentResStillExists) {
                    // If current resolution is gone for the current camera, select the first resolution
                    setSelectedResolution(currentCam?.resolutions?.[0]);
                }
                // If both camera and resolution still exist, keep the selection.
            }
            // The useEffect watching selectedCameraId/selectedResolution will pick up changes here and send the config.

        } else if (remoteProfile && remoteProfile.length === 0) {
            // Received empty profile, maybe indicate no cameras available
            console.log("Received empty profile.");
            setLocalProfile([]);
            setSelectedCameraId(undefined);
            setSelectedResolution(undefined);
        }
    }, [remoteProfile, selectedCameraId, selectedResolution]);


    // Attach stream to video element
    useEffect(() => {
        if (videoRef.current) {
            if (stream) {
                console.log("Attaching stream to video element");
                videoRef.current.srcObject = stream;
            } else {
                // Clear stream if it becomes null (e.g., on connection close)
                console.log("Stream became null, detaching from video element.");
                videoRef.current.srcObject = null;
            }
        }
    }, [stream]);

    // Handle fullscreen changes
    useEffect(() => {
        const handleFullscreenChange = () => {
            setIsFullscreen(!!document.fullscreenElement);
        };
        document.addEventListener('fullscreenchange', handleFullscreenChange);
        return () => document.removeEventListener('fullscreenchange', handleFullscreenChange);
    }, []);

    // Hide controls after a delay
    const hideControls = useCallback(() => {
        if (controlsTimeoutRef.current) {
            clearTimeout(controlsTimeoutRef.current);
        }
        controlsTimeoutRef.current = window.setTimeout(() => {
            setShowControls(false);
        }, 3000); // Hide after 3 seconds
    }, []);

    // Show controls and set timer to hide them
    const handleInteraction = useCallback(() => {
        setShowControls(true);
        hideControls();
    }, [hideControls]);

    // Initial setup for controls visibility
    useEffect(() => {
        handleInteraction(); // Show controls initially and start timer
        return () => { // Cleanup timeout on unmount
            if (controlsTimeoutRef.current) {
                clearTimeout(controlsTimeoutRef.current);
            }
        };
    }, [handleInteraction]);

    // --- Handlers ---

    const toggleFullscreen = () => {
        if (!document.fullscreenElement && containerRef.current) {
            containerRef.current.requestFullscreen();
        } else if (document.fullscreenElement) {
            document.exitFullscreen();
        }
    };

    const rotateVideo = () => {
        setRotation((prev) => (prev + 90) % 360);
    };


    const handleCameraChange = (event: SelectChangeEvent<string>) => {
        const newCamId = event.target.value;
        const newCam = localProfile?.find(cam => cam.id === newCamId);
        setSelectedCameraId(newCamId);
        // Also update resolution to the first available for the new camera
        setSelectedResolution(newCam?.resolutions?.[0]);
        // The useEffect will handle sending the config
    };

    const handleResolutionChange = (event: SelectChangeEvent<string>) => {
        const value = event.target.value;
        const [widthStr, heightStr] = value.split(RESOLUTION_SEPARATOR);
        const width = parseInt(widthStr, 10);
        const height = parseInt(heightStr, 10);
        const newRes = localProfile?.find(cam => cam.id === selectedCameraId)
            ?.resolutions.find(res => res.width === width && res.height === height);
        setSelectedResolution(newRes);
        // The useEffect will handle sending the config
    };

    // --- Rendering Logic ---

    const renderStatus = () => {
        if (error) {
            return (
                <Chip icon={<ErrorIcon />} label={`Error: ${error}`} color="error" variant="filled" />
            );
        }

        switch (connectionState) {
            case 'connecting':
            case 'new':
                return <Chip icon={<CircularProgress size={20} />} label="Connecting..." color="info" variant="outlined" />;
            case 'connected':
                // Optionally show ICE/Signaling state briefly or only on issues
                // Or use a simple "Connected" chip or Ping indicator
                return <Chip icon={<PingStrongIcon />} label="Connected" color="success" variant="filled" />;
            case 'disconnected':
                return <Chip icon={<CloudOffIcon />} label="Disconnected" color="warning" variant="filled" />;
            case 'failed':
                return <Chip icon={<ErrorIcon />} label="Connection Failed" color="error" variant="filled" />;
            case 'closed':
                return <Chip icon={<CloudOffIcon />} label="Connection Closed" color="default" variant="outlined" />;
            default:
                return <Chip label={`State: ${connectionState}`} variant="outlined" />;
        }
    };

    const currentCamera = localProfile?.find(cam => cam.id === selectedCameraId);
    const currentResolutions = currentCamera?.resolutions || [];
    const resolutionValue = selectedResolution ? `${selectedResolution.width}${RESOLUTION_SEPARATOR}${selectedResolution.height}` : '';


    return (
        <Box
            ref={containerRef}
            sx={{
                position: 'relative',
                width: '100%',
                height: '100%', // Takes full height of parent (Layout's main area)
                bgcolor: 'black',
                overflow: 'hidden', // Hide potential scrollbars
                cursor: showControls ? 'default' : 'none', // Hide cursor when controls are hidden
            }}
            onMouseMove={handleInteraction}
            onClick={handleInteraction} // Show controls on click too
            onTouchStart={handleInteraction} // Show on touch
        >
            <video
                ref={videoRef}
                autoPlay
                playsInline // Important for mobile
                style={{
                    display: 'block', // Remove extra space below video
                    width: '100%',
                    height: '100%',
                    objectFit: 'contain', // Adjust as needed ('cover', 'contain')
                    transform: `rotate(${rotation}deg)`,
                    transition: 'transform 0.3s ease', // Smooth rotation
                    backgroundColor: 'black', // Ensure bg is black
                }}
                muted={false} // Ensure audio plays if available
            />

            {/* Overlay for Controls - Conditionally Rendered */}
            <Box
                sx={{
                    position: 'absolute',
                    top: 0,
                    left: 0,
                    width: '100%',
                    height: '100%',
                    display: 'flex',
                    flexDirection: 'column',
                    justifyContent: 'space-between',
                    pointerEvents: showControls ? 'auto' : 'none', // Allow interaction only when visible
                    opacity: showControls ? 1 : 0,
                    transition: 'opacity 0.5s ease-in-out',
                    // Semi-transparent backgrounds for readability
                    background: 'linear-gradient(to bottom, rgba(0,0,0,0.5) 0%, rgba(0,0,0,0) 20%, rgba(0,0,0,0) 80%, rgba(0,0,0,0.7) 100%)',
                }}
            >
                {/* Top Bar Controls */}
                <Box display="flex" justifyContent="space-between" alignItems="center" p={1}>
                    <Tooltip title="Back to Device List">
                        <IconButton onClick={onBack} sx={{ color: 'white', bgcolor: 'rgba(0,0,0,0.5)', '&:hover': { bgcolor: 'rgba(0,0,0,0.7)'} }}>
                            {/* Use appropriate back icon */}
                            <SettingsRemoteIcon />
                        </IconButton>
                    </Tooltip>
                    <Paper sx={{ p: 0.5, bgcolor: 'rgba(0,0,0,0.6)', borderRadius: 1, display: 'inline-block' }}>
                        {renderStatus()}
                    </Paper>
                    <Box> {/* Placeholder for right-side icons if needed */} </Box>
                </Box>

                {/* Center Area (e.g., for status text if needed, or keep empty) */}
                <Box flexGrow={1} display="flex" justifyContent="center" alignItems="center">
                    {/* Loading indicator while connecting */}
                    { (connectionState === 'connecting' || connectionState === 'new') && !stream &&
                        <CircularProgress color="inherit" sx={{ color: 'white' }} />
                    }
                    {/* Error display if stream fails but connection might be active */}
                    { error &&
                        <Typography color="error" sx={{ bgcolor: 'rgba(0,0,0,0.7)', p:1, borderRadius: 1}}>{error}</Typography>
                    }

                </Box>

                {/* Bottom Bar Controls */}
                <Paper sx={{
                    p: 1,
                    display: 'flex',
                    alignItems: 'center',
                    gap: 1,
                    flexWrap: 'wrap', // Allow wrapping on small screens
                    bgcolor: 'rgba(0,0,0,0.7)', // Semi-transparent background
                    borderTopLeftRadius: 4,
                    borderTopRightRadius: 4,
                    borderBottomLeftRadius: 0, // Square bottom corners
                    borderBottomRightRadius: 0,
                }}>
                    {/* Camera Selection */}
                    <FormControl size="small" sx={{ minWidth: 150, flexGrow: 1 }} disabled={(localProfile?.length ?? 0) <= 1 || !isDataChannelOpen || connectionState !== 'connected'}> {/* Disable if no cameras, DC not open, or not connected */}
                        <InputLabel id="camera-select-label" sx={{ color: 'rgba(255,255,255,0.7)' }}>Camera</InputLabel>
                        <Select
                            labelId="camera-select-label"
                            value={selectedCameraId || ''}
                            label="Camera"
                            onChange={handleCameraChange}
                            sx={{ color: 'white', '.MuiOutlinedInput-notchedOutline': { borderColor: 'rgba(255,255,255,0.5)' }, '& .MuiSvgIcon-root': { color: 'white' } }}
                            MenuProps={{ PaperProps: { sx: { bgcolor: 'grey.800', color: 'white'} } }}
                        >
                            {localProfile?.map((cam) => (
                                <MenuItem key={cam.id} value={cam.id}>{cam.name || `Camera ${cam.id}`}</MenuItem>
                            ))}
                            {localProfile?.length === 0 && <MenuItem value="" disabled>No cameras</MenuItem>}
                        </Select>
                    </FormControl>

                    {/* Resolution Selection */}
                    <FormControl size="small" sx={{ minWidth: 150, flexGrow: 1 }} disabled={!currentCamera || (currentResolutions?.length ?? 0) <= 1 || !isDataChannelOpen || connectionState !== 'connected'}> {/* Disable if no camera/resolutions, DC not open, or not connected */}
                        <InputLabel id="resolution-select-label" sx={{ color: 'rgba(255,255,255,0.7)' }}>Resolution</InputLabel>
                        <Select
                            labelId="resolution-select-label"
                            value={resolutionValue}
                            label="Resolution"
                            onChange={handleResolutionChange}
                            sx={{ color: 'white', '.MuiOutlinedInput-notchedOutline': { borderColor: 'rgba(255,255,255,0.5)' }, '& .MuiSvgIcon-root': { color: 'white' } }}
                            MenuProps={{ PaperProps: { sx: { bgcolor: 'grey.800', color: 'white'} } }}
                        >
                            {currentResolutions.map((res) => (
                                <MenuItem key={`${res.width}x${res.height}`} value={`${res.width}${RESOLUTION_SEPARATOR}${res.height}`}>
                                    {`${res.width} × ${res.height}`}
                                </MenuItem>
                            ))}
                            {currentResolutions.length === 0 && <MenuItem value="" disabled>No resolutions</MenuItem>}
                        </Select>
                    </FormControl>
                    {/* Spacer for small screens when wrapped */}
                    {/* <Box sx={{ width: '100%', height: 0, display: { xs: 'block', sm: 'none' } }} /> */}

                    {/* Action Buttons */}
                    <Tooltip title="Rotate View">
                        <IconButton onClick={rotateVideo} sx={{ color: 'white' }}>
                            <RotateRightIcon />
                        </IconButton>
                    </Tooltip>
                    <Tooltip title={isFullscreen ? 'Exit Fullscreen' : 'Enter Fullscreen'}>
                        <IconButton onClick={toggleFullscreen} sx={{ color: 'white' }}>
                            {isFullscreen ? <FullscreenExitIcon /> : <FullscreenIcon />}
                        </IconButton>
                    </Tooltip>
                </Paper>

            </Box>
        </Box>
    );
}

export default StreamView;
