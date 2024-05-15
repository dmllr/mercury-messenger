/** @type {RTCPeerConnection} */
let rtc;
let token = null; // Store the token here after authentication

const iceConnectionLog = document.getElementById('ice-connection-state'),
    iceGatheringLog = document.getElementById('ice-gathering-state'),
    signalingLog = document.getElementById('signaling-state'),
    dataChannelLog = document.getElementById('data-channel');

function randomString(len) {
    const charSet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    let randomString = '';
    for (let i = 0; i < len; i++) {
        const randomPoz = Math.floor(Math.random() * charSet.length);
        randomString += charSet.substring(randomPoz, randomPoz + 1);
    }
    return randomString;
}

const receiveID = randomString(10);
// const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
// const websocket = new WebSocket(protocol + '//app.hela.me:8000/');
const websocket = new WebSocket('ws://localhost:8000/');
websocket.onopen = function () {
    authenticateUser();
}

// data channel
let dc = null, dcTimeout = null;

function authenticateUser() {
    // Send authentication message to the server
    websocket.send(JSON.stringify({
        type: 'authenticate',
        username: 'user1', // replace with actual username
        password: 'password1' // replace with actual password
    }));
}

function createPeerConnection() {
    const config = {
        sdpSemantics: 'unified-plan',
        bundlePolicy: "max-bundle",
    };

    config.iceServers = [
        {
            urls: "stun:stun.relay.metered.ca:80",
        },
        {
            urls: "turn:a.relay.metered.ca:80",
            username: "49880a0f046c8990b1990b35",
            credential: "qZCNW/57EB8GwO3A",
        },
        {
            urls: "turn:a.relay.metered.ca:80?transport=tcp",
            username: "49880a0f046c8990b1990b35",
            credential: "qZCNW/57EB8GwO3A",
        },
        {
            urls: "turn:a.relay.metered.ca:443",
            username: "49880a0f046c8990b1990b35",
            credential: "qZCNW/57EB8GwO3A",
        },
        {
            urls: "turn:a.relay.metered.ca:443?transport=tcp",
            username: "49880a0f046c8990b1990b35",
            credential: "qZCNW/57EB8GwO3A",
        },
    ];

    let pc = new RTCPeerConnection(config);

    // register some listeners to help debugging
    pc.addEventListener('icegatheringstatechange', function () {
        iceGatheringLog.textContent += ' -> ' + pc.iceGatheringState;
    }, false);
    iceGatheringLog.textContent = pc.iceGatheringState;

    pc.addEventListener('iceconnectionstatechange', function () {
        iceConnectionLog.textContent += ' -> ' + pc.iceConnectionState;
    }, false);
    iceConnectionLog.textContent = pc.iceConnectionState;

    pc.addEventListener('signalingstatechange', function () {
        signalingLog.textContent += ' -> ' + pc.signalingState;
    }, false);
    signalingLog.textContent = pc.signalingState;

    // pc.onicecandidateerror = function(event) {
    //     if (event.errorCode >= 300 && event.errorCode <= 699) {
    //         signalingLog.textContent += ' -> error[300-699]';
    //         // Here this a standardized ICE error
    //         // Do something...
    //     } else if (event.errorCode >= 700 && event.errorCode <= 799) {
    //         signalingLog.textContent += ' -> error[700-799]';
    //         // Here, the application perhaps didn't reach the server ?
    //         // Do something else...
    //     }
    // }

    // connect audio / video
    pc.addEventListener('track', function (evt) {
        console.log(evt);
        document.getElementById('media').style.display = 'block';
        const videoTag = document.getElementById('video');
        videoTag.srcObject = evt.streams[0];
        videoTag.load();
        videoTag.play();
    });

    let time_start = null;

    function current_stamp() {
        if (time_start === null) {
            time_start = new Date().getTime();
            return 0;
        } else {
            return new Date().getTime() - time_start;
        }
    }

    pc.ondatachannel = function (event) {
        dc = event.channel;
        dc.onopen = function () {
            dataChannelLog.textContent += '- open\n';
            dataChannelLog.scrollTop = dataChannelLog.scrollHeight;
        };
        dc.onmessage = function (evt) {

            dataChannelLog.textContent += '< ' + evt.data + '\n';
            dataChannelLog.scrollTop = dataChannelLog.scrollHeight;

            dcTimeout = setTimeout(function () {
                if (dc == null && dcTimeout != null) {
                    dcTimeout = null;
                    return
                }
                const message = 'Pong ' + current_stamp();
                dataChannelLog.textContent += '> ' + message + '\n';
                dataChannelLog.scrollTop = dataChannelLog.scrollHeight;
                dc.send(message);
            }, 1000);
        }
        dc.onclose = function () {
            clearTimeout(dcTimeout);
            dcTimeout = null;
            dataChannelLog.textContent += '- close\n';
            dataChannelLog.scrollTop = dataChannelLog.scrollHeight;
        };
    }

    return pc;
}

