import React, { createContext, useState, ReactNode } from 'react';
import { User } from 'firebase/auth';

interface AuthContextProps {
    user: User | null;
    setUser: (user: User | null) => void;
    loading: boolean;
    setLoading: (loading: boolean) => void;
    idToken: string | null;
    setIdToken: (token: string | null) => void;
}

export const AuthContext = createContext<AuthContextProps | undefined>(undefined);

interface AuthProviderProps {
    children: ReactNode;
}

export const AuthProvider: React.FC<AuthProviderProps> = ({ children }) => {
    const [user, setUser] = useState<User | null>(null);
    const [loading, setLoading] = useState<boolean>(true); // Start loading initially
    const [idToken, setIdToken] = useState<string | null>(null);

    return (
        <AuthContext.Provider value={{ user, setUser, loading, setLoading, idToken, setIdToken }}>
            {children}
        </AuthContext.Provider>
    );
};