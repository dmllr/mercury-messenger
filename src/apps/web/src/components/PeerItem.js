// src/components/PeerItem.js
import React from 'react';

const PeerItem = ({ peer, onPeerClicked }) => {
    const handleClick = () => {
        onPeerClicked(peer);
    };

    return (
        <div className="peer-item" onClick={handleClick}>
            <div className="peer-content">
                <div className="peer-name">{peer.name}</div>
                <div className="peer-status">
                    <span className={`status-indicator ${peer.online ? 'online' : 'offline'}`}></span>
                    {peer.online ? 'Online' : 'Offline'}
                </div>
            </div>
        </div>
    );
};

export default PeerItem;