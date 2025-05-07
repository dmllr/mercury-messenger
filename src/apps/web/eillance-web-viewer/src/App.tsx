import { useEffect } from 'react';
import { Routes, Route, useLocation, useNavigate } from 'react-router-dom';
import { CssBaseline, ThemeProvider } from '@mui/material';
import { useAuth } from './hooks/useAuth';
import { useSignaling } from './hooks/useSignaling';
import useThemeDetector from './hooks/useThemeDetector';
import { darkTheme, lightTheme } from './styles/theme';
import AuthScreen from './components/AuthScreen';
import Layout from './components/Layout';
import LoadingSpinner from './components/LoadingSpinner';
import ErrorDisplay from './components/ErrorDisplay';
import DeleteAccountPage from './components/DeleteAccountPage';
import ProtectedRoute from './components/ProtectedRoute';
import { ServiceConnectionState } from '@/types';

// This component encapsulates the signaling logic and main Layout
function AppWithSignalingLayout() {
    const { user, idToken } = useAuth();
    const { connectionState, error: signalingError, connectSignaling } = useSignaling();

    // This ensures that signaling connection attempts only happen when a token is available
    // and the user is authenticated. ProtectedRoute should handle user presence.
    if (!user || !idToken) {
        // This might be hit briefly while idToken is being fetched after login.
        return <LoadingSpinner message="Preparing application..." />;
    }

    // Signaling state handling from original App.tsx logic
    switch (connectionState) {
        case ServiceConnectionState.Online:
            return <Layout />;
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
                    message="Disconnected from signaling server."
                    onRetry={connectSignaling}
                />
            );
        case ServiceConnectionState.Unknown:
            return <LoadingSpinner message="Initializing signaling connection..." />;
        default:
            return <ErrorDisplay message="Invalid signaling state." />;
    }
}

// This component handles routing logic and initial auth loading/redirects
function AppRoutes() {
    const { user, loading: authLoading, idToken } = useAuth();
    const location = useLocation();
    const navigate = useNavigate();

    // Effect for post-login redirection
    useEffect(() => {
        if (!authLoading && user && location.state?.from) {
            const from = location.state.from as { pathname: string; search?: string };
            const targetPath = from.pathname + (from.search || '');
            console.log(`User authenticated. Redirecting to originally requested path: ${targetPath}`);
            navigate(targetPath, { replace: true, state: null }); // Clear location state after redirect
        }
    }, [authLoading, user, location.state, navigate]);

    if (authLoading) {
        return <LoadingSpinner message="Checking authentication..." />;
    }

    // If user is logged in but no idToken yet (brief state, common after login before token is fetched by useAuth)
    // Only show this if NOT on the auth screen itself to avoid flicker during login process
    if (user && !idToken && location.pathname !== "/auth/signin") {
        return <LoadingSpinner message="Retrieving authentication token..." />;
    }

    return (
        <Routes>
            <Route path="/auth/signin" element={<AuthScreen />} />
            <Route
                path="/account/delete"
                element={
                    <ProtectedRoute>
                        <DeleteAccountPage />
                    </ProtectedRoute>
                }
            />
            <Route
                path="/*" // Catch-all for the main app layout and other pages
                element={
                    <ProtectedRoute>
                        <AppWithSignalingLayout />
                    </ProtectedRoute>
                }
            />
        </Routes>
    );
}

// Main App component with ThemeProvider
function App() {
    const isDarkTheme = useThemeDetector();
    const theme = isDarkTheme ? darkTheme : lightTheme;

    return (
        <ThemeProvider theme={theme}>
            <CssBaseline />
            {/* BrowserRouter is now in main.tsx */}
            <AppRoutes />
        </ThemeProvider>
    );
}

export default App;
