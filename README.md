# WebRTC Client

This project demonstrates a simple WebRTC client that interacts with a WebRTC server for testing purposes. It includes functionalities for establishing a peer connection, sending offers, and managing ICE candidates.

## Project Structure

```
webrtc-client
├── src
│   ├── main.cpp            # Entry point of the application
│   ├── comfystream_client.cpp # Implementation of the ComfyStreamClient class
│   └── comfystream_client.h   # Declaration of the ComfyStreamClient class
├── CMakeLists.txt         # CMake configuration file
└── README.md               # Project documentation
```

## Dependencies

- C++17 or later
- WebRTC library
- JSON library (e.g., jsoncpp)
- HTTP library (e.g., httplib)

## Building the Project

1. Clone the repository:
   ```
   git clone <repository-url>
   cd webrtc-client
   ```

2. Create a build directory:
   ```
   mkdir build
   cd build
   ```

3. Run CMake to configure the project:
   ```
   cmake ..
   ```

4. Build the project:
   ```
   make
   ```

## Running the Application

After building the project, you can run the application using the following command:

```
./webrtc-client
```

Make sure to replace `<repository-url>` with the actual URL of the repository. Adjust any paths as necessary based on your environment and setup.# comfystream-webrtc-cpp-test
