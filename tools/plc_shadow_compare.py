#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plc_shadow_compare.py —— 阶段 2：legacy 与 vnext 只读结果逐字段对拍
==========================================================================
依据《servoV6剩余迁移工作实施方案》§10.2“真实只读影子运行”，对拍以下字段：
  - 拓扑 header：Magic / SchemaVersion / Revision / ConfigValid / ConfigErrorCode
  - A/B 组：groupCode / valid / 每个 role 的 valid 与 plcAxisIndex
    （无效 role 的 motionMode / axisClass 为 PLC 残留脏字段，显示层忽略 → SKIP）
  - 16 槽位：绝对位置 / 相对位置 / 运动状态 / 运动限制 / 告警码 / 手动速度 / 定位速度
  - 龙门 ×2：State / AckSeq / CommandResult / CommandErrorCode / 控制许可 / InGear / Fault
  - 急停：M224 / M225

用法（先各采集一份单次快照）：
  1) legacy 侧：python tools/plc_read_validate.py --json > legacy.json
  2) vnext 侧：tools/plc_vnext_readonly_probe --host IP --port 502 --unit 1 --poll 1 --json > vnext.json
  3) 对拍：   python tools/plc_shadow_compare.py --vnext vnext.json --legacy legacy.json

退出码（默认严格模式）：
  0 = 全部字段一致且无 SKIP（证据完整）
  1 = 参数/读取/JSON 解析错误
  2 = 存在字段差异
  3 = 无差异但存在 SKIP（证据不完整，不得视为通过）
  --allow-skip 时：SKIP 不触发非零（仅调试用）。
