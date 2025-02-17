const signalingServerUrl = 'http://127.0.0.1:8888/offer';
const localVideo = document.getElementById('localVideo');
const remoteVideo = document.getElementById('remoteVideo');

async function startWebRTC() {
    const pc = new RTCPeerConnection({
        iceServers: [{ urls: 'stun:stun.l.google.com:19302' }]
    });

    pc.onicecandidate = event => {
        if (event.candidate) {
            console.log('[Local Candidate]', event.candidate);
        }
    };

    pc.ontrack = event => {
        console.log('[Track]', event.track);
        remoteVideo.srcObject = event.streams[0];
    };

    // Get local camera stream
    const localStream = await navigator.mediaDevices.getUserMedia({ video: true });
    localVideo.srcObject = localStream;
    localStream.getTracks().forEach(track => pc.addTrack(track, localStream));

    const offer = await pc.createOffer();
    await pc.setLocalDescription(offer);

    const response = await fetch(signalingServerUrl, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
            offer: {
                sdp: pc.localDescription.sdp,
                type: pc.localDescription.type
            },
            prompt: {
                "1": { "inputs": { "image": "sampled_frame.jpg" } },
                "2": { "inputs": { "images": ["12", 0] } }
            }
        })
    });

    const answer = await response.json();
    await pc.setRemoteDescription(new RTCSessionDescription(answer));
    console.log('[Remote Description]', answer);
}

startWebRTC().catch(console.error);
