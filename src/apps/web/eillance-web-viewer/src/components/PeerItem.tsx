import { ListItem, ListItemButton, ListItemText, ListItemAvatar, Avatar } from '@mui/material';
import VideocamIcon from '@mui/icons-material/Videocam';
import { Peer } from '@/types/peer';

interface PeerItemProps {
    peer: Peer;
    onClick: (peer: Peer) => void;
}

function PeerItem({ peer, onClick }: PeerItemProps) {
    return (
        <ListItem disablePadding>
            <ListItemButton onClick={() => onClick(peer)}>
                <ListItemAvatar>
                    <Avatar>
                        <VideocamIcon />
                    </Avatar>
                </ListItemAvatar>
                <ListItemText primary={peer.name || 'Unnamed Device'} secondary={`ID: ${peer.id}`} />
            </ListItemButton>
        </ListItem>
    );
}

export default PeerItem;