离线自测（无需 PLC）：python tools/plc_shadow_compare.py --selftest
"""
from __future__ import annotations

import argparse
import copy
import json
import re
import sys
from typing import Dict, List, Tuple


# ---------------------------------------------------------------------------
# 比较核心：逐字段判定，返回 (name, status, detail)
# ---------------------------------------------------------------------------

def _num_eq(a, b, tol: float) -> bool:
    """数值/布尔比较：任一浮点则用容差；否则精确。"""
    if isinstance(a, bool) or isinstance(b, bool):
        return a == b
    if isinstance(a, float) or isinstance(b, float):
        return abs(float(a) - float(b)) <= tol
    return a == b


def _check(name: str, a, b, tol: float) -> Tuple[str, str, str]:
    if _num_eq(a, b, tol):
        return (name, "PASS", f"{a!r} == {b!r}")
    return (name, "DIFF", f"legacy={a!r} vs vnext={b!r}")


def _cmp_topology(lt: Dict[str, object], vt: Dict[str, object],
                  tol: float) -> List[Tuple[str, str, str]]:
    rows: List[Tuple[str, str, str]] = []
    lh, vh = lt.get("head", {}), vt.get("header", {})
    for key in ("magic", "schemaVersion", "revision", "configValid",
                "configErrorCode"):
        rows.append(_check(f"topology.header.{key}", lh.get(key), vh.get(key), tol))
    lg = {g.get("index"): g for g in lt.get("groups", [])}
    vg = {g.get("index"): g for g in vt.get("groups", [])}
    for idx in sorted(set(lg) | set(vg)):
        lgrp, vgrp = lg.get(idx), vg.get(idx)
        if lgrp is None or vgrp is None:
            rows.append((f"topology.group[{idx}]", "SKIP",
                         "一侧缺失该组"))
            continue
        rows.append(_check(f"topology.group[{idx}].groupCode",
                           lgrp.get("groupCode"), vgrp.get("groupCode"), tol))
        rows.append(_check(f"topology.group[{idx}].valid",
                           lgrp.get("valid"), vgrp.get("valid"), tol))
        lr = lgrp.get("roles", [])
        vr = vgrp.get("roles", [])
        for i in range(max(len(lr), len(vr))):
            lr0 = lr[i] if i < len(lr) else None
            vr0 = vr[i] if i < len(vr) else None
            if lr0 is None or vr0 is None:
                rows.append((f"topology.group[{idx}].role[{i}]", "SKIP",
                             "一侧缺失该 role"))
                continue
            rows.append(_check(f"topology.group[{idx}].role[{i}].valid",
                               lr0.get("valid"), vr0.get("valid"), tol))
            rows.append(_check(f"topology.group[{idx}].role[{i}].plcAxisIndex",
                               lr0.get("plcAxisIndex"),
                               vr0.get("plcAxisIndex"), tol))
            # 无效 role：motionMode/axisClass 为 PLC 残留脏字段，vnext 探针显示层
            # 忽略（只输出 valid+plcAxisIndex），故此处 SKIP，不比对。
            valid = vr0.get("valid", False)
            if not valid:
                rows.append((f"topology.group[{idx}].role[{i}].motionMode/axisClass",
                             "SKIP_OK", "invalid role：脏字段忽略"))
                continue
            rows.append(_check(f"topology.group[{idx}].role[{i}].motionMode",
                               lr0.get("motionMode"), vr0.get("motionMode"), tol))
            rows.append(_check(f"topology.group[{idx}].role[{i}].axisClass",
                               lr0.get("axisClass"), vr0.get("axisClass"), tol))
    return rows


def _cmp_axis(la: List[Dict[str, object]], va: List[Dict[str, object]],
              tol: float) -> List[Tuple[str, str, str]]:
    rows: List[Tuple[str, str, str]] = []
    la_map = {a.get("slot"): a for a in la}
    va_map = {a.get("slot"): a for a in va}
    for idx in sorted(set(la_map) | set(va_map)):
        l, v = la_map.get(idx), va_map.get(idx)
        if l is None or v is None:
            rows.append((f"axis[{idx}]", "SKIP", "一侧缺失该槽位"))
            continue
        for key in ("absPosition", "relPosition", "manualSpeed",
                    "positioningSpeed"):
            rows.append(_check(f"axis[{idx}].{key}", l.get(key), v.get(key), tol))
        for key in ("motionState", "motionLimit", "alarmWord"):
            rows.append(_check(f"axis[{idx}].{key}", l.get(key), v.get(key), tol))
    return rows


def _cmp_gantry(lg: List[Dict[str, object]], vg: List[Dict[str, object]],
                tol: float) -> List[Tuple[str, str, str]]:
    rows: List[Tuple[str, str, str]] = []
    lg_map = {g.get("index"): g for g in lg}
    vg_map = {g.get("index"): g for g in vg}
    for idx in sorted(set(lg_map) | set(vg_map)):
        l, v = lg_map.get(idx), vg_map.get(idx)
        if l is None or v is None:
            rows.append((f"gantry[{idx}]", "SKIP", "一侧缺失该组"))
            continue
        for key in ("state", "ackSeq", "commandResult", "commandErrorCode"):
            rows.append(_check(f"gantry[{idx}].{key}", l.get(key), v.get(key), tol))
        for key in ("memberControlAllowed", "logicalControlAllowed",
                    "x1InGear", "x2InGear", "fault"):
            rows.append(_check(f"gantry[{idx}].{key}", l.get(key), v.get(key), tol))
    return rows


def _cmp_safety(ls: Dict[str, object], vs: Dict[str, object],
                tol: float) -> List[Tuple[str, str, str]]:
    return [
        _check("safety.M224_emergencyStop",
               ls.get("M224_emergencyStop"), vs.get("M224_emergencyStop"), tol),
        _check("safety.M225_release",
               ls.get("M225_release"), vs.get("M225_release"), tol),
    ]


def compare(legacy: Dict[str, object], vnext: Dict[str, object],
            tol: float) -> List[Tuple[str, str, str]]:
    rows: List[Tuple[str, str, str]] = []
    if "topology" in legacy or "topology" in vnext:
        if "topology" not in legacy or "topology" not in vnext:
            rows.append(("topology", "SKIP", "一侧缺少 topology"))
        else:
            rows += _cmp_topology(legacy["topology"], vnext["topology"], tol)

    l_axis = legacy.get("axis")
    v_axis = (vnext.get("runtime") or {}).get("axes")
    if l_axis is not None or v_axis is not None:
        if isinstance(l_axis, dict) and "_error" in l_axis:
            rows.append(("axis", "SKIP", f"legacy 读取失败: {l_axis['_error']}"))
        elif l_axis is None or v_axis is None:
            rows.append(("axis", "SKIP", "一侧缺少 axis"))
        else:
            rows += _cmp_axis(l_axis, v_axis, tol)

    l_gantry = legacy.get("gantry")
    v_gantry = (vnext.get("runtime") or {}).get("gantry")
    if l_gantry is not None or v_gantry is not None:
        if l_gantry is None or v_gantry is None:
            rows.append(("gantry", "SKIP", "一侧缺少 gantry"))
        else:
            rows += _cmp_gantry(l_gantry, v_gantry, tol)

    if "safety" in legacy or "safety" in vnext:
        if "safety" not in legacy or "safety" not in vnext:
            rows.append(("safety", "SKIP", "一侧缺少 safety"))
        else:
            rows += _cmp_safety(legacy["safety"], vnext["safety"], tol)
    return rows


# ---------------------------------------------------------------------------
# 报告输出
# ---------------------------------------------------------------------------

def report(rows: List[Tuple[str, str, str]]) -> Tuple[int, int, int, int]:
    counts = {"PASS": 0, "DIFF": 0, "SKIP": 0, "SKIP_OK": 0}
    for name, status, detail in rows:
        counts[status] = counts.get(status, 0) + 1
        if status == "DIFF":
            print(f"  [DIFF] {name}: {detail}")
        elif status == "SKIP":
            print(f"  [SKIP] {name}: {detail}  (证据不完整)")
        # SKIP_OK（无效 role 脏字段忽略）为预期正常跳过，折叠不打印
    if counts["DIFF"] == 0 and counts["SKIP"] == 0:
        print(f"  [PASS] 对拍全部一致（PASS={counts['PASS']} "
              f"SKIP_OK={counts['SKIP_OK']} DIFF=0）")
    return (counts["PASS"], counts["DIFF"], counts["SKIP"], counts["SKIP_OK"])


def exit_code(diffs: int, skips: int, allow_skip: bool) -> int:
    """严格模式退出码：
      - DIFF>0 → 2（存在字段差异）
      - DIFF==0 且 SKIP>0 且非 allow_skip → 3（证据不完整，不得视为通过）
      - 其余 → 0（全部一致且无 SKIP；或 allow_skip 时忽略 SKIP）
    DIFF 优先于 SKIP。"""
    if diffs > 0:
        return 2
    if skips > 0 and not allow_skip:
        return 3
    return 0


def _load(path: str) -> Dict[str, object]:
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        # 兜底：容忍探针旧版手工 printf 产生的尾随逗号（对象/数组闭合前的 `,`）。
        # 合法 JSON 中 `,}` / `,]` 不会出现，因此该清洗对合法输入无副作用。
        cleaned = re.sub(r",(\s*[}\]])", r"\1", text)
        return json.loads(cleaned)


# ---------------------------------------------------------------------------
# 离线自测（无需 PLC）
# ---------------------------------------------------------------------------

def _make_samples() -> Tuple[Dict[str, object], Dict[str, object]]:
    """构造一份最小但覆盖全部比对分支的 legacy / vnext 样本（保持一致）。"""
    roles = [
        {"valid": True, "hmiVisible": True, "plcAxisIndex": 0, "motorNo": 1,
         "axisClass": 0, "unitType": 0, "motionMode": 1},
        {"valid": True, "hmiVisible": False, "plcAxisIndex": 5, "motorNo": 0,
         "axisClass": 2, "unitType": 0, "motionMode": 5},
        {"valid": False, "hmiVisible": False, "plcAxisIndex": -1, "motorNo": 0,
         "axisClass": 0, "unitType": 0, "motionMode": 0},  # 无效 role（脏字段应被 SKIP）
    ]
    legacy = {
        "topology": {
            "head": {"magic": 20269510, "schemaVersion": 1, "revision": 3,
                     "configValid": True, "configErrorCode": 0},
            "groups": [{"index": 0, "valid": True, "hmiVisible": True,
                        "groupCode": 0, "roles": copy.deepcopy(roles)}],
        },
        "axis": [
            {"slot": 0, "absPosition": 150.25, "relPosition": 10.0,
             "motionState": 4, "motionLimit": 0, "alarmWord": 0,
             "manualSpeed": 50.0, "positioningSpeed": 100.0},
            {"slot": 1, "absPosition": 0.0, "relPosition": 0.0,
             "motionState": 1, "motionLimit": 0, "alarmWord": 0,
             "manualSpeed": 0.0, "positioningSpeed": 0.0},
        ],
        "gantry": [
            {"index": 0, "state": 3, "ackSeq": 9, "commandResult": 2,
             "commandErrorCode": 0, "memberControlAllowed": True,
             "logicalControlAllowed": True, "x1InGear": True, "x2InGear": True,
             "fault": False},
        ],
        "safety": {"M224_emergencyStop": False, "M225_release": False},
    }
    vnext = {
        "topology": {
            "header": {"magic": 20269510, "schemaVersion": 1, "revision": 3,
                       "configValid": True, "configErrorCode": 0},
            "groups": [{"index": 0, "valid": True, "groupCode": 0,
                        "roles": [
                            {"valid": True, "plcAxisIndex": 0,
                             "motionMode": 1, "axisClass": 0},
                            {"valid": True, "plcAxisIndex": 5,
                             "motionMode": 5, "axisClass": 2},
                            {"valid": False, "plcAxisIndex": -1, "ignored": True},
                        ]}],
        },
        "runtime": {
            "quality": "Trusted",
            "axes": [
                {"slot": 0, "trusted": True, "absPosition": 150.25,
                 "relPosition": 10.0, "motionState": 4, "motionLimit": 0,
                 "alarmWord": 0, "manualSpeed": 50.0,
                 "positioningSpeed": 100.0},
                {"slot": 1, "trusted": True, "absPosition": 0.0,
                 "relPosition": 0.0, "motionState": 1, "motionLimit": 0,
                 "alarmWord": 0, "manualSpeed": 0.0,
                 "positioningSpeed": 0.0},
            ],
            "gantry": [
                {"index": 0, "trusted": True, "state": 3, "ackSeq": 9,
                 "commandResult": 2, "commandErrorCode": 0,
                 "memberControlAllowed": True, "logicalControlAllowed": True,
                 "x1InGear": True, "x2InGear": True, "fault": False},
            ],
        },
        "safety": {"ok": True, "M224_emergencyStop": False, "M225_release": False},
    }
    return legacy, vnext


def _run_selftest() -> int:
    print("=== plc_shadow_compare 离线自测（无需 PLC） ===\n")
    tol = 0.001

    legacy, vnext = _make_samples()
    rows = compare(legacy, vnext, tol)
    diffs = [r for r in rows if r[1] == "DIFF"]
    assert diffs == [], f"一致样本应无 DIFF，实际: {diffs}"
    skips_ok = [r for r in rows if r[1] == "SKIP_OK"]
    assert any("motionMode/axisClass" in r[0] for r in skips_ok), "无效 role 应 SKIP_OK"
    incompl = [r for r in rows if r[1] == "SKIP"]
    assert incompl == [], f"一致样本不应有证据不完整 SKIP: {incompl}"
    assert exit_code(0, len(incompl), False) == 0
    print(f"  [PASS] 一致样本无 DIFF 且无证据不完整 SKIP；无效 role 脏字段 SKIP_OK "
          f"(共 {len(rows)} 项, exit=0)")

    # 注入差异：浮点 + 整型 + 布尔 + 龙门
    vnext2 = copy.deepcopy(vnext)
    vnext2["runtime"]["axes"][0]["absPosition"] = 999.0
    vnext2["runtime"]["axes"][1]["motionState"] = 5
    vnext2["safety"]["M224_emergencyStop"] = True
    vnext2["runtime"]["gantry"][0]["ackSeq"] = 77
    rows2 = compare(legacy, vnext2, tol)
    keys = {r[0] for r in rows2 if r[1] == "DIFF"}
    assert "axis[0].absPosition" in keys, keys
    assert "axis[1].motionState" in keys, keys
    assert "safety.M224_emergencyStop" in keys, keys
    assert "gantry[0].ackSeq" in keys, keys
    print(f"  [PASS] 注入 4 处差异均被检出 (DIFF={len(keys)})")

    # 容差：浮点差在容差内不报 DIFF
    vnext3 = copy.deepcopy(vnext)
    vnext3["runtime"]["axes"][0]["absPosition"] = 150.2504  # 差 0.0004 < 0.001
    rows3 = compare(legacy, vnext3, tol)
    assert not any(r[1] == "DIFF" for r in rows3), "容差内浮点不应 DIFF"
    print("  [PASS] 浮点容差(0.001)内不误报 DIFF")

    # legacy axis 读取失败 → 证据不完整 SKIP（exit 3，不得视为通过）
    legacy_err = copy.deepcopy(legacy)
    legacy_err["axis"] = {"_error": "块[绝对位置]读取失败: timeout"}
    rows4 = compare(legacy_err, vnext, tol)
    n_incompl = sum(1 for r in rows4 if r[1] == "SKIP")
    assert any(r[0] == "axis" and r[1] == "SKIP" for r in rows4), "legacy 读失败应 SKIP"
    assert exit_code(0, n_incompl, False) == 3, "证据不完整 SKIP → exit 3"
    assert exit_code(0, n_incompl, True) == 0, "--allow-skip 时证据不完整 SKIP → exit 0"
    print("  [PASS] legacy 读取失败时 axis 整块 SKIP（证据不完整）：默认 exit 3，"
          "--allow-skip 归 0")

    # 退出码语义（严格模式：SKIP 不得视为通过）
    assert exit_code(0, 0, False) == 0, "全一致无 SKIP → 0"
    assert exit_code(1, 0, False) == 2, "DIFF → 2"
    assert exit_code(0, 1, False) == 3, "无 DIFF 但 SKIP>0 → 3（证据不完整）"
    assert exit_code(0, 1, True) == 0, "--allow-skip 时 SKIP → 0（调试用）"
    assert exit_code(1, 1, False) == 2, "DIFF 优先于 SKIP"
    print("  [PASS] 退出码语义：SKIP>0 默认 exit 3，--allow-skip 归 0，DIFF 优先")

    print("\n=== 自测全部通过 ===")
    return 0


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def _parse_args() -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="阶段 2：legacy 与 vnext 只读结果逐字段对拍")
    ap.add_argument("--vnext", help="vnext 探针 --json 输出文件（poll=1 单次快照）")
    ap.add_argument("--legacy", help="legacy plc_read_validate.py --json 输出文件")
    ap.add_argument("--tol", type=float, default=0.001,
                    help="浮点字段比较容差（默认 0.001）")
    ap.add_argument("--allow-skip", action="store_true",
                    help="调试用：存在 SKIP 时仍返回 0（默认严格模式 SKIP→3，"
                         "视证据不完整）")
    ap.add_argument("--selftest", action="store_true", help="离线自测（无需 PLC）")
    return ap.parse_args()


def main() -> int:
    args = _parse_args()
    if args.selftest:
        return _run_selftest()
    if not args.vnext or not args.legacy:
        print("错误: 必须同时提供 --vnext 与 --legacy（或用 --selftest）",
              file=sys.stderr)
        return 1
    try:
        vnext = _load(args.vnext)
        legacy = _load(args.legacy)
    except Exception as e:  # noqa: BLE001
        print(f"错误: 读取/解析 JSON 失败: {e}", file=sys.stderr)
        return 1

    rows = compare(legacy, vnext, args.tol)
    print("=== 阶段 2 对拍结果 ===")
    p, d, s, sk = report(rows)
    code = exit_code(d, s, args.allow_skip)
    if code == 3:
        print(f"==== 汇总: PASS={p} DIFF={d} SKIP={s} SKIP_OK={sk} —— 证据不完整（INCOMPLETE） ====")
        print("提示: 存在 SKIP 表示某区域/字段未对拍；不得以此作为阶段 2 通过依据。"
              "修复后重跑，或用 --allow-skip 仅作调试。", file=sys.stderr)
    else:
        print(f"==== 汇总: PASS={p} DIFF={d} SKIP={s} SKIP_OK={sk} ====")
    return code


if __name__ == "__main__":
    sys.exit(main())





