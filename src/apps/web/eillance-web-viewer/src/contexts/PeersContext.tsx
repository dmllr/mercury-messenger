import React, { createContext, useState, ReactNode, useContext, useMemo } from 'react';
import { Peer } from '@/types/peer';

interface PeersContextProps {
    peers: Peer[];
    setPeers: (peers: Peer[] | ((prevPeers: Peer[]) => Peer[])) => void; // Allow functional updates
    loading: boolean;
    setLoading: (loading: boolean) => void;
    selectedPeerId: string | null;
    setSelectedPeerId: (peerId: string | null) => void;
    selectedPeer: Peer | null; // Derived state for convenience
    setSelectedPeer: (peerId: string | null) => void; // Simpler alias
}

export const PeersContext = createContext<PeersContextProps | undefined>(undefined);

interface PeersProviderProps {
    children: ReactNode;
}

export const PeersProvider: React.FC<PeersProviderProps> = ({ children }) => {
    const [peers, setPeers] = useState<Peer[]>([]);
    const [loading, setLoading] = useState<boolean>(false); // Start not loading
    const [selectedPeerId, setSelectedPeerId] = useState<string | null>(null);

    // Derived state: find the selected peer object based on ID
    const selectedPeer = useMemo(() => {
        return peers.find(p => p.id === selectedPeerId) || null;
    }, [peers, selectedPeerId]);

    // Provide a simpler function alias if needed, maps directly to setting the ID
    const setSelectedPeer = (peerId: string | null) => {
        setSelectedPeerId(peerId);
    };

    return (
        <PeersContext.Provider value={{
            peers,
            setPeers,
            loading,
            setLoading,
            selectedPeerId,
            setSelectedPeerId,
            selectedPeer,
            setSelectedPeer // Provide the alias
        }}>
            {children}
        </PeersContext.Provider>
    );
};

// Custom hook for easy access
export const usePeers = (): PeersContextProps => {
    const context = useContext(PeersContext);
    if (context === undefined) {
        throw new Error('usePeers must be used within a PeersProvider');
    }
    return context;
};