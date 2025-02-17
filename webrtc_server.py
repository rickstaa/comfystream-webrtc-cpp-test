import asyncio
import json
import logging
from aiohttp import web
from aiortc import RTCPeerConnection, RTCSessionDescription, MediaStreamTrack, RTCRtpSender
from aiortc.codecs import h264

logger = logging.getLogger(__name__)
logging.basicConfig(level=logging.INFO)

MAX_BITRATE = 2000000
MIN_BITRATE = 2000000

import logging

logging.getLogger("aiortc").setLevel(logging.DEBUG)
logging.getLogger("aiortc.stun").setLevel(logging.DEBUG)
logging.getLogger("aiortc.rtcdtlstransport").setLevel(logging.DEBUG)
logging.getLogger("aiortc.rtcrtpsender").setLevel(logging.DEBUG)
logging.getLogger("aiortc.rtcrtpreceiver").setLevel(logging.DEBUG)


class VideoStreamTrack(MediaStreamTrack):
    kind = "video"

    def __init__(self, track: MediaStreamTrack):
        super().__init__()
        self.track = track

    async def recv(self):
        frame = await self.track.recv()
        return frame

def force_codec(pc, sender, forced_codec):
    kind = forced_codec.split("/")[0]
    codecs = RTCRtpSender.getCapabilities(kind).codecs
    transceiver = next(t for t in pc.getTransceivers() if t.sender == sender)
    codecPrefs = [codec for codec in codecs if codec.mimeType == forced_codec]
    transceiver.setCodecPreferences(codecPrefs)

async def offer(request):
    params = await request.json()
    offer_params = params["offer"]
    offer = RTCSessionDescription(sdp=offer_params["sdp"], type=offer_params["type"])

    pc = RTCPeerConnection()
    tracks = {"video": None}

    # Prefer h264
    transceiver = pc.addTransceiver("video")
    caps = RTCRtpSender.getCapabilities("video")
    prefs = list(filter(lambda x: x.name == "H264", caps.codecs))
    transceiver.setCodecPreferences(prefs)

    # Monkey patch max and min bitrate to ensure constant bitrate
    h264.MAX_BITRATE = MAX_BITRATE
    h264.MIN_BITRATE = MIN_BITRATE

    @pc.on("track")
    def on_track(track):
        logger.info(f"Track received: {track.kind}")
        if track.kind == "video":
            videoTrack = VideoStreamTrack(track)
            tracks["video"] = videoTrack
            sender = pc.addTrack(videoTrack)
            force_codec(pc, sender, "video/H264")

        @track.on("ended")
        async def on_ended():
            logger.info(f"{track.kind} track ended")

    await pc.setRemoteDescription(offer)
    answer = await pc.createAnswer()
    await pc.setLocalDescription(answer)

    logger.info(f"SDP Answer: {pc.localDescription.sdp}")

    return web.Response(
        content_type="application/json",
        text=json.dumps({"sdp": pc.localDescription.sdp, "type": pc.localDescription.type}),
        headers={
            "Access-Control-Allow-Origin": "*",
            "Access-Control-Allow-Methods": "POST, GET, OPTIONS",
            "Access-Control-Allow-Headers": "Content-Type"
        }
    )

async def handle_options(request):
    return web.Response(
        status=204,
        headers={
            "Access-Control-Allow-Origin": "*",
            "Access-Control-Allow-Methods": "POST, GET, OPTIONS",
            "Access-Control-Allow-Headers": "Content-Type"
        }
    )

async def on_shutdown(app):
    coros = [pc.close() for pc in app["pcs"]]
    await asyncio.gather(*coros)
    app["pcs"].clear()

def health(_):
    return web.Response(content_type="application/json", text="OK")

if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)

    app = web.Application()
    app["pcs"] = set()

    app.router.add_post("/offer", offer)
    app.router.add_options("/offer", handle_options)
    app.router.add_get("/", health)
    
    app.on_shutdown.append(on_shutdown)

    web.run_app(app, host="127.0.0.1", port=8888)
