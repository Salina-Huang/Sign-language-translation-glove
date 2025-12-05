import 'package:flutter/material.dart';
import 'package:flutter_blue_plus/flutter_blue_plus.dart';
import 'dart:async';

void main() {
  runApp(const MyApp());
}

class MyApp extends StatelessWidget {
  const MyApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Sign Language Glove Bluetooth',
      theme: ThemeData(
        primarySwatch: Colors.blue,
      ),
      home: const BluetoothGlovePage(),
    );
  }
}

class BluetoothGlovePage extends StatefulWidget {
  const BluetoothGlovePage({super.key});

  @override
  State<BluetoothGlovePage> createState() => _BluetoothGlovePageState();
}

class _BluetoothGlovePageState extends State<BluetoothGlovePage> {
  // Bluetooth related variables
  StreamSubscription<BluetoothAdapterState>? _adapterStateSubscription;
  StreamSubscription<List<ScanResult>>? _scanResultsSubscription;
  BluetoothDevice? _connectedDevice;
  BluetoothCharacteristic? _dataCharacteristic;
  StreamSubscription<List<int>>? _dataSubscription;
  List<ScanResult> _scanResults = [];
  
  // Application state variables
  String _currentGesture = "Waiting for glove connection...";
  String _sensorData = "No data";
  String _connectionStatus = "Not connected";
  List<String> _gestureHistory = [];
  bool _isScanning = false;
  bool _isConnected = false;

  // ESP32 Bluetooth UUIDs
  final String gloveServiceUUID = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";
  final String dataCharacteristicUUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8";

  @override
  void initState() {
    super.initState();
    _setupBluetooth();
  }

  @override
  void dispose() {
    // Clean up all subscriptions
    _adapterStateSubscription?.cancel();
    _scanResultsSubscription?.cancel();
    _dataSubscription?.cancel();
    _disconnectFromDevice();
    super.dispose();
  }

  void _setupBluetooth() {
    // Listen to Bluetooth state (using static getter)
    _adapterStateSubscription = FlutterBluePlus.adapterState.listen((state) {
      setState(() {
        _connectionStatus = "Bluetooth Status: ${_getStateText(state)}";
      });
    });
  }

  void _startScan() {
    setState(() {
      _isScanning = true;
      _connectionStatus = "Scanning...";
      _scanResults.clear(); // Clear previous scan results
    });

    // Start scanning (using static method)
    FlutterBluePlus.startScan(
      timeout: const Duration(seconds: 10),
    );

    // Listen to scan results (using static getter)
    _scanResultsSubscription = FlutterBluePlus.scanResults.listen((results) {
      setState(() {
        _scanResults = results.where((result) => 
          result.device.advName.isNotEmpty &&
          (result.device.advName.contains("ESP32") || 
           result.device.advName.contains("Glove") ||
           result.device.remoteId.toString().isNotEmpty)
        ).toList();
      });
    });

    // Stop scanning after 10 seconds
    Timer(const Duration(seconds: 10), () async {
      await FlutterBluePlus.stopScan();
      setState(() {
        _isScanning = false;
        _connectionStatus = _scanResults.isEmpty ? "No devices found" : "Please select a device to connect";
      });
      if (mounted) {
        setState(() {
          _isScanning = false;
        });
        // Stop scanning (using static method)
        FlutterBluePlus.stopScan();
      }
    });
  }