function sendAnswer(pc) {
    return pc.createAnswer()
        .then((answer) => rtc.setLocalDescription(answer))
        .then(function () {
            // wait for ICE gathering to complete
            return new Promise(function (resolve) {
                if (pc.iceGatheringState === 'complete') {
                    resolve();
                } else {
                    function checkState() {
                        if (pc.iceGatheringState === 'complete') {
                            pc.removeEventListener('icegatheringstatechange', checkState);
                            resolve();
                        }
                    }

                    pc.addEventListener('icegatheringstatechange', checkState);
                }
            });
        }).then(function () {
            const answer = pc.localDescription;

            document.getElementById('answer-sdp').textContent = answer.sdp;

            return websocket.send(JSON.stringify(
                {
                    id: "server",
                    token: token,
                    type: answer.type,
                    sdp: answer.sdp
                }));
        }).catch(function (e) {
            alert(e);
        });
}

function handleOffer(offer) {
    rtc = createPeerConnection();
    return rtc.setRemoteDescription(offer)
        .then(() => sendAnswer(rtc));
}

function sendStreamRequest() {
    websocket.send(JSON.stringify(
        {
            id: "server",
            token: token,
            type: "streamRequest"
        }));
}

async function start() {
    document.getElementById('start').style.display = 'none';
    document.getElementById('stop').style.display = 'inline-block';
    document.getElementById('media').style.display = 'block';
    sendStreamRequest();
}

function stop() {
    document.getElementById('stop').style.display = 'none';
    document.getElementById('media').style.display = 'none';
    document.getElementById('start').style.display = 'inline-block';

    // close data channel
    if (dc) {
        dc.close();
        dc = null;
    }

    // close transceivers
    if (rtc !== undefined && rtc.getTransceivers) {
        rtc.getTransceivers().forEach(function (transceiver) {
            if (transceiver.stop) {
                transceiver.stop();
            }
        });
    }

    // close local audio / video
    rtc.getSenders().forEach(function (sender) {
        const track = sender.track;
        if (track !== null) {
            sender.track.stop();
        }
    });

    // close peer connection
    setTimeout(function () {
        rtc.close();
        rtc = null;
    }, 500);
}

websocket.onmessage = async function (evt) {
    const received_msg = evt.data;
    const object = JSON.parse(received_msg);

    if (object.type === 'authenticated') {
        // Store the token
        token = object.token;
        document.getElementById('start').disabled = false;

        // Publish the client after authentication
        websocket.send(JSON.stringify({
            type: 'publish_client',
            token: token,
            client_id: receiveID
        }));
    } else if (object.type === "offer") {
        document.getElementById('offer-sdp').textContent = object.sdp;
        await handleOffer(object)
    } else if (object.type === "client_published") {
        console.log('Client published:', object.client_id);
    } else if (object.type === "error") {
        console.error('Error:', object.error);
    }
}
