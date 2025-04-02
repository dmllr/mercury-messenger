import React, { createContext, useState, ReactNode, useMemo } from 'react';
import { User } from 'firebase/auth';

// Interface defines the shape of the context value
interface AuthContextProps {
    user: User | null;
    setUser: (user: User | null) => void;
    loading: boolean;
    setLoading: (loading: boolean) => void;
    idToken: string | null;
    setIdToken: (token: string | null) => void;
}

// Create the context with the defined type (or undefined initially)
export const AuthContext = createContext<AuthContextProps | undefined>(undefined);

interface AuthProviderProps {
    children: ReactNode;
}

// Provider component implementation
export const AuthProvider: React.FC<AuthProviderProps> = ({ children }) => {
    // State definitions are inside the component body
    const [user, setUser] = useState<User | null>(null);
    const [loading, setLoading] = useState<boolean>(true); // Start loading initially
    const [idToken, setIdToken] = useState<string | null>(null);

    // Memoize the context value object
    // Ensure all state values and stable setters provided in the value are listed
    // Setters from useState are stable and don't strictly need to be deps, but including them is fine.
    const contextValue = useMemo(() => ({
        user,
        setUser,
        loading,
        setLoading,
        idToken,
        setIdToken
    }), [user, loading, idToken]); // Dependencies are the state values that cause the value object to change

    return (
        // Provide the memoized value
        <AuthContext.Provider value={contextValue}>
            {children}
        </AuthContext.Provider>
    );
};