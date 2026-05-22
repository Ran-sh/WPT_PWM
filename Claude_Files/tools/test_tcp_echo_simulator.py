#!/usr/bin/env python3
"""
TCP 遥测模拟器 — 模拟 STM32+ESP8266 行为，用于 LabVIEW 上位机测试

功能:
  - TCP Server 模式: 监听 8080 端口 (模拟 NetAssist)
  - TCP Client 模式: 连接指定服务器 (模拟 ESP8266 连入)
  - 定时发送 JSON 遥测: {"V":48.20,"I":2.15,"F":120000}
  - 接收并响应 ON/OFF 指令

用法:
  python test_tcp_echo_simulator.py                  # Server 模式，手动输入
  python test_tcp_echo_simulator.py --auto           # Server 模式，自动生成随机数据
  python test_tcp_echo_simulator.py --client 192.168.1.100  # Client 模式
"""

import argparse
import json
import math
import random
import socket
import sys
import time
from datetime import datetime


class TelemetrySimulator:
    """模拟 STM32 遥测数据生成"""

    def __init__(self, auto_vary: bool = True):
        self.auto_vary = auto_vary
        self.time_s = 0.0

        # 基线值
        self.base_v = 48.0  # 母线电压 (V)
        self.base_i = 2.0   # 母线电流 (A)
        self.base_f = 120000  # PWM 频率 (Hz)
        self.pwm_on = True

    def generate(self) -> dict:
        """生成一次遥测数据"""
        self.time_s += 1.0

        if not self.pwm_on:
            return {"V": round(random.uniform(1, 3), 2),
                    "I": 0.0,
                    "F": 0,
                    "S": "OFF"}

        if self.auto_vary:
            # 模拟负载波动 ±5%
            v = self.base_v * random.uniform(0.95, 1.05)
            i = self.base_i * random.uniform(0.90, 1.10)
            f = int(self.base_f * random.uniform(0.98, 1.02))
        else:
            v = self.base_v
            i = self.base_i
            f = self.base_f

        return {"V": round(v, 2), "I": round(i, 2), "F": f, "S": "ON"}

    def set_pwm(self, on: bool):
        self.pwm_on = on
        if on:
            print("[模拟] PWM 已开启")
        else:
            print("[模拟] PWM 已关闭")


def format_json(data: dict) -> str:
    """格式化为与 STM32 固件一致的 JSON 字符串"""
    # 只发送 V, I, F (与 App_Net.c 格式一致)
    return json.dumps({"V": data["V"], "I": data["I"], "F": data["F"]}) + "\r\n"


def run_server(auto_vary: bool, port: int = 8080):
    """TCP Server 模式 — 模拟 NetAssist"""
    sim = TelemetrySimulator(auto_vary=auto_vary)
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("0.0.0.0", port))
    server.listen(1)

    print(f"=" * 60)
    print(f"  TCP 遥测模拟器 (Server 模式)")
    print(f"  监听端口: {port}")
    print(f"  数据变化: {'自动 ±5%' if auto_vary else '固定值'}")
    print(f"  等待 ESP8266/LabVIEW 连接...")
    print(f"=" * 60)

    data_count = 0
    client = None

    try:
        while True:
            if client is None:
                print(f"[{datetime.now().strftime('%H:%M:%S')}] 等待连接...")
                client, addr = server.accept()
                print(f"[{datetime.now().strftime('%H:%M:%S')}] 客户端已连接: {addr[0]}:{addr[1]}")
                client.settimeout(0.5)
                data_count = 0
                sim = TelemetrySimulator(auto_vary=auto_vary)

            try:
                # 接收指令
                try:
                    rx_data = client.recv(1024)
                    if rx_data:
                        rx_str = rx_data.decode("utf-8", errors="replace").strip()
                        print(f"\n[接收] {rx_str}")
                        if "ON" in rx_str.upper():
                            sim.set_pwm(True)
                        if "OFF" in rx_str.upper():
                            sim.set_pwm(False)
                    elif rx_data == b"":
                        # 连接断开
                        print(f"[{datetime.now().strftime('%H:%M:%S')}] 客户端断开")
                        client.close()
                        client = None
                        continue
                except socket.timeout:
                    pass  # 无数据，正常

                # 发送遥测 (每秒)
                data = sim.generate()
                json_str = format_json(data)
                client.sendall(json_str.encode("utf-8"))
                data_count += 1

                status = f"{'✓' if sim.pwm_on else '✗'}"
                print(f"  [{data_count:4d}] {status} V={data['V']:6.2f}V  I={data['I']:5.2f}A  F={data['F']:6d}Hz", end="\r")

                time.sleep(1.0)

            except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
                print(f"\n[{datetime.now().strftime('%H:%M:%S')}] 连接异常断开")
                if client:
                    client.close()
                client = None
                time.sleep(1)

    except KeyboardInterrupt:
        print(f"\n\n[{datetime.now().strftime('%H:%M:%S')}] 用户中断，共发送 {data_count} 条数据")
    finally:
        if client:
            client.close()
        server.close()
        print("服务器已关闭")


