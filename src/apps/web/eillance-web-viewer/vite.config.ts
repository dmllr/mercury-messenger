import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import path from 'path';

// https://vitejs.dev/config/
export default defineConfig({
    appType: 'spa',
    plugins: [react()],
    resolve: {
        alias: {
            '@': path.resolve(__dirname, './src'),
        },
    },
    server: {
        port: 3001,
        // host: true
    },
    build: {
        rollupOptions: {
            input: {
                // Specify the entry point HTML file relative to the project root
                main: path.resolve(__dirname, 'index.html')
            }
        },
    }
})
