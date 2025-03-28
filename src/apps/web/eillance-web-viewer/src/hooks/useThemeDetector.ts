import { useState, useEffect } from 'react';

const useThemeDetector = (): boolean => {
    const getCurrentTheme = () => window.matchMedia("(prefers-color-scheme: dark)").matches;
    const [isDarkTheme, setIsDarkTheme] = useState(getCurrentTheme());

    useEffect(() => {
        const mqListener = ((e: MediaQueryListEvent) => {
            setIsDarkTheme(e.matches);
        });
        const darkThemeMq = window.matchMedia("(prefers-color-scheme: dark)");

        // Add listener safely for modern browsers
        try {
            darkThemeMq.addEventListener('change', mqListener);
        } catch (e1) {
            // Fallback for older browsers
            try {
                darkThemeMq.addListener(mqListener);
            } catch (e2) {
                console.error('Error adding theme listener:', e2);
            }
        }


        // Cleanup listener on unmount
        return () => {
            try {
                darkThemeMq.removeEventListener('change', mqListener);
            } catch (e1) {
                try {
                    darkThemeMq.removeListener(mqListener);
                } catch (e2) {
                    console.error('Error removing theme listener:', e2);
                }
            }
        };
    }, []);

    return isDarkTheme;
}

export default useThemeDetector;