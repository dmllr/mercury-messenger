import { useState } from 'react';
import { useNavigate, useLocation } from 'react-router-dom'; // Import useLocation
import { AppBar, Toolbar, Typography, IconButton, Box, Container, Drawer, List, ListItem, ListItemButton, ListItemText, ListItemIcon, Divider } from '@mui/material';
import MenuIcon from '@mui/icons-material/Menu';
import LogoutIcon from '@mui/icons-material/Logout';
import VideocamIcon from '@mui/icons-material/Videocam';
// import SettingsIcon from '@mui/icons-material/Settings'; // Removed unused import
import DeleteForeverIcon from '@mui/icons-material/DeleteForever';
import { useAuth } from '@/hooks/useAuth';
import PeerList from './PeerList';
import StreamView from './StreamView';
import { usePeers } from '@/contexts/PeersContext';
import StatusIndicator from './StatusIndicator';


function Layout() {
    const { logout } = useAuth();
    const { selectedPeer, setSelectedPeer } = usePeers();
    const [mobileOpen, setMobileOpen] = useState(false);
    const navigate = useNavigate();
    const location = useLocation(); // Get current location

    const handleDrawerToggle = () => {
        setMobileOpen(!mobileOpen);
    };

    const handleNavigation = (path: string) => {
        navigate(path);
        if (mobileOpen) {
            setMobileOpen(false);
        }
    };

    const handlePeerSelected = (peerId: string | null) => {
        setSelectedPeer(peerId);
        if (peerId === null) {
            handleNavigation('/');
        }
        if (mobileOpen) {
            setMobileOpen(false);
        }
    };


    const drawerWidth = 240;

    const drawer = (
        <Box sx={{ textAlign: 'center' }}>
            <Typography variant="h6" sx={{ my: 2 }}>
                Eillance Viewer
            </Typography>
            <Divider />
            <List>
                <ListItem disablePadding>
                    <ListItemButton onClick={() => handlePeerSelected(null)}>
                        <ListItemIcon><VideocamIcon /></ListItemIcon>
                        <ListItemText primary="Available Devices" />
                    </ListItemButton>
                </ListItem>
                {/* Settings link removed for now to fix unused import error */}
                <Divider />
                <ListItem disablePadding>
                    <ListItemButton onClick={() => handleNavigation('/account/delete')}>
                        <ListItemIcon><DeleteForeverIcon color="error" /></ListItemIcon>
                        <ListItemText primary="Delete Account" />
                    </ListItemButton>
                </ListItem>
                <Divider />
                <ListItem disablePadding>
                    <ListItemButton onClick={async () => {
                        if (mobileOpen) setMobileOpen(false);
                        await logout();
                    }}>
                        <ListItemIcon><LogoutIcon /></ListItemIcon>
                        <ListItemText primary="Logout" />
                    </ListItemButton>
                </ListItem>
            </List>
        </Box>
    );

    return (
        <Box sx={{ display: 'flex' }}>
            <AppBar
                position="fixed"
                sx={{
                    zIndex: (theme) => theme.zIndex.drawer + 1,
                }}
            >
                <Toolbar>
                    <IconButton
                        color="inherit"
                        aria-label="open drawer"
                        edge="start"
                        onClick={handleDrawerToggle}
                        sx={{ mr: 2 }}
                    >
                        <MenuIcon />
                    </IconButton>
                    <Typography variant="h6" noWrap component="div" sx={{ flexGrow: 1 }}>
                        {selectedPeer ? selectedPeer.name :
                            location.pathname.includes('/account/delete') ? 'Account Deletion' : 'Available Devices'}
                    </Typography>
                    <StatusIndicator />
                    <IconButton color="inherit" onClick={logout} aria-label="logout">
                        <LogoutIcon />
                    </IconButton>
                </Toolbar>
            </AppBar>
            <Drawer
                variant="temporary"
                open={mobileOpen}
                onClose={handleDrawerToggle}
                ModalProps={{
                    keepMounted: true,
                }}
                sx={{
                    '& .MuiDrawer-paper': { boxSizing: 'border-box', width: drawerWidth },
                }}
            >
                {drawer}
            </Drawer>

            <Box
                component="main"
                sx={{
                    flexGrow: 1,
                    p: 0,
                    width: '100%',
                    mt: ['56px', '64px'],
                    height: 'calc(100vh - 56px)',
                    '@media (min-width:600px)': {
                        height: 'calc(100vh - 64px)',
                    },
                    overflow: 'hidden',
                    display: 'flex',
                    flexDirection: 'column',
                }}
            >
                {selectedPeer ? (
                    <StreamView peer={selectedPeer} onBack={() => handlePeerSelected(null)} />
                ) : (
                    location.pathname === '/' || !location.pathname.startsWith('/account/') ? (
                        <Container maxWidth="md" sx={{py: 3, flexGrow: 1}}>
                            <PeerList onPeerSelected={(peer) => handlePeerSelected(peer.id)}/>
                        </Container>
                    ) : null
                )}
            </Box>
        </Box>
    );
}

export default Layout;