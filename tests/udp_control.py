#!/usr/bin/env python3
"""
UDP 控制脚本 —— servoV6 UDP 通讯层（Phase 5 异步语义）测试工具
==============================================================
与 tests/udp_client_test.py（旧同步语义、不支持 operationId）不同，
本脚本感知当前 UdpCommandDispatcher 的「异步提交流程」：

  运动/写入命令 (cmd=0/1/3/5) -> 立即回 Queued + operationId
  状态查询        (cmd=6 QUERY_OPERATION) -> 轮询直至终态

默认端口 62000（与 main.cpp 中 kEnableUdpVnext 分支实际监听一致）。

用法：
  交互模式：
      python tests/udp_control.py [--host 127.0.0.1] [--port 62000] [--group Machine_A]

  自动化回归（冒烟）：
      python tests/udp_control.py --auto [--host 127.0.0.1] [--port 62000]
      成功退出码 0；任一断言失败退出码 1。

交互内置命令：
  move <target_mm>   -> MOVE_TO_REL_TARGET  (cmd=0)  例: move 500
  rel <offset_mm>    -> MOVE_OFFSET         (cmd=1)  例: rel -50
  setspeed <rpm>     -> SET_MOVE_SPEED      (cmd=3)  例: setspeed 30
  setzero            -> SET_REL_ZERO        (cmd=5)
  pos                -> GET_REL_POSITION    (cmd=2)
  speed              -> GET_MOVE_SPEED      (cmd=4)
  query <operationId>-> QUERY_OPERATION     (cmd=6)  例: query 0x....（运动后自动轮询）
  group <name>       -> 切换默认分组
  raw <json>         -> 直接发送原始 JSON
  help / quit

所有运动/写入命令都会自动提取 operationId 并轮询 cmd=6 直到终态。
"""

import socket
import json
import sys
import time
import argparse
from dataclasses import dataclass

# 统一输出编码为 UTF-8，避免 Windows GBK 控制台打印中文/符号乱码（交互与管道均适用）
if sys.platform == "win32":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

# ──────────────────────────────────────────────────────────────
# 协议常量（与 application/udp/UdpProtocol.h 保持一致）
# ──────────────────────────────────────────────────────────────
CMD_MOVE_TO_REL_TARGET = 0
CMD_MOVE_OFFSET        = 1
CMD_GET_REL_POSITION   = 2
CMD_SET_MOVE_SPEED     = 3
CMD_GET_MOVE_SPEED     = 4
CMD_SET_REL_ZERO       = 5
CMD_QUERY_OPERATION    = 6

R_MOTOR_ID = 2  # R 轴

# 终态集合（到达后无需继续轮询）
TERMINAL_STATES = {
    "Succeeded", "Failed", "Cancelled",
    "TimedOut", "CommitUncertain", "Rejected",
}


@dataclass
class Settings:
    host: str = "127.0.0.1"
    port: int = 62000
    group: str = "Machine_A"
    timeout: float = 5.0          # 单包回复超时
    poll_interval: float = 0.2    # operationId 轮询间隔
    max_polls: int = 150          # 最大轮询次数（约30s）


