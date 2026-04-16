# FOME CAN Bridge

The FOME CAN Bridge is a lightweight, high-performance proxy application designed to bridge Controller Area Network (CAN) traffic from hardware (ECUs) over to a desktop tuning software like TunerStudio.

## Building the Bridge
The bridge avoids legacy bloat and compiles rapidly using Gradle. 

### Prerequisites
- Java JDK 11 or higher
- Bash environment (Linux, macOS, or Git Bash for Windows)

### Compilation
To compile the bridge simply execute the provided build script located in the repository root:
```bash
./build-bridge.sh
```
This automatically compiles the project and generates a standalone FAT JAR with all dependencies bundled here: `current_bridge_output/fome-can-bridge.jar`.

Alternatively, if you'd like to build the project directly using gradle:
```bash
cd fome-can-bridge
./gradlew shadowJar
```

## Running the Bridge

Once compiled, you can launch the application by running the executable JAR:
```bash
java -jar current_bridge_output/fome-can-bridge.jar
```
This will launch the graphical interface where you can specify your ECU parameters and select the connected adapter.

### Linux (SocketCAN)
The application natively supports SocketCAN (`can0`, `can1`, `vcan0`, etc) on Linux.
Alternatively, there is a convenient helper script provided that runs the JAR and takes CLI arguments directly to bypass the UI:
```bash
./fome_bridge.sh can0 802 809
```
*(Usage: `./fome_bridge.sh [device_name] [rx_id] [tx_id]`)*

### Windows (COM Ports / SLCAN)
On Windows, you can connect using a standard COM port (e.g. `COM1`, `COM3`, etc) mapping directly to a serial CAN bridge using the SLCAN protocol (such as CANable flashed with slcan firmware). Select your COM port within the graphical interface, and click "Start Server" to host the TunerStudio networking socket on port 29001.

## TunerStudio Integration
Once the console indicates that the TCP port is listening on `29001`, open TunerStudio:
1. Open your ECU's Project.
2. Go to `Communications -> Settings`.
3. Set the communication type to `TCP/IP`.
4. Port "29001", IP Address "localhost"
5. Run "Test port" to verify communication.
