import { CssBaseline, ThemeProvider } from '@mui/material'; // Import Box
import { useAuth } from './hooks/useAuth';
import AuthScreen from './components/AuthScreen';
import Layout from './components/Layout';
import { darkTheme, lightTheme } from './styles/theme';
import useThemeDetector from './hooks/useThemeDetector';
import { useSignaling } from './hooks/useSignaling';
import LoadingSpinner from './components/LoadingSpinner';
import ErrorDisplay from './components/ErrorDisplay';
import { ServiceConnectionState } from '@/types';

function App() {
    const { user, loading: authLoading, idToken } = useAuth(); // Get idToken here too
    const {
        connectionState,
        error: signalingError,
        connectSignaling,
    } = useSignaling();
    const isDarkTheme = useThemeDetector();
    const theme = isDarkTheme ? darkTheme : lightTheme;

    // 1. Render Auth Loading Spinner if checking authentication
    if (authLoading) {
        return (
            <ThemeProvider theme={theme}>
                <CssBaseline />
                <LoadingSpinner message="Checking authentication..." />
            </ThemeProvider>
        );
    }

    // 2. Render AuthScreen if user is not logged in (and auth check is complete)
    if (!user) {
        return (
            <ThemeProvider theme={theme}>
                <CssBaseline />
                <AuthScreen />
            </ThemeProvider>
        );
    }

    // 3. User is logged in, now handle Signaling State
    // We need a valid token to connect to signaling
    if (!idToken) {
        // This state might occur briefly between user login and token retrieval
        // Or if token retrieval failed in useAuth
        return (
            <ThemeProvider theme={theme}>
                <CssBaseline />
                <LoadingSpinner message="Retrieving authentication token..." />
            </ThemeProvider>
        );
    }

    // We have user and token, proceed with signaling checks
    let content;
    switch (connectionState) {
        case ServiceConnectionState.Online:
            content = <Layout />;
            break;
        case ServiceConnectionState.Establishing:
            content = <LoadingSpinner message="Connecting to signaling server..." />;
            break;
        case ServiceConnectionState.Failure:
            content = (
                <ErrorDisplay
                    message={`Signaling connection failed: ${signalingError || 'Unknown error'}.`}
                    onRetry={connectSignaling} // Retry connection
                />
            );
            break;
        case ServiceConnectionState.Disconnected:
            // Offer reconnect option
            content = (
                <ErrorDisplay
                    message="Disconnected from signaling server."
                    onRetry={connectSignaling}
                />
            );
            break;
        case ServiceConnectionState.Unknown:
            // Should transition quickly to Establishing if idToken is present (handled by useSignaling effect)
            content = <LoadingSpinner message="Initializing signaling connection..." />;
            break;
        default:
            content = <ErrorDisplay message="Invalid signaling state." />;
    }

    // Render the main app structure with the determined content
    return (
        <ThemeProvider theme={theme}>
            <CssBaseline />
            {/* Render the content determined by signaling state */}
            {content}
        </ThemeProvider>
    );
}

export default App;
