import { Chip, Tooltip, CircularProgress } from '@mui/material';
import {
    CloudDone as CloudDoneIcon, // Online
    CloudOff as CloudOffIcon, // Disconnected / Failed
    CloudQueue as CloudQueueIcon // Establishing
} from '@mui/icons-material';
import { useSignaling } from '@/hooks/useSignaling';
import { ServiceConnectionState } from '@/types/signaling';

function StatusIndicator() {
    const { connectionState } = useSignaling();

    let icon = null;
    let label = "Unknown";
    let color: "success" | "error" | "warning" | "info" | "default" = "default";
    let tooltip = "Signaling Server Connection Status";

    switch (connectionState) {
        case ServiceConnectionState.Establishing:
            icon = <CircularProgress size={16} color="inherit" />;
            label = "Connecting";
            color = "info";
            tooltip = "Attempting to connect to signaling server...";
            break;
        case ServiceConnectionState.Online:
            icon = <CloudDoneIcon fontSize="small" />;
            label = "Online";
            color = "success";
            tooltip = "Connected to signaling server.";
            break;
        case ServiceConnectionState.Failure:
            icon = <CloudOffIcon fontSize="small" />;
            label = "Failed";
            color = "error";
            tooltip = "Failed to connect to signaling server.";
            break;
        case ServiceConnectionState.Disconnected:
            icon = <CloudOffIcon fontSize="small" />;
            label = "Offline";
            color = "warning";
            tooltip = "Disconnected from signaling server.";
            break;
        case ServiceConnectionState.Unknown:
        default:
            icon = <CloudQueueIcon fontSize="small" />;
            label = "Status Unknown";
            color = "default";
            tooltip = "Signaling connection status is unknown.";
    }

    return (
        <Tooltip title={tooltip}>
            {/* Use size="small" for a less intrusive chip */}
            <Chip
                icon={icon}
                label={label}
                color={color}
                size="small"
                variant="outlined" // Use outlined for a cleaner look in the AppBar
                sx={{ mr: 1 }} // Add some margin if needed
            />
        </Tooltip>
    );
}

export default StatusIndicator;