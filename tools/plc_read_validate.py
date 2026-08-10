#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
PLC 变量协议 · Modbus 最终地址表 —— 只读校验脚本
==================================================

依据《PLC变量协议_Modbus最终地址表.md》实现：
  * Modbus TCP 只读：FC01 读线圈 / FC03 读保持寄存器
  * 0 基址寻址：M192 的 PDU 线圈地址就是 192；D 区同理，不加 1、不加 40001
  * 32 位字序（低字在低地址）：
        u32 = D[n] | (D[n+1] << 16)
        例如 1.0f = 0x3F800000 -> D[n]=0x0000, D[n+1]=0x3F80
  * 覆盖区域：
        - 标准 16 轴 D 区（3.1）    - M 线圈区（3.2）
        - 旧联动兼容区（4）        - AxisTopology（5）
        - GantryParam（6）         - GantryCommand（7）
        - GantryStatus（8）

本脚本为纯只读工具，不实现任何 Modbus 写功能。

用法：
    python plc_read_validate.py                 # 默认联机 192.168.1.88 全量读取
    python plc_read_validate.py --selftest      # 离线自测解析逻辑（无需 PLC）
    python plc_read_validate.py --host IP --only status   # 指定 IP / 只读某个区域
--only 可选值：axis / coils / legacy / topology / param / command / status
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
from typing import Dict, List, Tuple


# ---------------------------------------------------------------------------
# 1. Modbus TCP 只读客户端（原生 socket，精确控制 PDU 原始地址）
# ---------------------------------------------------------------------------

class ModbusError(RuntimeError):
    """Modbus 异常响应（0x01/0x02/0x03/0x04 ...）。"""

    EXC_TEXT = {0x01: "Illegal Function", 0x02: "Illegal Address",
                0x03: "Illegal Value", 0x04: "Slave Device Failure",
                0x06: "Slave Device Busy"}

    def __init__(self, code: int):
        self.code = code
        self.text = self.EXC_TEXT.get(code, "Unknown")
        super().__init__(f"Modbus Exception 0x{code:02X} ({self.text})")


