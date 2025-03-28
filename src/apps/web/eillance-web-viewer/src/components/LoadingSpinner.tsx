import { Box, CircularProgress, Typography } from '@mui/material';

interface LoadingSpinnerProps {
    message?: string;
}

function LoadingSpinner({ message = "Loading..." }: LoadingSpinnerProps) {
    return (
        <Box
            display="flex"
            flexDirection="column"
            justifyContent="center"
            alignItems="center"
            minHeight="100vh" // Take full viewport height
            sx={{ bgcolor: 'background.default', color: 'text.primary' }} // Use theme colors
        >
            <CircularProgress />
            <Typography variant="h6" sx={{ mt: 2 }}>
                {message}
            </Typography>
        </Box>
    );
}

export default LoadingSpinner;