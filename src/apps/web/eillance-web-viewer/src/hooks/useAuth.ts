import { useEffect, useContext } from 'react';
import { onAuthStateChanged, signOut, getIdToken } from 'firebase/auth';
import { auth } from '@/services/firebaseConfig';
import { AuthContext } from '@/contexts/AuthContext';

export function useAuth() {
    const context = useContext(AuthContext);
    if (context === undefined) {
        throw new Error('useAuth must be used within an AuthProvider');
    }
    const { user, setUser, loading, setLoading, idToken, setIdToken } = context;

    useEffect(() => {
        const unsubscribe = onAuthStateChanged(auth, async (currentUser) => {
            setLoading(true);
            setUser(currentUser);
            if (currentUser) {
                try {
                    const token = await getIdToken(currentUser, true); // Force refresh token
                    setIdToken(token);
                    console.log("Firebase ID Token refreshed.");
                } catch (error) {
                    console.error("Error getting ID token:", error);
                    setIdToken(null); // Clear token on error
                }
            } else {
                setIdToken(null); // Clear token if logged out
            }
            setLoading(false);
        });

        // Cleanup subscription on unmount
        return () => unsubscribe();
    }, [setUser, setLoading, setIdToken]); // Dependencies from context

    // Optional: Refresh token periodically (e.g., every 30 minutes)
    useEffect(() => {
        const interval = setInterval(async () => {
            if (user) {
                try {
                    const token = await getIdToken(user, true); // Force refresh
                    setIdToken(token);
                    console.log('Firebase ID Token refreshed periodically.');
                } catch (error) {
                    console.error('Error refreshing token periodically:', error);
                    // Handle error, maybe logout user if token refresh fails persistently
                    // await logout(); // Example: logout on refresh failure
                }
            }
        }, 30 * 60 * 1000); // 30 minutes

        return () => clearInterval(interval); // Cleanup interval
    }, [user, setIdToken]); // Rerun when user changes

    const logout = async () => {
        setLoading(true);
        try {
            await signOut(auth);
            // Auth state change will be picked up by onAuthStateChanged listener
            console.log("User signed out.");
        } catch (error) {
            console.error("Error signing out:", error);
            setLoading(false); // Ensure loading state is reset on error
        }
        // setLoading(false) will be handled by the onAuthStateChanged listener
    };

    return { user, loading, idToken, logout };
}