import 'package:flutter/material.dart';

class GloveHomePage extends StatefulWidget {
  const GloveHomePage({super.key});

  @override
  State<GloveHomePage> createState() => _GloveHomePageState();
}

class _GloveHomePageState extends State<GloveHomePage> {
  String _currentGesture = "等待识别...";
  String _receivedData = "";
  List<String> _gestureHistory = [];
  
  // 模拟从手套接收的数据
  final List<Map<String, dynamic>> _sampleGestures = [
    {'name': '你好', 'data': '1,1,2,2,2', 'direction': '1,2,2'},
    {'name': '我', 'data': '1,2,1,1,1', 'direction': '2,1,1'},
    {'name': 'D', 'data': '1,1,1,1,1', 'direction': '1,2,2'},
    {'name': 'E', 'data': '1,0,2,2,2', 'direction': '0,2,1'},
    {'name': 'OK', 'data': '1,1,2,2,2', 'direction': '1,2,2'},
    {'name': '谢谢', 'data': '1,0,1,1,1', 'direction': '2,0,1'},
  ];

  void _simulateGestureRecognition() {
    // 随机选择一个手势模拟识别
    final randomGesture = _sampleGestures[DateTime.now().millisecond % _sampleGestures.length];
    
    setState(() {
      _currentGesture = randomGesture['name'];
      _receivedData = "手指: ${randomGesture['data']} | 方向: ${randomGesture['direction']}";
      _gestureHistory.insert(0, "${DateTime.now().toString().substring(11, 19)} - ${randomGesture['name']}");
      
      // 只保留最近10条记录
      if (_gestureHistory.length > 10) {
        _gestureHistory.removeLast();
      }
    });
  }

  void _clearHistory() {
    setState(() {
      _gestureHistory.clear();
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('手语手套识别系统'),
        backgroundColor: Colors.blue[700],
        foregroundColor: Colors.white,
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: _clearHistory,
            tooltip: '清空历史',
          ),
        ],
      ),
      body: Padding(
        padding: const EdgeInsets.all(16.0),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // 当前手势显示
            Card(
              elevation: 4,
              child: Padding(
                padding: const EdgeInsets.all(20.0),
                child: Column(
                  children: [
                    const Text(
                      '当前识别手势',
                      style: TextStyle(
                        fontSize: 16,
                        color: Colors.grey,
                      ),
                    ),
                    const SizedBox(height: 8),
                    Text(
                      _currentGesture,
                      style: const TextStyle(
                        fontSize: 36,
                        fontWeight: FontWeight.bold,
                        color: Colors.blue,
                      ),
                    ),
                    const SizedBox(height: 16),
                    Text(
                      _receivedData,
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
            
            // 模拟识别按钮
            SizedBox(
              width: double.infinity,
              child: ElevatedButton.icon(
                onPressed: _simulateGestureRecognition,
                icon: const Icon(Icons.gesture),
                label: const Text('模拟手势识别'),
                style: ElevatedButton.styleFrom(
                  padding: const EdgeInsets.symmetric(vertical: 16),
                  backgroundColor: Colors.green,
                  foregroundColor: Colors.white,
                ),
              ),
            ),
            
            const SizedBox(height: 20),
            
            // 手势历史
            const Text(
              '识别历史',
              style: TextStyle(
                fontSize: 18,
                fontWeight: FontWeight.bold,
              ),
            ),
            const SizedBox(height: 10),
            Expanded(
              child: _gestureHistory.isEmpty
                  ? const Center(
                      child: Text(
                        '暂无识别记录',
                        style: TextStyle(color: Colors.grey),
                      ),
                    )
                  : ListView.builder(
                      itemCount: _gestureHistory.length,
                      itemBuilder: (context, index) {
                        return Card(
                          margin: const EdgeInsets.symmetric(vertical: 4),
                          child: ListTile(
                            leading: const Icon(Icons.history, color: Colors.blue),
                            title: Text(_gestureHistory[index]),
                            trailing: const Icon(Icons.chevron_right),
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