# ──────────────────────────────────────────────────────────────
# 通讯核心
# ──────────────────────────────────────────────────────────────
class UdpControl:
    def __init__(self, settings: Settings):
        self.s = settings
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(self.s.timeout)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def send(self, payload: dict, quiet: bool = False) -> dict | None:
        """发送 JSON 命令并接收单包回复。成功返回解析后的 dict，失败/超时返回 None。"""
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        if not quiet:
            print(f"\n>>> SEND {len(data)}B: {data.decode()}")
        try:
            self.sock.sendto(data, (self.s.host, self.s.port))
        except OSError as e:
            print(f"!!! SEND FAILED: {e}")
            return None
        try:
            resp, addr = self.sock.recvfrom(65535)
        except socket.timeout:
            print(f"!!! TIMEOUT: no reply within {self.s.timeout}s")
            return None
        except OSError as e:
            print(f"!!! RECV FAILED: {e}")
            return None
        try:
            reply = json.loads(resp.decode("utf-8"))
        except (json.JSONDecodeError, UnicodeDecodeError):
            print(f"!!! Invalid JSON reply: {resp[:200]!r}")
            return None
        if not quiet:
            print(f"<<< REPLY from {addr[0]}:{addr[1]} ({len(resp)}B):")
            print("    " + json.dumps(reply, indent=4, ensure_ascii=False).replace("\n", "\n    "))
        return reply

    # ── 命令封装 ──
    def move_to_rel_target(self, target: float, group: str | None = None) -> dict | None:
        return self.send({"cmd": CMD_MOVE_TO_REL_TARGET, "group": group or self.s.group,
                          "motor": R_MOTOR_ID, "target": target})

    def move_offset(self, offset: float, group: str | None = None) -> dict | None:
        return self.send({"cmd": CMD_MOVE_OFFSET, "group": group or self.s.group,
                          "motor": R_MOTOR_ID, "offset": offset})

    def set_move_speed(self, speed: float, group: str | None = None) -> dict | None:
        return self.send({"cmd": CMD_SET_MOVE_SPEED, "group": group or self.s.group,
                          "motor": R_MOTOR_ID, "speed": speed})

    def set_rel_zero(self, group: str | None = None) -> dict | None:
        return self.send({"cmd": CMD_SET_REL_ZERO, "group": group or self.s.group,
                          "motor": R_MOTOR_ID})

    def get_rel_position(self, group: str | None = None) -> dict | None:
        return self.send({"cmd": CMD_GET_REL_POSITION, "group": group or self.s.group,
                          "motor": R_MOTOR_ID})

    def get_move_speed(self, group: str | None = None) -> dict | None:
        return self.send({"cmd": CMD_GET_MOVE_SPEED, "group": group or self.s.group,
                          "motor": R_MOTOR_ID})

    def query_operation(self, op_id: str, quiet: bool = False) -> dict | None:
        return self.send({"cmd": CMD_QUERY_OPERATION, "operationId": op_id}, quiet=quiet)

    # ── 异步等待 ──
    def submit_and_wait(self, reply: dict | None, label: str = "operation",
                        quiet: bool = False) -> dict | None:
        """提交后若带回 operationId，则轮询 cmd=6 至终态；返回最终查询回复。"""
        if reply is None:
            return None
        if reply.get("result") != 1:
            print(f"!! {label}: submission failed (result != 1)")
            return reply
        op_id = reply.get("operationId")
        if not op_id:
            # 查询类命令：无 operationId，直接返回
            return reply
        print(f"[wait] {label}: operationId={op_id} -> polling until terminal state...")
        for _ in range(self.s.max_polls):
            time.sleep(self.s.poll_interval)
            q = self.query_operation(op_id, quiet=quiet)
            if q is None:
                print("!! poll got no reply")
                return q
            state = q.get("state")
            if state in TERMINAL_STATES:
                if not quiet:
                    print(f"[done] {label}: terminal state = {state}")
                return q
        print(f"!! {label}: polling exhausted after {self.s.max_polls} polls")
        return None

    def ok(self, reply: dict | None) -> bool:
        """判定一次命令整体成功（提交成功且异步终态为 Succeeded）。"""
        if reply is None:
            return False
        if reply.get("result") != 1:
            return False
        # 有 operationId 时以终态为准
        state = reply.get("state")
        if state is not None:
            return state == "Succeeded"
        return True


# ──────────────────────────────────────────────────────────────
# 自动化冒烟测试序列
# ──────────────────────────────────────────────────────────────
def run_auto(c: UdpControl, group: str) -> int:
    """执行一组冒烟断言。返回 0 表示全部通过，1 表示有失败。"""
    failures: list[str] = []
    c.s.group = group
    print(f"\n{'='*62}\n  AUTO 冒烟测试  group={group}  {c.s.host}:{c.s.port}\n{'='*62}")

    def check(name, cond, detail=""):
        mark = "[PASS]" if cond else "[FAIL]"
        print(f"  {mark}  {name}{'  ' + detail if detail else ''}")
        if not cond:
            failures.append(f"{name} {detail}")

    # 1) 查询类
    pos = c.get_rel_position()
    check("GET_REL_POSITION", pos is not None and pos.get("result") == 1,
          "" if not pos else f"curr={pos.get('curr')}")
    spd = c.get_move_speed()
    check("GET_MOVE_SPEED", spd is not None and spd.get("result") == 1,
          "" if not spd else f"speed={spd.get('speed')}")

    # 2) 写入类（异步轮询）
    check("SET_MOVE_SPEED", c.ok(c.submit_and_wait(c.set_move_speed(30), "setspeed", quiet=True)),
          "speed=30")
    check("SET_REL_ZERO", c.ok(c.submit_and_wait(c.set_rel_zero(), "setzero", quiet=True)), "")
    check("MOVE_OFFSET", c.ok(c.submit_and_wait(c.move_offset(10), "move_offset", quiet=True)),
          "offset=10")
    check("MOVE_TO_REL_TARGET", c.ok(c.submit_and_wait(c.move_to_rel_target(20), "move", quiet=True)),
          "target=20")

    # 3) 错误矩阵（期望 result=0）
    err_cases = [
        ("missing cmd",          {"group": group, "motor": 2}),
        ("missing motor",        {"cmd": 0, "group": group}),
        ("missing group",        {"cmd": 0, "motor": 2}),
        ("bad motor (motor=0)",  {"cmd": 0, "motor": 0, "group": group}),
        ("bad group",            {"cmd": 0, "motor": 2, "group": "NoSuchGroup"}),
        ("unknown cmd=99",       {"cmd": 99, "motor": 2, "group": group}),
        ("missing target",       {"cmd": 0, "motor": 2, "group": group}),
        ("missing offset",       {"cmd": 1, "motor": 2, "group": group}),
        ("missing speed",        {"cmd": 3, "motor": 2, "group": group}),
        ("missing opId (cmd=6)", {"cmd": 6}),
    ]
    for name, payload in err_cases:
        r = c.send(payload, quiet=True)
        check(name, r is not None and r.get("result") == 0,
              "" if not r else f"msg={r.get('msg')!r}")

    # 4) 非法 JSON
    c.sock.settimeout(c.s.timeout)
    c.sock.sendto(b"this is not json", (c.s.host, c.s.port))
    try:
        raw, _ = c.sock.recvfrom(65535)
        bad = json.loads(raw.decode())
        check("invalid JSON", bad.get("result") == 0 and "json" in bad.get("msg", "").lower(),
              f"msg={bad.get('msg')!r}")
    except (socket.timeout, json.JSONDecodeError):
        check("invalid JSON", False, "no proper reply")

    # 汇总
    print(f"\n{'='*62}")
    print(f"  RESULT: {len(failures)} failure(s)")
    for f in failures:
        print(f"    - {f}")
    print("=" * 62)
    return 1 if failures else 0


