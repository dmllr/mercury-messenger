import { Box, Typography, CircularProgress, List } from '@mui/material';
import PeerItem from './PeerItem';
import { Peer } from '@/types/peer';
import { usePeers } from '@/contexts/PeersContext';

interface PeerListProps {
    onPeerSelected: (peer: Peer) => void;
}

function PeerList({ onPeerSelected }: PeerListProps) {
    const { peers, loading } = usePeers();

    if (loading) {
        return (
            <Box display="flex" justifyContent="center" alignItems="center" minHeight="200px">
                <CircularProgress />
            </Box>
        );
    }

    if (peers.length === 0) {
        return (
            <Box display="flex" justifyContent="center" alignItems="center" minHeight="200px">
                <Typography variant="h6" color="text.secondary">
                    No registered devices found.
                </Typography>
            </Box>
        );
    }

    return (
        <List>
            {peers.map((peer) => (
                <PeerItem key={peer.id} peer={peer} onClick={() => onPeerSelected(peer)} />
            ))}
        </List>
    );
}

export default PeerList;