import App from './App.tsx';
import ReactDOM from 'react-dom/client';
import { BrowserRouter } from 'react-router-dom';
import { AuthProvider } from './contexts/AuthContext.tsx';
import { PeersProvider } from './contexts/PeersContext.tsx';
import { SignalingProvider } from './contexts/SignalingContext.tsx';
import './styles/global.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
    <AuthProvider>
        <PeersProvider>
            <SignalingProvider>
                <BrowserRouter basename="/apps/viewer">
                    <App />
                </BrowserRouter>
            </SignalingProvider>
        </PeersProvider>
    </AuthProvider>
);