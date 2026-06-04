#!/usr/bin/env python3
"""
UDP 测试客户端 —— servoV6 UDP 通讯层的交互式测试工具
========================================================
类似 Postman，用于手工测试 UDP 命令。

用法：
    python tests/udp_client_test.py [--host 127.0.0.1] [--port 9001]

运行后进入交互模式，输入 JSON 命令发送并查看回复。
输入内置快捷命令（如 'pos'）可自动填充协议字段。

内置快捷命令：
  pos            → 查询 R 轴当前相对位置  (cmd=2)
  speed          → 查询 R 轴当前移动速度   (cmd=4)
  setzero        → 设置 R 轴相对零点       (cmd=5)
  move <target>  → 绝对位置移动 (cmd=0)   例: move 100
  rel <offset>   → 相对偏移移动 (cmd=1)   例: rel -50
  setspeed <v>   → 设置移动速度 (cmd=3)   例: setspeed 25
  raw <json>     → 直接发送原始 JSON
  group <name>   → 切换默认分组          例: group Machine_B
  help           → 显示帮助
  quit           → 退出
========================================================
"""

import socket
import json
import sys
import argparse
import time


def send_udp(sock: socket.socket, host: str, port: int, payload: dict, timeout: float = 5.0) -> dict | None:
    """发送 JSON 命令并接收回复"""
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    print(f"\n>>> SEND ({len(data)} bytes): {data.decode()}")
    try:
        sock.sendto(data, (host, port))
    except OSError as e:
        print(f"!!! SEND FAILED: {e}")
        return None

    sock.settimeout(timeout)
    try:
        resp_data, addr = sock.recvfrom(65535)
        reply = json.loads(resp_data.decode("utf-8"))
        elapsed_ms = 0  # approximate
        print(f"<<< REPLY from {addr[0]}:{addr[1]} ({len(resp_data)} bytes):")
        print(f"    {json.dumps(reply, indent=4, ensure_ascii=False)}")

        if reply.get("result") == 1:
            print(f"    ✅ SUCCESS")
        else:
            msg = reply.get("msg", "(no message)")
            print(f"    ❌ FAILED: {msg}")
        return reply
    except socket.timeout:
        print(f"!!! TIMEOUT: no reply within {timeout}s")
        return None
    except json.JSONDecodeError:
        print(f"!!! Invalid JSON reply: {resp_data[:200]}")
        return None
    except OSError as e:
        print(f"!!! RECV FAILED: {e}")
        return None


