# ComfyStream WebRTC C++ Client  

This repository contains my test code for connecting an C++ WebRTC client to an python WebRTC server.

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

5. **Compile the C++ client** by installing the required build tools and using CMake:  

   ```bash
   sudo apt install build-essential
   cmake .
   cmake --build .
   ```

6. **Run the C++ client:**  

   ```bash
   ./webrtc-client
   ```
  
7. **Observe that C++ client communication is broken:** Despite data being sent, the `recv` method of `MediaStreamTrack` is never triggered.
