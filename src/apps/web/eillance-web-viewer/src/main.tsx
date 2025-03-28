import ReactDOM from 'react-dom/client';
import App from './App.tsx';
import { AuthProvider } from './contexts/AuthContext.tsx';
import { PeersProvider } from './contexts/PeersContext.tsx';
import { SignalingProvider } from './contexts/SignalingContext.tsx';
import './styles/global.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
    // <React.StrictMode> // <-- COMMENT OUT OR REMOVE THIS LINE
    <AuthProvider>
        <PeersProvider>
            <SignalingProvider>
                <App />
            </SignalingProvider>
        </PeersProvider>
    </AuthProvider>
    // </React.StrictMode> // <-- COMMENT OUT OR REMOVE THIS LINE
);