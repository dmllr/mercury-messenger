import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { Box, Typography, Button, Paper, Alert, CircularProgress } from '@mui/material';
import { useAuth } from '@/hooks/useAuth';
// import { auth } from '@/services/firebaseConfig'; // No longer needed here
import { deleteUser } from 'firebase/auth';

const DeleteAccountPage: React.FC = () => {
    const navigate = useNavigate();
    const { user, logout } = useAuth(); // user object is auth.currentUser
    const [isDeleting, setIsDeleting] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [successMessage, setSuccessMessage] = useState<string | null>(null);

    const handleDeleteAccount = async () => {
        if (!user) {
            setError("You must be logged in to delete your account.");
            return;
        }

        if (!window.confirm("Are you absolutely sure you want to delete your account? This action is permanent and cannot be undone.")) {
            return;
        }

        setIsDeleting(true);
        setError(null);
        setSuccessMessage(null);

        try {
            // The `user` object from useAuth() is firebase.auth().currentUser
            await deleteUser(user);
            setSuccessMessage("Your account has been successfully deleted. You will be logged out.");
            setTimeout(async () => {
                await logout();
                navigate('/auth/signin', { replace: true });
            }, 3000);
        } catch (err: any) {
            console.error("Error deleting account:", err);
            if (err.code === 'auth/requires-recent-login') {
                setError("This operation is sensitive and requires recent authentication. Please sign out and sign back in, then try again.");
            } else {
                setError(err.message || "An unexpected error occurred while deleting your account.");
            }
            setIsDeleting(false);
        }
    };

    const handleCancel = () => {
        navigate(-1);
    };

    return (
        <Box
            display="flex"
            justifyContent="center"
            alignItems="center"
            minHeight="calc(100vh - 64px)"
            p={3}
        >
            <Paper elevation={3} sx={{ padding: { xs: 2, sm: 4 }, maxWidth: 600, width: '100%', textAlign: 'center' }}>
                <Typography variant="h4" component="h1" gutterBottom color="error.main">
                    Delete Your Account
                </Typography>
                <Typography variant="body1" paragraph sx={{ my: 2 }}>
                    We're sorry to see you go. Please read the following carefully before proceeding.
                </Typography>
                <Alert severity="warning" sx={{ textAlign: 'left', mb: 3 }}>
                    <Typography variant="h6">Important Considerations:</Typography>
                    <ul>
                        <li><strong>This action is permanent and irreversible.</strong> Once your account is deleted, it cannot be recovered.</li>
                        <li>All your authentication details with Eillance Viewer will be removed from our system.</li>
                        <li>Any settings or preferences associated with your account (if applicable in the future) will be lost.</li>
                    </ul>
                </Alert>

                {error && <Alert severity="error" sx={{ mb: 2 }}>{error}</Alert>}
                {successMessage && <Alert severity="success" sx={{ mb: 2 }}>{successMessage}</Alert>}

                {user && !successMessage && (
                    <Box sx={{ mt: 3, display: 'flex', flexDirection: { xs: 'column', sm: 'row' }, justifyContent: 'center', gap: 2 }}>
                        <Button
                            variant="outlined"
                            color="primary"
                            onClick={handleCancel}
                            disabled={isDeleting}
                        >
                            Cancel
                        </Button>
                        <Button
                            variant="contained"
                            color="error"
                            onClick={handleDeleteAccount}
                            disabled={isDeleting}
                            startIcon={isDeleting ? <CircularProgress size={20} color="inherit" /> : null}
                        >
                            {isDeleting ? 'Deleting...' : 'Yes, Delete My Account'}
                        </Button>
                    </Box>
                )}
                {!user && !successMessage && (
                    <Typography variant="body1" color="text.secondary" sx={{mt: 2}}>
                        Please sign in to manage your account.
                    </Typography>
                )}
            </Paper>
        </Box>
    );
};

export default DeleteAccountPage;