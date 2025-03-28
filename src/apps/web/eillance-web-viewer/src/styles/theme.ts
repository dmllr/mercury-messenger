import { createTheme } from '@mui/material/styles';
import { deepPurple, amber, grey } from '@mui/material/colors';

// Light Theme
export const lightTheme = createTheme({
    palette: {
        mode: 'light',
        primary: {
            main: deepPurple[500], // Example primary color
        },
        secondary: {
            main: amber[700], // Example secondary color
        },
        background: {
            default: grey[100], // Light background
            paper: '#ffffff', // Paper background
        },
    },
    // You can customize components globally here
    // components: { ... }
});

// Dark Theme
export const darkTheme = createTheme({
    palette: {
        mode: 'dark',
        primary: {
            main: deepPurple[300], // Lighter primary for dark mode
        },
        secondary: {
            main: amber[500], // Lighter secondary for dark mode
        },
        background: {
            default: grey[900], // Dark background
            paper: grey[800], // Darker paper background
        },
        text: {
            primary: '#ffffff', // Ensure primary text is white
            secondary: grey[400], // Slightly dimmer secondary text
        }
    },
    // Customize components for dark mode if needed
    // components: { ... }
});