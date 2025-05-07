import React from 'react';
import { Navigate, useLocation } from 'react-router-dom';
import { useAuth } from '@/hooks/useAuth';
import LoadingSpinner from './LoadingSpinner';

interface ProtectedRouteProps {
    children: JSX.Element;
}

const ProtectedRoute: React.FC<ProtectedRouteProps> = ({ children }) => {
    const { user, loading: authLoading } = useAuth();
    const location = useLocation();

    if (authLoading) {
        return <LoadingSpinner message="Verifying access..." />;
    }

    if (!user) {
        // User not authenticated, redirect to sign-in page
        // Pass the current location in state so we can redirect back after login
        return <Navigate to="/auth/signin" state={{ from: location }} replace />;
    }

    return children; // User is authenticated, render the protected component
};

export default ProtectedRoute;