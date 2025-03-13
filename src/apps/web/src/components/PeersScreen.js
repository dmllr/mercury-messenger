// src/components/PeersScreen.js
import React from 'react';
import PeerItem from './PeerItem';

const PeersScreen = ({ peers, onPeerClicked }) => {
    if (peers.length === 0) {
        return (
            <div className="empty-state">
                <p>No registered devices</p>
            </div>
        );
    }

    return (
        <div className="peers-container">
            {peers.map(peer => (
                <PeerItem
                    key={peer.id}
                    peer={peer}
                    onPeerClicked={onPeerClicked}
                />
            ))}
        </div>
    );
};

export default PeersScreen;