class ModbusTcpClient:
    """最小化 Modbus TCP 客户端 —— 只实现读操作（FC01 / FC03）。"""

    def __init__(self, host: str, port: int = 502, unit: int = 1,
                 timeout: float = 3.0):
        self.host = host
        self.port = port
        self.unit = unit
        self.timeout = timeout
        self._sock = None
        self._tid = 0

    # -- 连接管理 ----------------------------------------------------------
    def connect(self) -> None:
        if self._sock is not None:
            return
        self._sock = socket.create_connection((self.host, self.port), self.timeout)
        self._sock.settimeout(self.timeout)

    def close(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
        self._sock = None

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    # -- 底层事务 ----------------------------------------------------------
    def _recv_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self._sock.recv(n - len(buf))
            if not chunk:
                raise ConnectionError("连接被 PLC 关闭")
            buf += chunk
        return bytes(buf)

    def _transaction(self, fc: int, payload: bytes) -> bytes:
        """发送一个 ADU，返回去除功能码之后的 PDU 数据部分。"""
        if self._sock is None:
            raise ConnectionError("尚未连接")
        self._tid = (self._tid + 1) & 0xFFFF
        tid = self._tid
        pdu = bytes([fc]) + payload
        # MBAP: TransactionId(2) + ProtocolId(2)=0 + Length(2) + UnitId(1)
        mbap = struct.pack(">HHHB", tid, 0, len(pdu) + 1, self.unit)
        self._sock.sendall(mbap + pdu)

        hdr = self._recv_exact(7)
        rtid, _proto, rlen, runit = struct.unpack(">HHHB", hdr)
        if rtid != tid:
            raise ConnectionError(f"事务号不匹配: 期望 {tid}, 收到 {rtid}")
        if runit != self.unit:
            raise ConnectionError(f"Unit ID 不匹配: 期望 {self.unit}, 收到 {runit}")
        body = self._recv_exact(rlen - 1)
        rfc = body[0]
        if rfc & 0x80:
            raise ModbusError(body[1])
        if rfc != fc:
            raise ConnectionError(f"功能码不匹配: 期望 0x{fc:02X}, 收到 0x{rfc:02X}")
        return body[1:]

    # -- 读线圈（FC01） ----------------------------------------------------
    def read_coils(self, address: int, count: int, chunk: int = 2000) -> List[bool]:
        """读取 count 个线圈，PDU 地址为 0 基址（直接等于 M 编号）。"""
        bits: List[bool] = []
        a, remaining = address, count
        while remaining > 0:
            n = min(remaining, chunk)
            body = self._transaction(0x01, struct.pack(">HH", a, n))
            byte_count = body[0]
            data = body[1:1 + byte_count]
            for i in range(n):
                bits.append(bool((data[i // 8] >> (i % 8)) & 1))
            a += n
            remaining -= n
        return bits

    # -- 读保持寄存器（FC03） ----------------------------------------------
    def read_holding_registers(self, address: int, count: int,
                               chunk: int = 125) -> List[int]:
        """读取 count 个保持寄存器，PDU 地址为 0 基址（直接等于 D 编号）。

        标准 Modbus 每次 FC03 最多 125 个寄存器，因此按 125 分片拼接。
        """
        regs: List[int] = []
        a, remaining = address, count
        while remaining > 0:
            n = min(remaining, chunk)
            body = self._transaction(0x03, struct.pack(">HH", a, n))
            byte_count = body[0]
            if byte_count != 2 * n:
                raise ConnectionError(f"字节数异常: 期望 {2*n}, 收到 {byte_count}")
            data = body[1:1 + byte_count]
            regs.extend(struct.unpack(f">{n}H", data))
            a += n
            remaining -= n
        return regs


# ---------------------------------------------------------------------------
# 2. 值解码工具（严格遵循 32 位低字在前 / 寄存器内高字节在前）
# ---------------------------------------------------------------------------

def _to_int16(reg: int) -> int:
    """无符号寄存器 -> 有符号 INT16。"""
    return struct.unpack(">h", struct.pack(">H", reg & 0xFFFF))[0]


def _u32(lo: int, hi: int) -> int:
    """u32 = D[n] | (D[n+1] << 16)：lo 为低地址寄存器，hi 为高地址寄存器。"""
    return ((hi & 0xFFFF) << 16) | (lo & 0xFFFF)


def _real32(lo: int, hi: int) -> float:
    return struct.unpack(">f", struct.pack(">I", _u32(lo, hi)))[0]


def _dint32(lo: int, hi: int) -> int:
    return struct.unpack(">i", struct.pack(">I", _u32(lo, hi)))[0]


# ---------------------------------------------------------------------------
# 3. 枚举 / 文本映射（源自地址表 3.3 / 5 / 8）
# ---------------------------------------------------------------------------

MOTION_TEXT = {0: "轴控入口未使能", 1: "空闲", 2: "正向点动", 3: "反向点动",
               4: "绝对定位", 5: "相对定位"}
LIMIT_TEXT = {0: "无限位", 1: "正软限位", 2: "负软限位", 3: "正硬限位", 4: "负硬限位"}
ALARM_BITS = {
    0: "软件急停", 1: "旧联动职责占用冲突(新不用)", 2: "旧联动建立超差(新不用)",
    3: "旧联动成员配置错误(新不用)", 4: "旧联动运行超差(新不用)",
    5: "轴功能块/伺服/轴命令错误", 6: "点动心跳超时",
}
GROUPCODE_TEXT = {0: "A", 1: "B"}
AXIS_CLASS_TEXT = {0: "线性", 1: "旋转", 2: "虚轴"}
UNIT_TYPE_TEXT = {0: "mm", 1: "degree"}
MOTION_MODE_TEXT = {0: "未用", 1: "龙门X1", 2: "龙门X2", 3: "线性单轴",
                    4: "旋转单轴", 5: "龙门逻辑轴"}
STATE_TEXT = {0: "未配置", 1: "已配置·已解除", 2: "建立中", 3: "已联动",
              4: "解除中", 5: "联动故障"}
CMD_RESULT_TEXT = {0: "无结果", 1: "处理中", 2: "成功", 3: "拒绝", 4: "失败"}
CMD_TEXT = {0: "无", 1: "建立", 2: "解除", 3: "安全复位"}


# ---------------------------------------------------------------------------
# 4. 寄存器字段模型与通用解析
# ---------------------------------------------------------------------------

class RegField:
    """一个字段的布局描述。

    offset: 相对所在寄存器块基址的偏移（D 字数量）
    dtype : 'REAL' | 'DINT' | 'INT' | 'WORD' | 'BOOL'
    bit   : 仅 BOOL 有效，bit 在所在寄存器内的位序号
    """

    def __init__(self, name: str, offset: int, dtype: str, bit: int = 0):
        self.name = name
        self.offset = offset
        self.dtype = dtype
        self.bit = bit


def parse_fields(regs: List[int], fields: List[RegField]) -> Dict[str, object]:
    """把一段连续寄存器按字段描述解析为 {字段名: 值}。"""
    out: Dict[str, object] = {}
    for f in fields:
        idx = f.offset
        if f.dtype == "REAL":
            out[f.name] = _real32(regs[idx], regs[idx + 1])
        elif f.dtype == "DINT":
            out[f.name] = _dint32(regs[idx], regs[idx + 1])
        elif f.dtype == "INT":
            out[f.name] = _to_int16(regs[idx])
        elif f.dtype == "WORD":
            out[f.name] = regs[idx] & 0xFFFF
        elif f.dtype == "BOOL":
            out[f.name] = bool((regs[idx] >> f.bit) & 1)
        else:
            raise ValueError(f"未知类型 {f.dtype}")
    return out


# ---------------------------------------------------------------------------
# 5. 地址块定义（全部 0 基址）
# ---------------------------------------------------------------------------

# 5.1 标准 16 轴 D 区（地址表 3.1）: (名称, 基址, 元素个数, 类型, 单位)
#     REAL 每项占 2 D；INT/WORD 每项占 1 D
STANDARD_AXIS_BLOCKS: List[Tuple[str, int, int, str, str]] = [
    ("手动速度",       0,   16, "REAL", "EU/s"),
    ("定位速度",       32,  16, "REAL", "EU/s"),
    ("绝对位置",       64,  16, "REAL", "EU"),
    ("相对位置",       96,  16, "REAL", "EU"),
    ("运动状态",       128, 16, "INT",  ""),
    ("运动限制",       144, 16, "INT",  ""),
    ("告警码",         160, 16, "WORD", ""),
    ("相对原点记录",   1064, 16, "REAL", "EU"),
    ("绝对定位距离",   1096, 16, "REAL", "EU"),
    ("相对定位距离",   1128, 16, "REAL", "EU"),
    ("软件负限位",     1160, 16, "REAL", "EU"),
    ("软件正限位",     1192, 16, "REAL", "EU"),
    ("超差阈值(兼容)", 1224, 3,  "INT",  ""),
    ("软限位控制",     1228, 16, "WORD", ""),
    ("职责(兼容/待定)",1244, 16, "INT",  ""),
]

# 5.2 M 线圈区（地址表 3.2）: (名称, 起始M, 个数, 语义)
COIL_BLOCKS: List[Tuple[str, int, int, str]] = [
    ("使能轴控",            0,   16, "保持电平"),
    ("相对原点清除",        16,  16, "上升沿·PLC自复位"),
    ("绝对位置清零",        32,  16, "上升沿·PLC自复位"),
    ("绝对定位触发",        48,  16, "客户端脉冲 ON→OFF"),
    ("相对定位触发",        64,  16, "客户端脉冲 ON→OFF"),
    ("点动正转",            80,  16, "保持电平"),
    ("点动反转",            96,  16, "保持电平"),
    ("报警解除触发",        112, 16, "不可用(已注释)"),
    ("使能电机",            128, 16, "保持电平"),
    ("相对定位终止触发",    144, 16, "客户端脉冲 ON→OFF"),
    ("绝对定位终止触发",    160, 16, "客户端脉冲 ON→OFF"),
    ("相对原点设置",        176, 16, "上升沿·PLC自复位"),
    ("点动心跳",            192, 16, "看门狗刷新"),
    ("告警码置零",          208, 16, "一次命令·PLC自复位"),
    ("设备急停",            224, 1,  "保持电平·锁存"),
    ("设备急停解除",        225, 1,  "上升沿解除"),
]


# 5.3 结构体字段（偏移均为相对各自基址）

# AxisTopology 头部字段，基址 D1400（地址表 5.1）
TOPOLOGY_HEAD_FIELDS = [
    RegField("Magic",          0,   "DINT"),
    RegField("SchemaVersion",  2,   "INT"),
    RegField("Reserved",       3,   "INT"),
    RegField("Revision",       4,   "DINT"),
    # D1406..D1407 为编译器对齐空洞，禁止读写依赖，不解析
    RegField("ConfigCRC",      174, "DINT"),   # D1574..D1575
    RegField("ConfigValid",    176, "BOOL", bit=0),  # D1576.bit0
    RegField("ConfigErrorCode",177, "INT"),          # D1577
]

# GantryCommand 字段，基址 D180，步长 4 D（地址表 7）
GANTRY_COMMAND_FIELDS = [
    RegField("Command",    0, "INT"),    # 0无 1建立 2解除 3安全复位
    RegField("RequestSeq", 1, "DINT"),
    RegField("Reserved",   3, "INT"),
]

# GantryStatus 字段，基址 D190，步长 18 D（地址表 8）
GANTRY_STATUS_FIELDS = [
    RegField("State",                   0,  "INT"),
    RegField("InternalStep",            1,  "INT"),
    RegField("AckSeq",                  2,  "DINT"),
    RegField("CommandResult",           4,  "INT"),
    RegField("CommandErrorCode",        5,  "INT"),
    RegField("ReadyToCouple",           6,  "BOOL", bit=0),
    RegField("ReadyToDecouple",         6,  "BOOL", bit=1),
    RegField("MemberControlAllowed",    6,  "BOOL", bit=2),
    RegField("LogicalControlAllowed",   6,  "BOOL", bit=3),
    RegField("X1InGear",                6,  "BOOL", bit=4),
    RegField("X2InGear",                6,  "BOOL", bit=5),
    RegField("X1Position",              7,  "REAL"),
    RegField("X2Position",              9,  "REAL"),
    RegField("LogicalPosition",         11, "REAL"),
    RegField("Skew",                    13, "REAL"),
    RegField("Fault",                   15, "BOOL", bit=0),
    RegField("FaultCode",               16, "INT"),
    RegField("Reserved",                17, "INT"),
]

# GantryParam 字段，基址 D1600，步长 22 D（地址表 6）
GANTRY_PARAM_FIELDS = [
    RegField("Valid",                0,  "BOOL", bit=0),
    RegField("DirectionX1",          1,  "INT"),
    RegField("DirectionX2",          2,  "INT"),
    RegField("RatioNumeratorX1",     3,  "INT"),
    RegField("RatioDenominatorX1",   4,  "INT"),
    RegField("RatioNumeratorX2",     5,  "INT"),
    RegField("RatioDenominatorX2",   6,  "INT"),
    RegField("PositionOffsetX1",     7,  "REAL"),
    RegField("PositionOffsetX2",     9,  "REAL"),
    RegField("CoupleSkewLimit",      11, "REAL"),
    RegField("RunningSkewLimit",     13, "REAL"),
    RegField("SkewDelayMs",          15, "DINT"),
    RegField("CoupleTimeoutMs",      17, "DINT"),
    RegField("DecoupleTimeoutMs",    19, "DINT"),
    RegField("Reserved",             21, "INT"),
]

# 角色字段，相对 role_base（地址表 5.2）
ROLE_FIELDS = [
    RegField("Valid",         0, "BOOL", bit=0),
    RegField("HmiVisible",    0, "BOOL", bit=1),
    RegField("PlcAxisIndex",  1, "INT"),
    RegField("MotorNo",       2, "INT"),
    RegField("AxisClass",     3, "DINT"),
    RegField("UnitType",      5, "DINT"),
    RegField("MotionMode",    7, "DINT"),
    RegField("Reserved",      9, "INT"),
]

# 组字段，相对 group_base（地址表 5.2）
GROUP_FIELDS = [
    RegField("Valid",        0, "BOOL", bit=0),
    RegField("HmiVisible",   0, "BOOL", bit=1),
    RegField("GroupCode",    1, "INT"),
    RegField("Reserved",     2, "INT"),
]

# 拓扑常量（地址表 5.1）
TOPOLOGY_BASE = 1400
GROUP_BASE = 1408        # D1408
GROUP_STRIDE = 83        # 每组 83 D
ROLE_BASE_IN_GROUP = 3   # 组头占 3 D
ROLE_STRIDE = 10         # 每个角色占 10 D
N_ROLES = 8
N_GROUPS = 2


# ---------------------------------------------------------------------------
# 6. 各区域读取 + 解析
# ---------------------------------------------------------------------------

def read_standard_axis(client: ModbusTcpClient) -> Dict[str, object]:
    """读取并解析标准 16 轴 D 区（3.1）。"""
    result: Dict[str, object] = {}
    for name, base, count, dtype, unit in STANDARD_AXIS_BLOCKS:
        try:
            regs = client.read_holding_registers(base, count)
        except Exception as e:
            result[name] = {"_error": str(e)}
            continue
        width = 2 if dtype in ("REAL", "DINT") else 1
        n_items = count // width
        values = []
        for i in range(n_items):
            off = i * width
            if dtype == "REAL":
                values.append(_real32(regs[off], regs[off + 1]))
            elif dtype == "INT":
                values.append(_to_int16(regs[off]))
            else:  # WORD
                values.append(regs[off] & 0xFFFF)
        result[name] = {"dtype": dtype, "unit": unit, "values": values}
    return result


def read_coils(client: ModbusTcpClient) -> Dict[str, object]:
    """读取并解析 M 线圈区（3.2）。一次读完 M0..M225。"""
    total = COIL_BLOCKS[-1][1] + COIL_BLOCKS[-1][2]  # 226
    bits = client.read_coils(0, total)
    result: Dict[str, object] = {}
    for name, start, count, note in COIL_BLOCKS:
        slice_ = bits[start:start + count]
        result[name] = {"start": start, "count": count, "note": note,
                        "on_indices": [i for i, b in enumerate(slice_) if b]}
    return result


def _parse_axis_topology(regs: List[int]) -> Dict[str, object]:
    """从 D1400..D1577 的 178 个寄存器解析 AxisTopology（5）。"""
    head = parse_fields(regs, TOPOLOGY_HEAD_FIELDS)
    groups = []
    for g in range(N_GROUPS):
        gstart = GROUP_BASE - TOPOLOGY_BASE + g * GROUP_STRIDE  # 组头相对基址
        gf = {}
        gf["Valid"] = bool((regs[gstart] >> 0) & 1)
        gf["HmiVisible"] = bool((regs[gstart] >> 1) & 1)
        gf["GroupCode"] = _to_int16(regs[gstart + 1])
        gf["Reserved"] = _to_int16(regs[gstart + 2])
        roles = []
        for r in range(N_ROLES):
            r_base = gstart + ROLE_BASE_IN_GROUP + r * ROLE_STRIDE
            rf = {}
            rf["Valid"] = bool((regs[r_base] >> 0) & 1)
            rf["HmiVisible"] = bool((regs[r_base] >> 1) & 1)
            rf["PlcAxisIndex"] = _to_int16(regs[r_base + 1])
            rf["MotorNo"] = _to_int16(regs[r_base + 2])
            rf["AxisClass"] = _dint32(regs[r_base + 3], regs[r_base + 4])
            rf["UnitType"] = _dint32(regs[r_base + 5], regs[r_base + 6])
            rf["MotionMode"] = _dint32(regs[r_base + 7], regs[r_base + 8])
            rf["Reserved"] = _to_int16(regs[r_base + 9])
            roles.append(rf)
        gf["roles"] = roles
        groups.append(gf)
    return {"head": head, "groups": groups}


def read_topology(client: ModbusTcpClient) -> Dict[str, object]:
    """读取 D1400..D1577（178 个寄存器）并解析 AxisTopology。"""
    regs = client.read_holding_registers(TOPOLOGY_BASE, 178)
    return _parse_axis_topology(regs)


def read_gantry_param(client: ModbusTcpClient) -> Dict[str, object]:
    """读取 D1600..D1643（2 组 × 22 D）并解析 GantryParam（6）。"""
    regs = client.read_holding_registers(1600, 44)
    out = {}
    for g in range(2):
        out[g] = parse_fields(regs[g * 22:(g + 1) * 22], GANTRY_PARAM_FIELDS)
    return out


def read_gantry_command(client: ModbusTcpClient) -> Dict[str, object]:
    """读取 D180..D187（2 组 × 4 D）并解析 GantryCommand（7）。"""
    regs = client.read_holding_registers(180, 8)
    out = {}
    for g in range(2):
        out[g] = parse_fields(regs[g * 4:(g + 1) * 4], GANTRY_COMMAND_FIELDS)
    return out


def read_gantry_status(client: ModbusTcpClient) -> Dict[str, object]:
    """读取 D190..D225（2 组 × 18 D）并解析 GantryStatus（8）。"""
    regs = client.read_holding_registers(190, 36)
    out = {}
    for g in range(2):
        out[g] = parse_fields(regs[g * 18:(g + 1) * 18], GANTRY_STATUS_FIELDS)
    return out


def read_legacy(client: ModbusTcpClient) -> Dict[str, object]:
    """读取旧联动兼容区（4）。"""
    # 4.1 联动成员信息[0..9]: D1260 + 4*i
    member = client.read_holding_registers(1260, 40)
    members = []
    for i in range(10):
        off = i * 4
        members.append({
            "Enable": bool((member[off] >> 0) & 1),
            "AxisNo": _to_int16(member[off + 1]),
            "IsMaster": _dint32(member[off + 2], member[off + 3]),
        })
    # 4.2 联动轴信息[0..2]: D1300 + 3*g
    group = client.read_holding_registers(1300, 9)
    legroups = []
    for i in range(3):
        off = i * 3
        legroups.append({
            "MasterAxis": _to_int16(group[off]),
            "SlaveAxis": _to_int16(group[off + 1]),
            "Valid": bool((group[off + 2] >> 0) & 1),
        })
    return {"members": members, "groups": legroups}


# ---------------------------------------------------------------------------
# 7. 输出格式化
# ---------------------------------------------------------------------------

def _fmt_axis_block(name: str, info: object) -> List[str]:
    lines = [f"[D 区] {name}"]
    if isinstance(info, dict) and "_error" in info:
        lines.append(f"    读取失败: {info['_error']}")
        return lines
    dtype = info["dtype"]
    unit = info["unit"]
    vals = info["values"]
    for i, v in enumerate(vals):
        suffix = ""
        if dtype == "INT" and name.startswith("运动状态"):
            suffix = "  (" + MOTION_TEXT.get(v, f"未知{v}") + ")"
        elif dtype == "INT" and name.startswith("运动限制"):
            suffix = "  (" + LIMIT_TEXT.get(v, f"未知{v}") + ")"
        elif dtype == "WORD" and name.startswith("告警码"):
            bits = [ALARM_BITS[b] for b in sorted(ALARM_BITS) if v & (1 << b)]
            suffix = "  {" + "; ".join(bits) + "}" if bits else ""
        unit_s = f" [{unit}]" if unit else ""
        lines.append(f"  i={i:>2}  D{_addr_of(name, i):>4}: {v}{unit_s}{suffix}")
    return lines


def _addr_of(name: str, i: int) -> int:
    """根据块名与下标计算 D 原始地址（用于显示，参考 2.1 公式）。"""
    base = next((b[1] for b in STANDARD_AXIS_BLOCKS if b[0] == name), 0)
    width = 2 if next((b[3] for b in STANDARD_AXIS_BLOCKS if b[0] == name), "") in ("REAL", "DINT") else 1
    return base + i * width


def print_axis(axis: Dict[str, object]) -> None:
    for name, info in axis.items():
        for line in _fmt_axis_block(name, info):
            print(line)
        print()


def print_coils(coils: Dict[str, object]) -> None:
    print("[M 区] 线圈")
    for name, info in coils.items():
        on = info["on_indices"]
        shown = ", ".join(str(s + info["start"]) for s in on) if on else "全 OFF"
        print(f"  {name} M{info['start']}..M{info['start']+info['count']-1} "
              f"({info['note']}): ON@[{shown}]")
    print()


def print_topology(topo: Dict[str, object]) -> None:
    print("[AxisTopology] D1400..D1577")
    h = topo["head"]
    print(f"  Magic=0x{h['Magic']:08X}  SchemaVersion={h['SchemaVersion']}  "
          f"Revision={h['Revision']}  ConfigCRC=0x{h['ConfigCRC']:08X}")
    print(f"  ConfigValid={h['ConfigValid']}  ConfigErrorCode={h['ConfigErrorCode']}")
    for g, gf in enumerate(topo["groups"]):
        print(f"  Group[{g}] Valid={gf['Valid']} HmiVisible={gf['HmiVisible']} "
              f"GroupCode={gf['GroupCode']}({GROUPCODE_TEXT.get(gf['GroupCode'],'?')})")
        for r, rf in enumerate(gf["roles"]):
            if not rf["Valid"]:
                continue
            print(f"    Role[{r}] PlcAxisIndex={rf['PlcAxisIndex']} MotorNo={rf['MotorNo']} "
                  f"AxisClass={rf['AxisClass']}({AXIS_CLASS_TEXT.get(rf['AxisClass'],'?')}) "
                  f"UnitType={rf['UnitType']}({UNIT_TYPE_TEXT.get(rf['UnitType'],'?')}) "
                  f"MotionMode={rf['MotionMode']}({MOTION_MODE_TEXT.get(rf['MotionMode'],'?')})")
    print()


def print_gantry_param(params: Dict[str, object]) -> None:
    for g, p in params.items():
        print(f"[GantryParam[{g}]] D{1600 + 22*g}..D{1621 + 22*g}")
        print(f"  Valid={p['Valid']}  DirectionX1={p['DirectionX1']}  DirectionX2={p['DirectionX2']}")
        print(f"  Ratio X1={p['RatioNumeratorX1']}/{p['RatioDenominatorX1']}  "
              f"X2={p['RatioNumeratorX2']}/{p['RatioDenominatorX2']}")
        print(f"  OffsetX1={p['PositionOffsetX1']:.4f}  OffsetX2={p['PositionOffsetX2']:.4f}")
        print(f"  CoupleSkewLimit={p['CoupleSkewLimit']:.4f}  RunningSkewLimit={p['RunningSkewLimit']:.4f}")
        print(f"  SkewDelayMs={p['SkewDelayMs']}  CoupleTimeoutMs={p['CoupleTimeoutMs']}  "
              f"DecoupleTimeoutMs={p['DecoupleTimeoutMs']}")
        print()


def print_gantry_command(cmds: Dict[str, object]) -> None:
    for g, c in cmds.items():
        print(f"[GantryCommand[{g}]] Command={c['Command']}({CMD_TEXT.get(c['Command'],'?')})  "
              f"RequestSeq={c['RequestSeq']}  Reserved={c['Reserved']}")
    print()


def print_gantry_status(sts: Dict[str, object]) -> None:
    for g, s in sts.items():
        print(f"[GantryStatus[{g}]] D{190 + 18*g}..D{207 + 18*g}")
        print(f"  State={s['State']}({STATE_TEXT.get(s['State'],'?')})  InternalStep={s['InternalStep']}")
        print(f"  AckSeq={s['AckSeq']}  CommandResult={s['CommandResult']}"
              f"({CMD_RESULT_TEXT.get(s['CommandResult'],'?')})  "
              f"CommandErrorCode={s['CommandErrorCode']}")
        print(f"  ReadyToCouple={s['ReadyToCouple']}  ReadyToDecouple={s['ReadyToDecouple']}  "
              f"MemberControlAllowed={s['MemberControlAllowed']}  "
              f"LogicalControlAllowed={s['LogicalControlAllowed']}")
        print(f"  X1InGear={s['X1InGear']}  X2InGear={s['X2InGear']}")
        print(f"  X1Position={s['X1Position']:.4f}  X2Position={s['X2Position']:.4f}  "
              f"LogicalPosition={s['LogicalPosition']:.4f}  Skew={s['Skew']:.4f}")
        print(f"  Fault={s['Fault']}  FaultCode={s['FaultCode']}")
        print()


def print_legacy(legacy: Dict[str, object]) -> None:
    print("[旧联动兼容区]")
    print("  联动成员信息[0..9] D1260+4i:")
    for i, m in enumerate(legacy["members"]):
        print(f"    [{i}] Enable={m['Enable']} AxisNo={m['AxisNo']} IsMaster={m['IsMaster']}")
    print("  联动轴信息[0..2] D1300+3g:")
    for i, grp in enumerate(legacy["groups"]):
        print(f"    [{i}] Master={grp['MasterAxis']} Slave={grp['SlaveAxis']} Valid={grp['Valid']}")
    print()

# ---------------------------------------------------------------------------
# 8. 离线自测（不连 PLC，验证解析逻辑）
# ---------------------------------------------------------------------------

def _assert(name: str, cond: bool, detail: str = "") -> None:
    mark = "PASS" if cond else "FAIL"
    print(f"  [{mark}] {name}" + (f"  {detail}" if detail else ""))
    if not cond:
        raise SystemExit(f"自测失败: {name} {detail}")


def _run_selftest() -> None:
    print("=== 离线自测：解析逻辑（不连 PLC） ===\n")

    # (1) 32 位 REAL 字序：1.0f = 0x3F800000 -> 低地址 D[n]=0x0000, 高地址 D[n+1]=0x3F80
    regs = [0x0000, 0x3F80]
    _assert("REAL 1.0f 低字在前", _real32(regs[0], regs[1]) == 1.0,
            f"got {_real32(regs[0], regs[1])}")
    regs25 = [0x6666, 0x41CE]  # 25.8f = 0x41CE6666
    _assert("REAL 25.8f 字序", abs(_real32(regs25[0], regs25[1]) - 25.8) < 1e-3,
            f"got {_real32(regs25[0], regs25[1])}")

    # (2) DINT：0x12345678 -> 低地址 0x5678, 高地址 0x1234, 值 0x12345678
    _assert("DINT 字序", _dint32(0x5678, 0x1234) == 0x12345678)
    # 负数 DINT
    _assert("DINT 负数", _dint32(0x0000, 0xFFFF) == -65536)

    # (3) INT16 有符号
    _assert("INT16 有符号", _to_int16(0x8000) == -32768 and _to_int16(0x0001) == 1)

    # (4) 构建 AxisTopology 仿真寄存器，验证 group/role 偏移公式
    topo = [0] * 178
    # 头: D1400..1401 Magic=0x11223344, D1402 SchemaVersion=1, D1404..1405 Revision=7
    topo[0], topo[1] = 0x3344, 0x1122
    topo[2] = 1
    topo[4], topo[5] = 7, 0
    # ConfigValid D1576.bit0, ConfigErrorCode D1577=0
    topo[176] = 1
    topo[177] = 0
    # A 组 Role[0] X1: group_base(D1408)=off8, role_base = 8+3+10*0 = 11
    r0 = 11
    topo[r0] = 1            # Valid bit0
    topo[r0 + 1] = 0        # PlcAxisIndex
    topo[r0 + 2] = 1        # MotorNo
    topo[r0 + 3], topo[r0 + 4] = 0, 0     # AxisClass=0
    topo[r0 + 5], topo[r0 + 6] = 0, 0     # UnitType=0
    topo[r0 + 7], topo[r0 + 8] = 1, 0     # MotionMode=1
    # A 组 Role[5] X/SYN0: role_base = 8+3+10*5 = 61
    r5 = 61
    topo[r5] = 1
    topo[r5 + 1] = 13       # PlcAxisIndex=13
    topo[r5 + 2] = 0        # MotorNo=0(虚轴)
    topo[r5 + 3], topo[r5 + 4] = 2, 0     # AxisClass=2(虚轴)
    topo[r5 + 5], topo[r5 + 6] = 0, 0
    topo[r5 + 7], topo[r5 + 8] = 5, 0     # MotionMode=5(龙门逻辑轴)
    # B 组 Group[1].Valid=FALSE (默认 0 即可)
    p = _parse_axis_topology(topo)
    head = p["head"]
    _assert("Magic", head["Magic"] == 0x11223344)
    _assert("SchemaVersion", head["SchemaVersion"] == 1)
    _assert("Revision", head["Revision"] == 7)
    _assert("ConfigValid", head["ConfigValid"] is True)
    g0 = p["groups"][0]
    _assert("A组 Role0 X1",
            g0["roles"][0]["PlcAxisIndex"] == 0 and g0["roles"][0]["MotorNo"] == 1
            and g0["roles"][0]["MotionMode"] == 1)
    _assert("A组 Role5 SYN0",
            g0["roles"][5]["PlcAxisIndex"] == 13 and g0["roles"][5]["AxisClass"] == 2
            and g0["roles"][5]["MotionMode"] == 5)
    _assert("B组 Valid 默认 FALSE", p["groups"][1]["Valid"] is False)


    # (5) GantryStatus 仿真：State=3(已联动), AckSeq=9, InGear 全 TRUE
    st = [0] * 18
    st[0] = 3            # State
    st[1] = 80           # InternalStep=80 已联动
    st[2], st[3] = 9, 0  # AckSeq=9
    st[4] = 2            # CommandResult=成功
    st[6] = 0b111100     # bits4,5(X1InGear,X2InGear) 及 bit2/3
    s = parse_fields(st, GANTRY_STATUS_FIELDS)
    _assert("GantryStatus State", s["State"] == 3)
    _assert("GantryStatus AckSeq", s["AckSeq"] == 9)
    _assert("GantryStatus X1InGear/X2InGear", s["X1InGear"] and s["X2InGear"])

    # (6) GantryParam REAL 字段：PositionOffsetX1 写入 1.0
    pa = [0] * 22
    pa[0] = 1            # Valid
    pa[1] = 1            # DirectionX1=+1
    pa[7], pa[8] = 0x0000, 0x3F80  # PositionOffsetX1=1.0
    p2 = parse_fields(pa, GANTRY_PARAM_FIELDS)
    _assert("GantryParam Valid/DirectionX1", p2["Valid"] and p2["DirectionX1"] == 1)
    _assert("GantryParam PositionOffsetX1=1.0", abs(p2["PositionOffsetX1"] - 1.0) < 1e-6,
            f"got {p2['PositionOffsetX1']}")

    print("\n=== 自测全部通过 ===")


# ---------------------------------------------------------------------------
# 9. 命令行入口
# ---------------------------------------------------------------------------

def _parse_args():
    ap = argparse.ArgumentParser(description="PLC Modbus 地址表只读校验脚本")
    ap.add_argument("--host", default="192.168.1.88",
                    help="PLC IP（默认 192.168.1.88）")
    ap.add_argument("--port", type=int, default=502)
    ap.add_argument("--unit", type=int, default=1)
    ap.add_argument("--timeout", type=float, default=3.0)
    ap.add_argument("--selftest", action="store_true", help="离线自测解析逻辑")
    ap.add_argument("--only", choices=["axis", "coils", "legacy", "topology",
                                       "param", "command", "status"],
                    help="只读取某个区域")
    return ap.parse_args()


def _run_online(args) -> int:
    with ModbusTcpClient(args.host, args.port, args.unit, args.timeout) as client:
        only = args.only
        if only is None or only == "axis":
            print("== 标准 16 轴 D 区 (3.1) ==")
            print_axis(read_standard_axis(client))
        if only is None or only == "coils":
            print("== M 线圈区 (3.2) ==")
            print_coils(read_coils(client))
        if only is None or only == "legacy":
            print_legacy(read_legacy(client))
        if only is None or only == "topology":
            print_topology(read_topology(client))
        if only is None or only == "param":
            print_gantry_param(read_gantry_param(client))
        if only is None or only == "command":
            print_gantry_command(read_gantry_command(client))
        if only is None or only == "status":
            print_gantry_status(read_gantry_status(client))
    return 0


def main() -> int:
    args = _parse_args()
    if args.selftest:
        _run_selftest()
        return 0
    try:
        return _run_online(args)
    except (ConnectionError, ModbusError, OSError, socket.timeout) as e:
        print(f"错误: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

