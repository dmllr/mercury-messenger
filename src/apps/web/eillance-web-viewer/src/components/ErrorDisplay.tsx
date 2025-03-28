import { Box, Typography, Button } from '@mui/material';
import ErrorOutlineIcon from '@mui/icons-material/ErrorOutline';

interface ErrorDisplayProps {
    message: string;
    onRetry?: () => void;
}

function ErrorDisplay({ message, onRetry }: ErrorDisplayProps) {
    return (
        <Box
            display="flex"
            flexDirection="column"
            justifyContent="center"
            alignItems="center"
            minHeight="100vh" // Take full viewport height
            textAlign="center"
            p={3}
            sx={{ bgcolor: 'background.default', color: 'text.primary' }} // Use theme colors
        >
            <ErrorOutlineIcon color="error" sx={{ fontSize: 60, mb: 2 }} />
            <Typography variant="h5" color="error" gutterBottom>
                An Error Occurred
            </Typography>
            <Typography variant="body1" sx={{ mb: 3 }}>
                {message}
            </Typography>
            {onRetry && (
                <Button variant="contained" onClick={onRetry}>
                    Retry
                </Button>
            )}
        </Box>
    );
}

export default ErrorDisplay;