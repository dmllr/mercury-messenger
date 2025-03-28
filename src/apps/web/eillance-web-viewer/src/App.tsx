import { CssBaseline, ThemeProvider } from '@mui/material';
import { useAuth } from './hooks/useAuth';
import AuthScreen from './components/AuthScreen';
import Layout from './components/Layout';
import { darkTheme, lightTheme } from './styles/theme';
import useThemeDetector from './hooks/useThemeDetector';
import { useSignaling } from './hooks/useSignaling';
import LoadingSpinner from './components/LoadingSpinner';
import ErrorDisplay from './components/ErrorDisplay';
import { ServiceConnectionState } from './types/signaling';
import { Box } from '@mui/system'; // Import Box

function App() {
    const { user, loading: authLoading } = useAuth();
    const { connectionState, error: signalingError, connectSignaling } = useSignaling();
    const isDarkTheme = useThemeDetector();
    const theme = isDarkTheme ? darkTheme : lightTheme;

    // Determine the main content based on auth and signaling state
    const renderMainContent = () => {
        // We handle authLoading *after* deciding whether to show AuthScreen or Layout
        // to prevent AuthScreen from flashing if auth is fast.

        if (user) {
            // User is authenticated, handle signaling state
            switch (connectionState) {
                case ServiceConnectionState.Unknown:
                case ServiceConnectionState.Establishing:
                    return <LoadingSpinner message="Connecting to signaling server..." />;
                case ServiceConnectionState.Failure:
                    return (
                        <ErrorDisplay
                            message={`Signaling connection failed: ${signalingError || 'Unknown error'}.`}
                            onRetry={connectSignaling}
                        />
                    );
                case ServiceConnectionState.Disconnected:
                    return (
                        <ErrorDisplay
                            message={`Disconnected from signaling server.`}
                            onRetry={connectSignaling}
                        />
                    );
                case ServiceConnectionState.Online:
                    return <Layout />; // Show the main app layout
                default:
                    return <ErrorDisplay message="Invalid signaling state." />;
            }
        } else {
            // User is not authenticated (and not loading auth)
            // We *don't* render AuthScreen directly here anymore.
            // It will be handled below. We can render a placeholder or nothing.
            return null; // Or potentially a minimal loading/placeholder if needed
        }
    };

    return (
        <ThemeProvider theme={theme}>
            <CssBaseline />

            {/* Always include AuthScreen, but control its visibility */}
            {/* Use sx prop for conditional display */}
            <Box sx={{ display: !user && !authLoading ? 'block' : 'none' }}>
                <AuthScreen />
            </Box>

            {/* Show Loading Spinner *only* during initial auth check */}
            {authLoading && <LoadingSpinner message="Checking authentication..." />}

            {/* Render the main content (Layout or Signaling Status) */}
            {/* Only render this part if auth is not loading */}
            {!authLoading && (
                <Box sx={{ display: user ? 'block' : 'none' }}>
                    {renderMainContent()}
                </Box>
            )}

        </ThemeProvider>
    );
}

export default App;