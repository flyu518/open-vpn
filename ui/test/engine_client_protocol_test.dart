import 'dart:convert';
import 'dart:io';

import 'package:flutter_test/flutter_test.dart';
import 'package:openvpn_ui/models/session.dart';
import 'package:openvpn_ui/services/engine_client.dart';

void main() {
  test('Unix socket 客户端能区分交错到达的事件和响应', () async {
    final socketPath = '/tmp/openvpn-ui-client-$pid.sock';
    final server = await ServerSocket.bind(
      InternetAddress(socketPath, type: InternetAddressType.unix),
      0,
    );
    final connectionFuture = server.first;
    final client = UnixSocketEngineClient(socketPath: socketPath);
    final eventFuture = client.events.first;
    final responseFuture = client.snapshot();
    final connection = await connectionFuture;
    final input = StringBuffer();
    final subscription = connection.listen((bytes) {
      input.write(utf8.decode(bytes));
      while (input.toString().contains('\n')) {
        final value = input.toString();
        final newline = value.indexOf('\n');
        final line = value.substring(0, newline);
        input
          ..clear()
          ..write(value.substring(newline + 1));
        final request = jsonDecode(line) as Map<String, dynamic>;
        if (request['command'] == 'snapshot') {
          connection.write('${jsonEncode(<String, Object?>{
                'type': 'sessionEvent',
                'sessionId': 'office-a',
                'message': '已连接',
                'session': <String, Object?>{
                  'sessionId': 'office-a',
                  'name': '研发网络',
                  'state': 'connected',
                  'remoteHost': 'vpn.example.com',
                  'warnings': <String>['兼容模式：跳过 1 条冲突路由'],
                },
              })}\n');
          connection.write('${jsonEncode(<String, Object?>{
                'ok': true,
                'sessions': <Object?>[],
              })}\n');
        }
      }
    });

    final response = await responseFuture;
    final event = await eventFuture;

    expect(response.ok, isTrue);
    expect(event.sessionId, 'office-a');
    expect(event.status, VpnStatus.connected);
    expect(event.session?.warnings, contains('兼容模式：跳过 1 条冲突路由'));

    await subscription.cancel();
    await connection.close();
    await client.dispose();
    await server.close();
    final socketFile = File(socketPath);
    if (await socketFile.exists()) await socketFile.delete();
  });

  test('不会把 OpenVPN3 内部 tun 调试日志展示为卡片警告', () {
    final session = VpnSession.fromJson(<String, dynamic>{
      'sessionId': 'office-a',
      'name': '研发网络',
      'state': 'connected',
      'warnings': <String>[
        'SetupClient: transmitting tun setup list to /var/run/openvpn.sock {"config":{}}',
        '兼容模式：跳过 1 条冲突路由',
      ],
    });

    expect(session.warnings, <String>['兼容模式：跳过 1 条冲突路由']);
  });

  test('能解析 daemon 返回的连接时长和流量统计', () {
    final session = VpnSession.fromJson(<String, dynamic>{
      'sessionId': 'office-a',
      'name': '研发网络',
      'state': 'connected',
      'downloadBytes': 1024,
      'uploadBytes': 2048,
      'durationSeconds': 42,
    });

    expect(session.downloadBytes, 1024);
    expect(session.uploadBytes, 2048);
    expect(session.duration, const Duration(seconds: 42));
  });
}
