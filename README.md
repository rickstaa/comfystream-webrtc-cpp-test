# ComfyStream WebRTC C++ Client  

This repository contains my test code for connecting an C++ WebRTC client to an python WebRTC server.


## Prerequisites

- Python 3.11
- Conda
- CMake
- Build Essentials (for Linux)
- `nlohmann_json`
- [LibDataChannel](https://github.com/paullouisageneau/libdatachannel)
- `OpenCV`
- `FFmpeg`

## Testing Instructions  

1. **Set up a Python environment and install dependencies:**  

   ```bash
   conda create -n webrtc_test python=3.11
   conda activate webrtc_test
   pip install -r requirements.txt
   ```

2. **Start the Python WebRTC server:**  

   ```bash
   python3 webrtc_server.py
   ```

3. **Verify WebRTC functionality** by opening `index.html` in a browser and ensuring the JavaScript example runs without issues.  

4. **Close the browser and restart the WebRTC server:**  

   ```bash
   python3 webrtc_server.py
   ```

5. **Install the Libdatachannels library:** Follow the instructions from the [LibDataChannel GitHub repository](https://github.com/paullouisageneau/libdatachannel) to install LibDataChannel.

6. **Install C++ Dependencies:**

    ```bash
    sudo apt-get install nlohmann-json3-dev
    sudo apt-get install libopencv-dev
    sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
    sudo apt install build-essential
    ```

7. **Compile the C++ client** by installing the required build tools and using CMake:  

   ```bash
   cmake .
   cmake --build .
   ```

8. **Run the C++ client:**  

   ```bash
   ./webrtc-client
   ```
  
9.  **Observe that C++ client communication is broken:** Despite data being sent, the `recv` method of `MediaStreamTrack` is never triggered.
