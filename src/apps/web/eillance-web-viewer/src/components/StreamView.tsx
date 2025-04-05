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

    // --- Effects ---

    // Request stream on mount
    useEffect(() => {
        if (peer.id && !streamRequestedRef.current) {
            streamRequestedRef.current = true;

            // Initial request - might use default config or first available
            const initialConfig = composeConfiguration();
            console.log("Requesting stream with initial config:", initialConfig);
            requestStream(initialConfig);
        }

        // Only clean up on component unmount
        return () => {
            console.log("Closing WebRTC connection for peer:", peer.id);
            closeConnection();
            // Reset state for remount
            streamRequestedRef.current = false;
        };
    }, [peer.id]);

    // Update local profile when received over data channel
    useEffect(() => {
        if (remoteProfile && remoteProfile.length > 0) {
            console.log("Received updated profile:", remoteProfile);
            setLocalProfile(remoteProfile);
            // Reset selections if necessary, or try to maintain them
            if (!remoteProfile.find(cam => cam.id === selectedCameraId)) {
                const firstCam = remoteProfile[0];
                setSelectedCameraId(firstCam?.id);
                setSelectedResolution(firstCam?.resolutions?.[0]);
            } else {
                const currentCam = remoteProfile.find(cam => cam.id === selectedCameraId);
                if (!currentCam?.resolutions.find(res => res.width === selectedResolution?.width && res.height === selectedResolution?.height)) {
                    setSelectedResolution(currentCam?.resolutions?.[0]);
                }
            }
        }
    }, [remoteProfile, selectedCameraId, selectedResolution]);


    // Attach stream to video element
    useEffect(() => {
        if (stream && videoRef.current) {
            console.log("Attaching stream to video element");
            videoRef.current.srcObject = stream;
        } else if (!stream && videoRef.current) {
            videoRef.current.srcObject = null; // Clear stream if it becomes null
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
        } else {
            document.exitFullscreen();
        }
    };

    const rotateVideo = () => {
        setRotation((prev) => (prev + 90) % 360);
    };

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
            config.video = { cameraId: 0, width: 0, height: 0 };
        }
        // Audio config is usually empty for viewer->streamer requests based on C++
        config.audio = {};
        return config;
    }, [selectedCameraId, selectedResolution, localProfile]);


    const sendConfigurationRequest = useCallback(() => {
        if (!isDataChannelOpen) {
            console.warn("Data channel not open, cannot send configuration.");
            // Optionally show feedback to the user
            return;
        }
        const config = composeConfiguration();
        const message = {
            type: "configuration",
            config: config,
        };
        console.log("Sending configuration request:", message);
        sendDataChannelMessage(JSON.stringify(message));
    }, [isDataChannelOpen, composeConfiguration, sendDataChannelMessage]);

    const handleCameraChange = (event: SelectChangeEvent<string>) => {
        const newCamId = event.target.value;
        const newCam = localProfile?.find(cam => cam.id === newCamId);
        setSelectedCameraId(newCamId);
        setSelectedResolution(newCam?.resolutions?.[0]); // Select first resolution of new camera
        // Send config update immediately after selection
        sendConfigurationRequest();
    };

    const handleResolutionChange = (event: SelectChangeEvent<string>) => {
        const [widthStr, heightStr] = event.target.value.split(RESOLUTION_SEPARATOR);
        const width = parseInt(widthStr, 10);
        const height = parseInt(heightStr, 10);
        const newRes = localProfile?.find(cam => cam.id === selectedCameraId)
            ?.resolutions.find(res => res.width === width && res.height === height);
        setSelectedResolution(newRes);
        // Send config update immediately after selection
        sendConfigurationRequest();
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
                    <FormControl size="small" sx={{ minWidth: 150, flexGrow: 1 }} disabled={(localProfile?.length ?? 0) <= 1 || !isDataChannelOpen}>
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
                    <FormControl size="small" sx={{ minWidth: 150, flexGrow: 1 }} disabled={!currentCamera || (currentResolutions?.length ?? 0) <= 1 || !isDataChannelOpen}>
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