def run_client(server_ip: str, port: int = 8080):
    """TCP Client 模式 — 模拟 ESP8266 连接 LabVIEW"""
    sim = TelemetrySimulator(auto_vary=True)

    print(f"=" * 60)
    print(f"  TCP 遥测模拟器 (Client 模式 — 模拟 ESP8266)")
    print(f"  目标: {server_ip}:{port}")
    print(f"=" * 60)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    try:
        print(f"连接中...")
        sock.connect((server_ip, port))
        print(f"已连接到 {server_ip}:{port}")
        sock.settimeout(0.5)

        data_count = 0
        while True:
            try:
                # 接收指令
                try:
                    rx_data = sock.recv(1024)
                    if rx_data:
                        rx_str = rx_data.decode("utf-8", errors="replace").strip()
                        print(f"\n[接收指令] {rx_str}")
                        if "ON" in rx_str.upper():
                            sim.set_pwm(True)
                        if "OFF" in rx_str.upper():
                            sim.set_pwm(False)
                except socket.timeout:
                    pass

                # 发送遥测
                data = sim.generate()
                json_str = format_json(data)
                sock.sendall(json_str.encode("utf-8"))
                data_count += 1
                print(f"  [{data_count:4d}] V={data['V']:6.2f}V  I={data['I']:5.2f}A  F={data['F']:6d}Hz", end="\r")
                time.sleep(1.0)

            except (ConnectionResetError, BrokenPipeError):
                print(f"\n连接断开")
                break

    except KeyboardInterrupt:
        print(f"\n\n用户中断，共发送 {data_count} 条数据")
    except Exception as e:
        print(f"\n错误: {e}")
    finally:
        sock.close()
        print("已断开")


def main():
    parser = argparse.ArgumentParser(
        description="TCP 遥测模拟器 — 模拟 STM32+ESP8266 行为",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python test_tcp_echo_simulator.py                    # Server 监听 8080
  python test_tcp_echo_simulator.py --auto             # Server + 自动变化数据
  python test_tcp_echo_simulator.py --port 9999        # Server 自定义端口
  python test_tcp_echo_simulator.py --client 192.168.31.254  # Client 连 LabVIEW
        """
    )
    parser.add_argument("--port", type=int, default=8080, help="TCP 端口 (默认 8080)")
    parser.add_argument("--auto", action="store_true", help="自动变化数据 (默认固定值)")
    parser.add_argument("--client", type=str, metavar="IP", help="Client 模式: 连接指定 IP")
    args = parser.parse_args()

    try:
        if args.client:
            run_client(args.client, args.port)
        else:
            run_server(auto_vary=args.auto, port=args.port)
    except OSError as e:
        if "10048" in str(e) or "Address already in use" in str(e):
            print(f"错误: 端口 {args.port} 已被占用。请先关闭 NetAssist 或其他程序。")
        else:
            print(f"网络错误: {e}")
        sys.exit(1)


if __name__ == "__main__":
    main()