  void _connectToDevice(BluetoothDevice device) async {
    try {
      setState(() {
        _connectionStatus = "Connecting...";
      });

      // Listen to device connection state
      var connectionStateSubscription = device.connectionState.listen((BluetoothConnectionState state) async {
        if (state == BluetoothConnectionState.disconnected) {
          print("Disconnection reason: ${device.disconnectReason?.code} ${device.disconnectReason?.description}");
          setState(() {
            _connectionStatus = "Disconnected";
            _isConnected = false;
          });
        }
      });

      // Set up auto-cancellation when disconnected
      device.cancelWhenDisconnected(connectionStateSubscription, delayed: true, next: true);

      // Connect to device
      await device.connect(
        license: License.free, // Use free license
        autoConnect: false,
        timeout: const Duration(seconds: 35),
      );
      _connectedDevice = device;

      // Discover services
      List<BluetoothService> services = await device.discoverServices();

      // Find glove service
      for (BluetoothService service in services) {
        if (service.serviceUuid.toString().toLowerCase() == gloveServiceUUID) {
          for (BluetoothCharacteristic characteristic in service.characteristics) {
            if (characteristic.characteristicUuid.toString().toLowerCase() == dataCharacteristicUUID) {
              _dataCharacteristic = characteristic;
              
              // Enable notifications
              await characteristic.setNotifyValue(true);
              
              // Listen to data
              _dataSubscription = characteristic.onValueReceived.listen((data) {
                _processReceivedData(String.fromCharCodes(data));
              });

              setState(() {
                _isConnected = true;
                _connectionStatus = "Connected: ${device.platformName}";
                _currentGesture = "Ready to receive data...";
              });
              
              return;
            }
          }
        }
      }

      setState(() {
        _connectionStatus = "Glove service not found";
      });

    } catch (e) {
      setState(() {
        _connectionStatus = "Connection failed: $e";
        _isConnected = false;
      });
    }
  }

  void _processReceivedData(String data) {
    print("Received data: $data");
    
    setState(() {
      _sensorData = data;
      
      if (data.contains("Gesture[")) {
        final start = data.indexOf("Gesture[") + 8;
        final end = data.indexOf("]", start);
        final gesture = data.substring(start, end);
        
        _currentGesture = gesture;
        
        final timestamp = DateTime.now().toString().substring(11, 19);
        _gestureHistory.insert(0, "$timestamp - $gesture");
        
        if (_gestureHistory.length > 10) {
          _gestureHistory.removeLast();
        }
      } else if (data.contains("Flex[")) {
        _currentGesture = "Sensor Data";
      }
    });
  }

  void _disconnectFromDevice() async {
    _dataSubscription?.cancel();
    _dataSubscription = null;
    
    if (_connectedDevice != null) {
      await _connectedDevice!.disconnect();
    }
    
    setState(() {
      _connectedDevice = null;
      _dataCharacteristic = null;
      _isConnected = false;
      _isScanning = false;
      _connectionStatus = "Disconnected";
      _currentGesture = "Waiting for glove connection...";
    });
  }

  void _clearHistory() {
    setState(() {
      _gestureHistory.clear();
    });
  }

