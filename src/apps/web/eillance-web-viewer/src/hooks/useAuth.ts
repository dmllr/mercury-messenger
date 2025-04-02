import { useEffect, useContext, useRef } from 'react';
import { onAuthStateChanged, signOut, getIdToken } from 'firebase/auth';
import { auth } from '@/services/firebaseConfig';
import { AuthContext } from '@/contexts/AuthContext';

export function useAuth() {
    const context = useContext(AuthContext);
    if (context === undefined) {
        throw new Error('useAuth must be used within an AuthProvider');
    }
    // Use context state primarily for *providing* the values
    const { user, setUser, loading, setLoading, idToken, setIdToken } = context;

    // Ref to track the *previous* user ID to detect actual login/logout transitions
    const previousUserIdRef = useRef<string | null | undefined>(undefined); // Start as undefined

    // --- Log Hook Mount/Unmount ---
    useEffect(() => {
        const hookInstanceId = `useAuth-${Date.now()}`;
        console.log(`%cuseAuth Hook Mounted: ${hookInstanceId}`, 'color: green; font-weight: bold;');
        return () => {
            console.log(`%cuseAuth Hook Unmounted: ${hookInstanceId}`, 'color: red; font-weight: bold;');
        };
    }, []); // Empty dependency array runs only on mount/unmount

    // --- Effect for onAuthStateChanged Listener ---
    useEffect(() => {
        console.log("useAuth: Setting up onAuthStateChanged listener.");
        const listenerId = `listener-${Date.now()}`;

        // Initialize previous user ID ref *before* the first listener run
        previousUserIdRef.current = auth.currentUser?.uid ?? null;

        const unsubscribe = onAuthStateChanged(auth, async (currentUser) => {
            const currentUserId = currentUser?.uid ?? null;
            const previousUserId = previousUserIdRef.current;
            console.log(`useAuth: onAuthStateChanged triggered [${listenerId}]. Prev UserID: ${previousUserId}, Current UserID: ${currentUserId}`);

            // --- Core Logic: Only act on actual state TRANSITIONS ---

            // 1. User Logged IN (was null, now has ID)
            if (currentUserId && !previousUserId) {
                console.log(`useAuth: User logged IN [${listenerId}]. Fetching token...`);
                setLoading(true); // Start loading on login transition
                setUser(currentUser); // Set user immediately
                if (currentUser) {
                    try {
                        // Force refresh ONLY on initial login transition
                        const token = await getIdToken(currentUser, true);
                        console.log(`useAuth: Token fetched successfully on login [${listenerId}].`);
                        setIdToken(token);
                    } catch (error) {
                        console.error(`useAuth: Error getting ID token on login [${listenerId}]:`, error);
                        setIdToken(null); // Clear token on error
                        // Consider logging out the user or showing a critical error
                        // await signOut(auth); // Example: force logout on token failure
                    } finally {
                        setLoading(false); // Stop loading after token attempt
                    }
                } else {
                    // Should ideally not happen if currentUserId is set, but handles edge case
                    console.error(`useAuth: Inconsistent state - User ID found but user object is null [${listenerId}].`);
                    setIdToken(null);
                }
            }
            // 2. User Logged OUT (had ID, now is null)
            else if (!currentUserId && previousUserId) {
                console.log(`useAuth: User logged OUT [${listenerId}]. Clearing state...`);
                setLoading(true); // Briefly indicate state change
                setUser(null);
                setIdToken(null);
                setLoading(false);
            }
                // 3. User stayed logged in (ID might be same or different if account changed, but not null->null)
                // OR User stayed logged out (null -> null)
            // OR Firebase SDK internal update (user object ref changed but UID is the same)
            else {
                // Check if the user object reference itself changed even if UID is the same
                if (currentUserId === previousUserId && user !== currentUser ) {
                    console.log(`useAuth: User object updated by SDK, but UID (${currentUserId}) unchanged. Updating user context state only. [${listenerId}]`);
                    setUser(currentUser); // Update user object in context if needed
                } else {
                    console.log(`useAuth: Auth state unchanged (still ${currentUserId ? 'logged in' : 'logged out'}) or initial check. No token fetch needed here. [${listenerId}]`);
                }
                // --- DO NOT fetch token here ---
                // Rely on background refresh or periodic refresh
            }

            // Update the ref for the next run *after* processing the current state
            previousUserIdRef.current = currentUserId;
        });

        // Cleanup subscription on unmount
        return () => {
            console.log(`useAuth: Cleaning up onAuthStateChanged listener [${listenerId}].`);
            unsubscribe();
        };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [setUser, setIdToken, setLoading]); // Depend on setters

    // --- Effect for Initial Loading State ---
    // Handles the very initial loading state before the first listener runs
    useEffect(() => {
        // If previousUserIdRef is still undefined, the listener hasn't run yet.
        const initialCheckPending = previousUserIdRef.current === undefined;
        if (initialCheckPending && loading !== initialCheckPending) {
            console.log("useAuth: Setting initial loading state.");
            setLoading(initialCheckPending);
        } else if (!initialCheckPending && loading) {
            // If listener has run, but we're still marked as loading (e.g. from context), correct it.
            // This handles the case where the initial state is 'logged out'.
            console.log("useAuth: Initial check done, correcting loading state if needed.");
            setLoading(false);
        }

        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [loading, setLoading]); // Depend on loading state from context


    // --- Effect for Periodic Refresh ---
    useEffect(() => {
        console.log('useAuth: Setting up periodic token refresh interval.');
        const interval = setInterval(async () => {
            const currentUser = auth.currentUser; // Check *inside* interval
            if (currentUser) {
                console.log('useAuth: Performing periodic token refresh check...');
                try {
                    // Force refresh periodically
                    const newToken = await getIdToken(currentUser, true);
                    // Compare with the current token from context state
                    if (idToken !== newToken) {
                         console.log('useAuth: Firebase ID Token refreshed periodically.');
                         setIdToken(newToken); // Call with the new token value
                    } else {
                        // console.log('useAuth: Periodic token check - token unchanged.'); // Verbose
                    }
                 } catch (error) {
                    // Don't clear token here, might be temporary network issue
                    console.error('useAuth: Error refreshing token periodically:', error);
                }
            } else {
                // console.log('useAuth: Periodic token check - User not logged in.'); // Verbose
            }
        }, 30 * 60 * 1000); // 30 minutes (Consider increasing to 45-55 mins)

        return () => {
            console.log('useAuth: Clearing periodic token refresh interval.');
            clearInterval(interval);
        };
        // eslint-disable-next-line react-hooks/exhaustive-deps
    }, [setIdToken]); // Depends only on stable setter

    // Logout function
    const logout = async () => {
        console.log("useAuth: Attempting sign out via logout function...");
        // No need to setLoading(true) here, listener will handle state changes
        try {
            await signOut(auth);
            console.log("useAuth: Sign out successful.");
            // The onAuthStateChanged listener will detect the change to null user
        } catch (error) {
            console.error("useAuth: Error signing out:", error);
            // Maybe set an error state?
        }
    };

    // Return the state from the context
    return { user, loading, idToken, logout };
}