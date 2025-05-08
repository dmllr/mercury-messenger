import React, { useEffect, useState } from 'react';
import StyledFirebaseAuth from 'react-firebaseui/StyledFirebaseAuth';
import { EmailAuthProvider, GoogleAuthProvider } from 'firebase/auth';
import { auth } from '@/services/firebaseConfig'; // This should now work
import { Box, Typography, Paper } from '@mui/material';

// Configure FirebaseUI.
const uiConfig = {
    signInFlow: 'popup',
    signInOptions: [
        EmailAuthProvider.PROVIDER_ID,
        GoogleAuthProvider.PROVIDER_ID,
    ],
    callbacks: {
        signInSuccessWithAuthResult: () => false,
    },
};

// Wrap the component function with React.memo
const AuthScreen: React.FC = React.memo(() => {
    const [renderAuth, setRenderAuth] = useState(false);

    useEffect(() => {
        setRenderAuth(true);
    }, []);


    return (
        <Box
            display="flex"
            justifyContent="center"
            alignItems="center"
            minHeight="100vh"
            sx={{ bgcolor: 'background.default' }}
        >
            <Paper elevation={3} sx={{ padding: 4, textAlign: 'center' }}>
                <Typography variant="h5" component="h1" gutterBottom>
                    Sign In to Eillance Viewer
                </Typography>
                {renderAuth ? (
                    <StyledFirebaseAuth uiConfig={uiConfig} firebaseAuth={auth} />
                ) : (
                    <Typography>Loading authentication...</Typography>
                )}
            </Paper>
        </Box>
    );
});

export default AuthScreen;