  String _getStateText(BluetoothAdapterState state) {
    switch (state) {
      case BluetoothAdapterState.on:
        return "On";
      case BluetoothAdapterState.off:
        return "Off";
      case BluetoothAdapterState.unknown:
        return "Unknown";
      case BluetoothAdapterState.unauthorized:
        return "Unauthorized";
      default:
        return "Unknown State";
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Sign Language Glove Bluetooth'),
        backgroundColor: Colors.blue[700],
        foregroundColor: Colors.white,
        actions: [
          IconButton(
            icon: const Icon(Icons.delete),
            onPressed: _clearHistory,
            tooltip: 'Clear History',
          ),
        ],
      ),
      
      body: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Bluetooth device list
            if (_scanResults.isNotEmpty && !_isConnected)
              Card(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Padding(
                      padding: const EdgeInsets.all(16.0),
                      child: Row(
                        mainAxisAlignment: MainAxisAlignment.spaceBetween,
                        children: [
                          const Text(
                            'Available Devices',
                            style: TextStyle(
                              fontSize: 18,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                          Text('${_scanResults.length} devices'),
                        ],
                      ),
                    ),
                    const Divider(height: 1),
                    Container(
                      constraints: const BoxConstraints(maxHeight: 200),
                      child: ListView.builder(
                        shrinkWrap: true,
                        itemCount: _scanResults.length,
                        itemBuilder: (context, index) {
                          final result = _scanResults[index];
                          final device = result.device;
                          final name = device.advName.isNotEmpty
                              ? device.advName
                              : "Unknown Device ${device.remoteId}";
                          return ListTile(
                            title: Text(name),
                            subtitle: Text(device.remoteId.toString()),
                            trailing: Column(
                              mainAxisAlignment: MainAxisAlignment.center,
                              children: [
                                Text("${result.rssi} dBm"),
                                const SizedBox(height: 4),
                                Icon(
                                  Icons.signal_cellular_alt,
                                  color: result.rssi > -60
                                      ? Colors.green
                                      : result.rssi > -80
                                          ? Colors.orange
                                          : Colors.red,
                                ),
                              ],
                            ),
                            onTap: () => _connectToDevice(device),
                          );
                        },
                      ),
                    ),
                  ],
                ),
              ),

            // Connection status
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16.0),
                child: Column(
                  children: [
                    Row(
                      children: [
                        Icon(
                          _isConnected ? Icons.bluetooth_connected : Icons.bluetooth,
                          color: _isConnected ? Colors.green : Colors.grey,
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            _connectionStatus,
                            style: TextStyle(
                              color: _isConnected ? Colors.green : Colors.grey,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 10),
                    if (!_isConnected && !_isScanning)
                      ElevatedButton(
                        onPressed: _startScan,
                        child: const Text('Scan for Glove Devices'),
                      ),
                    if (_isScanning)
                      const Row(
                        children: [
                          CircularProgressIndicator(),
                          SizedBox(width: 10),
                          Text('Scanning...'),
                        ],
                      ),
                    if (_isConnected)
                      ElevatedButton(
                        onPressed: _disconnectFromDevice,
                        style: ElevatedButton.styleFrom(
                          backgroundColor: Colors.red,
                          foregroundColor: Colors.white,
                        ),
                        child: const Text('Disconnect'),
                      ),
                  ],
                ),
              ),
            ),
            
            const SizedBox(height: 20),
            
            // Current gesture display
            Card(
              elevation: 4,
              child: Padding(
                padding: const EdgeInsets.all(20.0),
                child: Column(
                  children: [
                    const Text(
                      'Recognized Gesture',
                      style: TextStyle(
                        fontSize: 16,
                        color: Colors.grey,
                      ),
                    ),
                    const SizedBox(height: 15),
                    Text(
                      _currentGesture,
                      style: const TextStyle(
                        fontSize: 32,
                        fontWeight: FontWeight.bold,
                        color: Colors.blue,
                      ),
                      textAlign: TextAlign.center,
                    ),
                    const SizedBox(height: 15),
                    Text(
                      _sensorData,
                      style: const TextStyle(
                        fontSize: 14,
                        color: Colors.grey,
                      ),
                      textAlign: TextAlign.center,
                    ),
                  ],
                ),
              ),
            ),
            
            const SizedBox(height: 20),
            
            // Recognition history
            const Text(
              'Recognition History',
              style: TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 10),
            
            Expanded(
              child: _gestureHistory.isEmpty
                  ? const Center(
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Icon(Icons.history, size: 50, color: Colors.grey),
                          SizedBox(height: 10),
                          Text(
                            'No recognition records',
                            style: TextStyle(color: Colors.grey),
                          ),
                        ],
                      ),
                    )
                  : ListView.builder(
                      itemCount: _gestureHistory.length,
                      itemBuilder: (context, index) {
                        return Card(
                          margin: const EdgeInsets.symmetric(vertical: 4),
                          child: ListTile(
                            leading: const Icon(Icons.gesture, color: Colors.blue),
                            title: Text(_gestureHistory[index]),
                          ),
                        );
                      },
                    ),
            ),
          ],
        ),
      ),
    );
  }
}