def main():
    parser = argparse.ArgumentParser(description="servoV6 UDP Test Client")
    parser.add_argument("--host", default="127.0.0.1", help="Target host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9001, help="Target port (default: 9001)")
    parser.add_argument("--group", default="Machine_A", help="Default group name (default: Machine_A)")
    parser.add_argument("--timeout", type=float, default=10.0, help="Reply timeout in seconds (default: 10)")
    args = parser.parse_args()

    current_group = args.group

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(1.0)  # default short timeout for first attempt

    print("=" * 60)
    print(f"  servoV6 UDP Test Client")
    print(f"  Target: {args.host}:{args.port}")
    print(f"  Group:  {current_group}")
    print(f"  Timeout: {args.timeout}s")
    print("=" * 60)
    print("Type 'help' for commands, 'quit' to exit.\n")

    try:
        while True:
            try:
                line = input("udp> ").strip()
            except (EOFError, KeyboardInterrupt):
                print("\nBye.")
                break

            if not line:
                continue

            # ──── quit ────
            if line.lower() in ("quit", "exit", "q"):
                print("Bye.")
                break

            # ──── help ────
            if line.lower() == "help":
                print("""
Quick Commands:
  pos                 → GET_REL_POSITION  (cmd=2)
  speed               → GET_MOVE_SPEED    (cmd=4)
  setzero             → SET_REL_ZERO      (cmd=5)
  move <target_mm>    → MOVE_TO_REL_TARGET (cmd=0)   e.g. move 500
  rel <offset_mm>     → MOVE_OFFSET       (cmd=1)   e.g. rel -50
  setspeed <rpm>      → SET_MOVE_SPEED    (cmd=3)   e.g. setspeed 30
  raw {"cmd":2,...}   → send raw JSON directly
  group <group_name>  → switch default group
  help                → show this help

Error Test Commands (验证分发器错误处理矩阵):
  test:badjson        → send malformed text    (expect "invalid JSON format")
  test:nocmd          → missing 'cmd' field     (expect "missing required field 'cmd'")
  test:nomotor        → missing 'motor' field   (expect "missing required field 'motor'")
  test:nogroup        → missing 'group' field   (expect "missing required field 'group'")
  test:badmotor       → motor=0 (non-R axis)    (expect "motor 0 not supported...")
  test:badgroup       → group="NoSuchGroup"      (expect "group 'NoSuchGroup' not found")
  test:unknowncmd     → cmd=99                  (expect "unknown cmd: 99")
  test:notarget       → cmd=0 without 'target'   (expect "missing required field 'target'")
  test:nooffset       → cmd=1 without 'offset'   (expect "missing required field 'offset'")
  test:nospeed        → cmd=3 without 'speed'    (expect "missing required field 'speed'")

Usage examples:
  >>> move 500
  >>> rel -50
  >>> pos
  >>> setspeed 30
""")
                continue

            # ──── group <name> ────
            if line.lower().startswith("group "):
                parts = line.split(maxsplit=1)
                if len(parts) >= 2:
                    current_group = parts[1].strip()
                    print(f"Switched to group: {current_group}")
                continue

            # ──── move <target> : cmd=0 ────
            if line.lower().startswith("move "):
                parts = line.split(maxsplit=1)
                try:
                    target = float(parts[1])
                except (ValueError, IndexError):
                    print("Usage: move <target_mm>  e.g. move 500")
                    continue
                payload = {"cmd": 0, "group": current_group, "motor": 2, "target": target}
                send_udp(sock, args.host, args.port, payload, args.timeout)
                continue

            # ──── rel <offset> : cmd=1 ────
            if line.lower().startswith("rel "):
                parts = line.split(maxsplit=1)
                try:
                    offset = float(parts[1])
                except (ValueError, IndexError):
                    print("Usage: rel <offset_mm>  e.g. rel -50")
                    continue
                payload = {"cmd": 1, "group": current_group, "motor": 2, "offset": offset}
                send_udp(sock, args.host, args.port, payload, args.timeout)
                continue

            # ──── setspeed <speed> : cmd=3 ────
            if line.lower().startswith("setspeed "):
                parts = line.split(maxsplit=1)
                try:
                    speed = float(parts[1])
                except (ValueError, IndexError):
                    print("Usage: setspeed <rpm>  e.g. setspeed 30")
                    continue
                payload = {"cmd": 3, "group": current_group, "motor": 2, "speed": speed}
                send_udp(sock, args.host, args.port, payload, args.timeout)
                continue

            # ──── pos : cmd=2 ────
            if line.lower() == "pos":
                payload = {"cmd": 2, "group": current_group, "motor": 2}
                send_udp(sock, args.host, args.port, payload, args.timeout)
                continue

            # ──── speed : cmd=4 ────
            if line.lower() == "speed":
                payload = {"cmd": 4, "group": current_group, "motor": 2}
                send_udp(sock, args.host, args.port, payload, args.timeout)
                continue

            # ──── setzero : cmd=5 ────
            if line.lower() == "setzero":
                payload = {"cmd": 5, "group": current_group, "motor": 2}
                send_udp(sock, args.host, args.port, payload, args.timeout)
                continue

            # ──── error test shortcuts ────
            if line.lower() == "test:badjson":
                sock.sendto(b"this is not json", (args.host, args.port))
                try:
                    resp, _ = sock.recvfrom(65535)
                    print(f"<<< {resp.decode()}")
                except socket.timeout:
                    print("!!! no reply")
                continue

            test_cases = {
                "test:nocmd":      {"motor": 2, "group": current_group},
                "test:nomotor":    {"cmd": 0, "group": current_group},
                "test:nogroup":    {"cmd": 0, "motor": 2},
                "test:badmotor":   {"cmd": 0, "motor": 0, "group": current_group},
                "test:badgroup":   {"cmd": 0, "motor": 2, "group": "NoSuchGroup"},
                "test:unknowncmd": {"cmd": 99, "motor": 2, "group": current_group},
                "test:notarget":   {"cmd": 0, "motor": 2, "group": current_group},
                "test:nooffset":   {"cmd": 1, "motor": 2, "group": current_group},
                "test:nospeed":    {"cmd": 3, "motor": 2, "group": current_group},
            }
            if line.lower() in test_cases:
                send_udp(sock, args.host, args.port, test_cases[line.lower()], args.timeout)
                continue

            # ──── raw JSON ────
            if line.lower().startswith("raw "):
                json_str = line[4:].strip()
                try:
                    payload = json.loads(json_str)
                except json.JSONDecodeError as e:
                    print(f"Invalid JSON: {e}")
                    continue
                send_udp(sock, args.host, args.port, payload, args.timeout)
                continue

            # ──── unknown ────
            print(f"Unknown command: '{line}'. Type 'help' for available commands.")

    finally:
        sock.close()


if __name__ == "__main__":
    main()