# ──────────────────────────────────────────────────────────────
# 交互模式
# ──────────────────────────────────────────────────────────────
def run_interactive(c: UdpControl):
    print(f"servoV6 UDP Control  (target={c.s.host}:{c.s.port}, group={c.s.group})")
    print("Type 'help' for commands, 'quit' to exit.\n")
    group = c.s.group
    while True:
        try:
            line = input("udp> ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nBye.")
            break
        if not line:
            continue

        low = line.lower()
        if low in ("quit", "exit", "q"):
            print("Bye.")
            break
        if low == "help":
            print("commands: move <t> | rel <d> | setspeed <v> | setzero | pos | speed | "
                  "query <opId> | group <name> | raw <json> | help | quit")
            continue
        if low.startswith("group "):
            group = line.split(maxsplit=1)[1].strip()
            print(f"Switched group: {group}")
            continue
        if low.startswith("move "):
            c.submit_and_wait(c.move_to_rel_target(_arg(line, "target"), group), "move")
            continue
        if low.startswith("rel "):
            c.submit_and_wait(c.move_offset(_arg(line, "offset"), group), "rel")
            continue
        if low.startswith("setspeed "):
            c.submit_and_wait(c.set_move_speed(_arg(line, "speed"), group), "setspeed")
            continue
        if low == "setzero":
            c.submit_and_wait(c.set_rel_zero(group), "setzero")
            continue
        if low == "pos":
            c.get_rel_position(group)
            continue
        if low == "speed":
            c.get_move_speed(group)
            continue
        if low.startswith("query "):
            c.query_operation(line.split(maxsplit=1)[1].strip())
            continue
        if low.startswith("raw "):
            try:
                payload = json.loads(line[4:].strip())
            except json.JSONDecodeError as e:
                print(f"Invalid JSON: {e}")
                continue
            c.send(payload)
            continue
        print(f"Unknown command: '{line}'. Type 'help'.")


def _arg(line: str, name: str) -> float:
    try:
        return float(line.split(maxsplit=1)[1])
    except (ValueError, IndexError):
        print(f"Usage: {name} must be a number")
        raise SystemExit(1)


def main():
    parser = argparse.ArgumentParser(description="servoV6 UDP Control Script")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=62000,
                        help="target port (default 62000, matches main.cpp UdpServer)")
    parser.add_argument("--group", default="Machine_A")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--poll-interval", type=float, default=0.2)
    parser.add_argument("--max-polls", type=int, default=150)
    parser.add_argument("--auto", action="store_true", help="run automated smoke tests and exit")
    args = parser.parse_args()

    s = Settings(host=args.host, port=args.port, group=args.group,
                 timeout=args.timeout, poll_interval=args.poll_interval,
                 max_polls=args.max_polls)
    c = UdpControl(s)
    try:
        if args.auto:
            rc = run_auto(c, args.group)
            sys.exit(rc)
        run_interactive(c)
    finally:
        c.close()


if __name__ == "__main__":
    main()
