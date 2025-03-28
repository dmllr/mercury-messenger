import { useState } from 'react';
import { AppBar, Toolbar, Typography, IconButton, Box, Container, Drawer, List, ListItem, ListItemButton, ListItemText, ListItemIcon, Divider } from '@mui/material';
import MenuIcon from '@mui/icons-material/Menu';
import LogoutIcon from '@mui/icons-material/Logout';
import VideocamIcon from '@mui/icons-material/Videocam';
import SettingsIcon from '@mui/icons-material/Settings'; // Example icon
import { useAuth } from '@/hooks/useAuth';
import PeerList from './PeerList';
import StreamView from './StreamView';
import { usePeers } from '@/contexts/PeersContext';
import StatusIndicator from './StatusIndicator';


function Layout() {
    const { logout } = useAuth();
    const { selectedPeer, setSelectedPeer } = usePeers();
    const [mobileOpen, setMobileOpen] = useState(false);

    const handleDrawerToggle = () => {
        setMobileOpen(!mobileOpen);
    };

    const handlePeerSelected = (peerId: string | null) => {
        setSelectedPeer(peerId);
        // Optionally close drawer on selection if open
        if (mobileOpen) {
            setMobileOpen(false);
        }
    };

    const drawerWidth = 240;

    const drawer = (
        <Box onClick={handleDrawerToggle} sx={{ textAlign: 'center' }}>
            <Typography variant="h6" sx={{ my: 2 }}>
                Eillance Viewer
            </Typography>
            <Divider />
            <List>
                {/* Navigation Items */}
                <ListItem disablePadding>
                    <ListItemButton onClick={() => handlePeerSelected(null)}> {/* Go back to Peer List */}
                        <ListItemIcon><VideocamIcon /></ListItemIcon>
                        <ListItemText primary="Available Devices" />
                    </ListItemButton>
                </ListItem>
                {/* Add other navigation items like settings here if needed */}
                <ListItem disablePadding>
                    <ListItemButton>
                        <ListItemIcon><SettingsIcon /></ListItemIcon>
                        <ListItemText primary="Settings (TBD)" />
                    </ListItemButton>
                </ListItem>
                <Divider />
                {/* Logout */}
                <ListItem disablePadding>
                    <ListItemButton onClick={logout}>
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
                    zIndex: (theme) => theme.zIndex.drawer + 1, // Ensure AppBar is above drawer
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
                        {selectedPeer ? selectedPeer.name : 'Available Devices'}
                    </Typography>
                    <StatusIndicator /> {/* Add Status Indicator */}
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
                    keepMounted: true, // Better open performance on mobile.
                }}
                sx={{
                    '& .MuiDrawer-paper': { boxSizing: 'border-box', width: drawerWidth },
                }}
            >
                {drawer}
            </Drawer>

            {/* Main Content Area */}
            <Box
                component="main"
                sx={{
                    flexGrow: 1,
                    p: 0, // Remove padding for fullscreen video
                    width: '100%', // Ensure main content takes full width
                    mt: ['56px', '64px'], // Adjust margin top for AppBar height (mobile, desktop)
                    height: 'calc(100vh - 56px)', // Full height minus AppBar
                    '@media (min-width:600px)': {
                        height: 'calc(100vh - 64px)',
                    },
                    overflow: 'hidden', // Prevent scrollbars if video fits exactly
                }}
            >
                {/* No Toolbar necessary here if content fills whole area */}
                {selectedPeer ? (
                    // Render StreamView taking full space
                    <StreamView peer={selectedPeer} onBack={() => handlePeerSelected(null)} />
                ) : (
                    // Render PeerList with padding
                    <Container maxWidth="md" sx={{py: 3}}>
                        <PeerList onPeerSelected={(peer) => handlePeerSelected(peer.id)}/>
                    </Container>
                )}
            </Box>
        </Box>
    );
}

export default Layout;