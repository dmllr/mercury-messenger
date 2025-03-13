// src/App.js
import React, { useState, useEffect } from 'react';
import { BrowserRouter as Router, Route, Switch, Redirect } from 'react-router-dom';
import firebase from 'firebase/app';
import 'firebase/auth';
import { FirebaseAuthProvider, FirebaseAuthConsumer } from '@react-firebase/auth';
import StyledFirebaseAuth from 'react-firebaseui/StyledFirebaseAuth';

import PeersScreen from './components/PeersScreen';
import StreamScreen from './components/StreamScreen';
import { getAvailablePeers, updatePeerStatus } from './services/firebaseService';
import './styles.css';

// Firebase UI config
const uiConfig = {
    signInFlow: 'popup',
    signInOptions: [
        firebase.auth.GoogleAuthProvider.PROVIDER_ID,
        firebase.auth.EmailAuthProvider.PROVIDER_ID
    ],
    callbacks: {
        signInSuccessWithAuthResult: () => false
    }
};

function App() {
    const [peers, setPeers] = useState([]);
    const [selectedPeer, setSelectedPeer] = useState(null);
    const [isFullscreen, setIsFullscreen] = useState(false);

    useEffect(() => {
        const fetchPeers = async () => {
            if (firebase.auth().currentUser) {
                const peersList = await getAvailablePeers();
                setPeers(peersList);
            }
        };

        fetchPeers();
        const interval = setInterval(fetchPeers, 10000); // Refresh peers every 10 seconds

        return () => clearInterval(interval);
    }, []);

// src/App.js (continued)
    useEffect(() => {
        const handleFullscreenChange = () => {
            setIsFullscreen(!!document.fullscreenElement);
        };

        document.addEventListener('fullscreenchange', handleFullscreenChange);
        return () => {
            document.removeEventListener('fullscreenchange', handleFullscreenChange);
        };
    }, []);

    const handlePeerClick = (peer) => {
        setSelectedPeer(peer);
    };

    const handleSetFullscreen = (fullscreen) => {
        if (fullscreen) {
            if (document.documentElement.requestFullscreen) {
                document.documentElement.requestFullscreen();
            }
        } else {
            if (document.exitFullscreen) {
                document.exitFullscreen();
            }
        }
    };

    return (
        <FirebaseAuthProvider firebase={firebase}>
            <Router>
                <div className="app">
                    <FirebaseAuthConsumer>
                        {({ isSignedIn, user }) => {
                            if (!isSignedIn) {
                                return (
                                    <div className="auth-container">
                                        <h1>Eillance Surveillance</h1>
                                        <StyledFirebaseAuth uiConfig={uiConfig} firebaseAuth={firebase.auth()} />
                                    </div>
                                );
                            }

                            return (
                                <div className={`app-content ${isFullscreen ? 'fullscreen' : ''}`}>
                                    {!isFullscreen && (
                                        <header className="app-header">
                                            <h1>
                                                {selectedPeer ? selectedPeer.name : 'Available Devices'}
                                            </h1>
                                            {selectedPeer && (
                                                <button className="back-button" onClick={() => setSelectedPeer(null)}>
                                                    Back
                                                </button>
                                            )}
                                        </header>
                                    )}

                                    <main>
                                        <Switch>
                                            <Route exact path="/">
                                                {selectedPeer ? (
                                                    <Redirect to={`/stream/${selectedPeer.id}`} />
                                                ) : (
                                                    <PeersScreen peers={peers} onPeerClicked={handlePeerClick} />
                                                )}
                                            </Route>
                                            <Route path="/stream/:peerId" render={(props) => {
                                                const peerId = props.match.params.peerId;
                                                const peer = peers.find(p => p.id === peerId) || selectedPeer;
                                                if (!peer) return <Redirect to="/" />;

                                                return (
                                                    <StreamScreen
                                                        peer={peer}
                                                        isFullscreen={isFullscreen}
                                                        setFullscreen={handleSetFullscreen}
                                                        onBack={() => {
                                                            setSelectedPeer(null);
                                                            props.history.push('/');
                                                        }}
                                                    />
                                                );
                                            }} />
                                        </Switch>
                                    </main>
                                </div>
                            );
                        }}
                    </FirebaseAuthConsumer>
                </div>
            </Router>
        </FirebaseAuthProvider>
    );
}

export default App;
