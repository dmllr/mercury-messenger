import { useEffect, useContext, useRef } from 'react';
import { onAuthStateChanged, signOut, getIdToken, User } from 'firebase/auth'; // Ensure User is imported
import { auth } from '@/services/firebaseConfig'; // This should now work
import { AuthContext } from '@/contexts/AuthContext';

export function useAuth() {
    const context = useContext(AuthContext);
    if (context === undefined) {
        throw new Error('useAuth must be used within an AuthProvider');
    }
    const { user, setUser, loading, setLoading, idToken, setIdToken } = context;
    const previousUserIdRef = useRef<string | null | undefined>(undefined);

    useEffect(() => {
        const hookInstanceId = `useAuth-${Date.now()}`;
        console.log(`%cuseAuth Hook Mounted: ${hookInstanceId}`, 'color: green; font-weight: bold;');
        return () => {
            console.log(`%cuseAuth Hook Unmounted: ${hookInstanceId}`, 'color: red; font-weight: bold;');
        };
    }, []);

    useEffect(() => {
        console.log("useAuth: Setting up onAuthStateChanged listener.");
        const listenerId = `listener-${Date.now()}`;
        previousUserIdRef.current = auth.currentUser?.uid ?? null;

        const unsubscribe = onAuthStateChanged(auth, async (currentUser: User | null) => {
            const currentUserId = currentUser?.uid ?? null;
            const previousUserId = previousUserIdRef.current;
            console.log(`useAuth: onAuthStateChanged triggered [${listenerId}]. Prev UserID: ${previousUserId}, Current UserID: ${currentUserId}`);

            if (currentUserId && !previousUserId) {
                console.log(`useAuth: User logged IN [${listenerId}]. Fetching token...`);
                setLoading(true);
                setUser(currentUser);
                if (currentUser) {
                    try {
                        const token = await getIdToken(currentUser, true);
                        console.log(`useAuth: Token fetched successfully on login [${listenerId}].`);
                        setIdToken(token);
                    } catch (error) {
                        console.error(`useAuth: Error getting ID token on login [${listenerId}]:`, error);
                        setIdToken(null);
                    } finally {
                        setLoading(false);
                    }
                } else {
                    console.error(`useAuth: Inconsistent state - User ID found but user object is null [${listenerId}].`);
                    setIdToken(null);
                }
            }
            else if (!currentUserId && previousUserId) {
                console.log(`useAuth: User logged OUT [${listenerId}]. Clearing state...`);
                setLoading(true);
                setUser(null);
                setIdToken(null);
                setLoading(false);
            }
            else {
                if (currentUserId === previousUserId && user !== currentUser ) {
                    console.log(`useAuth: User object updated by SDK, but UID (${currentUserId}) unchanged. Updating user context state only. [${listenerId}]`);
                    setUser(currentUser);
                } else {
                    // console.log(`useAuth: Auth state unchanged (still ${currentUserId ? 'logged in' : 'logged out'}) or initial check. No token fetch needed here. [${listenerId}]`);
                }
            }
            previousUserIdRef.current = currentUserId;
        });
        return () => {
            console.log(`useAuth: Cleaning up onAuthStateChanged listener [${listenerId}].`);
            unsubscribe();
        };
    }, [setUser, setIdToken, setLoading, user]); // Added user to deps of onAuthStateChanged for the user object comparison

    useEffect(() => {
        const initialCheckPending = previousUserIdRef.current === undefined;
        if (initialCheckPending && loading !== initialCheckPending) {
            console.log("useAuth: Setting initial loading state.");
            setLoading(initialCheckPending);
        } else if (!initialCheckPending && loading) {
            console.log("useAuth: Initial check done, correcting loading state if needed.");
            setLoading(false);
        }
    }, [loading, setLoading]);


    useEffect(() => {
        console.log('useAuth: Setting up periodic token refresh interval.');
        const interval = setInterval(async () => {
            const currentUser = auth.currentUser;
            if (currentUser) {
                // console.log('useAuth: Performing periodic token refresh check...'); // Verbose
                try {
                    const newToken = await getIdToken(currentUser, true);
                    if (idToken !== newToken) {
                        console.log('useAuth: Firebase ID Token refreshed periodically.');
                        setIdToken(newToken);
                    }
                } catch (error) {
                    console.error('useAuth: Error refreshing token periodically:', error);
                }
            }
        }, 30 * 60 * 1000);

        return () => {
            console.log('useAuth: Clearing periodic token refresh interval.');
            clearInterval(interval);
        };
    }, [idToken, setIdToken]); // Added idToken to deps of interval

    const logout = async () => {
        console.log("useAuth: Attempting sign out via logout function...");
        try {
            await signOut(auth);
            console.log("useAuth: Sign out successful.");
        } catch (error) {
            console.error("useAuth: Error signing out:", error);
        }
    };
    return { user, loading, idToken, logout };
}