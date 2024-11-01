/** @type {RTCPeerConnection} */
let rtc;
let token = null; // Store the token here after authentication
let selectedClientId = null; // Store the selected client ID here
let selectedResolution = null;
let profile = null;

const iceConnectionLog = document.getElementById('ice-connection-state'),
    iceGatheringLog = document.getElementById('ice-gathering-state'),
    signalingLog = document.getElementById('signaling-state'),
    dataChannelLog = document.getElementById('data-channel');

const cameraSelect = document.getElementById('camera-select');
const resolutionSelect = document.getElementById('resolution-select');

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
const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
const websocket = new WebSocket(protocol + '//app.hela.me:8000');
// const websocket = new WebSocket('ws://localhost:8000/');
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
            urls: "turn:a.relay.metered.ca:443",
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
            sendConfiguration();
            dataChannelLog.textContent += '- open\n';
            dataChannelLog.scrollTop = dataChannelLog.scrollHeight;
        };
        dc.onmessage = function (evt) {
            dataChannelLog.textContent += '< ' + evt.data + '\n';
            dataChannelLog.scrollTop = dataChannelLog.scrollHeight;

            if (evt.data === "Ping") {
                dcTimeout = setTimeout(function () {
                    if (dc == null && dcTimeout != null) {
                        dcTimeout = null;
                        return
                    }
                    const reply = 'Pong ' + current_stamp();
                    dataChannelLog.textContent += '> ' + reply + '\n';
                    dataChannelLog.scrollTop = dataChannelLog.scrollHeight;
                    dc.send(reply);
                }, 1000);
            } else {
                const message = JSON.parse(evt.data);
                if (message.type === "info profile") {
                    if (profile == null) {
                        // overwrite the profile if only it wasn't set during the publication
                        profile = message.profile;
                        populateCameraAndResolutionSelectors();
                    }
                }
            }
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
                    to: selectedClientId,
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
            to: selectedClientId,
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

function refreshPublishers() {
    websocket.send(JSON.stringify({
        type: 'get publishers',
        token: token
    }));
}

function updateClientList(publishers) {
    const clientListDiv = document.getElementById('client-list');
    clientListDiv.innerHTML = '';
    publishers.forEach((publisher, index) => {
        const client_id = publisher.id;
        const client_name = publisher.name;
        const local_profile = publisher.profile;
        const radioBtn = document.createElement('input');
        radioBtn.type = 'radio';
        radioBtn.name = 'client';
        radioBtn.value = client_id;
        radioBtn.id = 'client_' + client_id;
        radioBtn.onclick = function() {
            selectedClientId = this.value;
            profile = local_profile;
            populateCameraAndResolutionSelectors();
        };
        if (index === 0) {
            radioBtn.checked = true;
            profile = local_profile;
            populateCameraAndResolutionSelectors();
        }

        const label = document.createElement('label');
        label.htmlFor = 'client_' + client_id;
        label.textContent = client_name;

        clientListDiv.appendChild(radioBtn);
        clientListDiv.appendChild(label);
        clientListDiv.appendChild(document.createElement('br'));
    });
    selectedClientId = publishers.length > 0 ? publishers[0].id : null;
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
            type: 'publish',
            token: token,
            client_id: receiveID,
            kind: "consumer",
        }));

        // Refresh client list after authentication
        refreshPublishers();
    } else if (object.type === "offer") {
        document.getElementById('offer-sdp').textContent = object.sdp;
        await handleOffer(object);
    } else if (object.type === "error") {
        console.error('Error: ', object.error, ', message: ', object.message);
    } else if (object.type === "info publishers") {
        updateClientList(object.clients);
    } else {
        console.error('Error: Unknown message type "', object.type, '" received');
    }
}

function populateCameraAndResolutionSelectors() {
    cameraSelect.innerHTML = '';
    resolutionSelect.innerHTML = '';

    if (profile == null) {
        return;
    }

    profile.forEach(camera => {
        const option = document.createElement('option');
        option.value = camera.id;
        option.textContent = camera.name;
        cameraSelect.appendChild(option);
    });

    if (profile.length > 0) {
        populateResolutions(profile[0].resolutions);
    }
}

function populateResolutions(resolutions) {
    resolutionSelect.innerHTML = '';

    // If there is a previously selected resolution, find the closest one
    if (selectedResolution) {
        resolutions.sort((a, b) => {
            // Priority 1: width difference
            const widthDiffA = Math.abs(a.width - selectedResolution.width);
            const widthDiffB = Math.abs(b.width - selectedResolution.width);

            if (widthDiffA !== widthDiffB) return widthDiffA - widthDiffB;

            // Priority 2: total pixel difference (width * height)
            const pixelsA = a.width * a.height;
            const pixelsB = b.width * b.height;
            const pixelDiffA = Math.abs(pixelsA - (selectedResolution.width * selectedResolution.height));
            const pixelDiffB = Math.abs(pixelsB - (selectedResolution.width * selectedResolution.height));

            return pixelDiffA - pixelDiffB;
        });
    }

    // Populate the resolutions
    resolutions.forEach((resolution, index) => {
        const option = document.createElement('option');
        option.value = JSON.stringify(resolution);
        option.textContent = `${resolution.width}x${resolution.height}`;

        // Select the first option or the closest match
        if (index === 0 || (selectedResolution && resolution.width === resolutions[0].width && resolution.height === resolutions[0].height)) {
            option.selected = true;
        }

        resolutionSelect.appendChild(option);
    });

    // Update the selectedResolution with the newly selected option
    selectedResolution = resolutions[0];
}

function onCameraChange() {
    // Save the currently selected resolution
    if (resolutionSelect.value) {
        selectedResolution = JSON.parse(resolutionSelect.value);
    }

    const selectedCameraId = cameraSelect.value;
    const selectedCamera = profile.find(camera => camera.id === selectedCameraId);

    // Populate resolutions for the new camera
    populateResolutions(selectedCamera.resolutions);

    // Send updated configuration
    sendConfiguration();
}

function onResolutionChange() {
    sendConfiguration();
}

function sendConfiguration() {
    const cameraId = cameraSelect.value;
    const resolution = JSON.parse(resolutionSelect.value);

    const configurationMessage = {
        type: 'configuration',
        camera_id: cameraId,
        resolution: resolution
    };

    if (!dc) {
        return;
    }

    const message = JSON.stringify(configurationMessage);
    dataChannelLog.textContent += '> ' + message + '\n';
    dataChannelLog.scrollTop = dataChannelLog.scrollHeight;
    dc.send(message);
}

function onRotationChange() {
    const rotationSelect = document.getElementById('rotation-select');
    const videoElement = document.getElementById('video');
    const videoContainer = document.getElementById('video-container');
    const selectedRotation = parseInt(rotationSelect.value);

    // Apply rotation using CSS transform
    videoElement.style.transform = `rotate(${selectedRotation}deg)`;

    // Adjust container padding based on rotation
    if (selectedRotation === 90 || selectedRotation === 270) {
        videoContainer.style.padding = '20% 0'; // Adjusts padding for vertical orientation
    } else {
        videoContainer.style.padding = '0'; // No extra padding needed for horizontal orientation
    }
}
