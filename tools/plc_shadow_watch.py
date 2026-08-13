#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plc_shadow_watch.py —— 阶段 2：真实只读影子运行·连续观察窗口签收判定
==========================================================================
依据《servoV6剩余迁移工作实施方案》§10.2，在完成连续观察窗口后判定阶段 2
是否可正式签收。输入是探针 plc_vnext_readonly_probe 的观察窗口汇总输出
（--json 时 stderr 的 `#summary` 行；非 json 时 stdout 的 `[summary]` 行）。

用法：
  1) 观察窗口（--json：stdout 存 shadow.json，stderr 汇总进 summary.txt）
     build/plc_vnext_readonly_probe.exe --host 192.168.1.88 --port 502 --unit 1 ^
       --poll 2000 --interval 200 --json > shadow.json 2> summary.txt
  2) 签收判定
     python tools/plc_shadow_watch.py --summary summary.txt
  或直接传探针观察日志：
     python tools/plc_shadow_watch.py --summary watch.log   # 非 json 模式输出

签收标准（全部满足才 PASS，对应 §10.2 通过标准）：
  - polls > 0（确有轮询发生）
  - runtimeNotTrusted == 0（runtime 持续 Trusted，无 Partial/TransportFailed）
  - safetyFail == 0（急停读取全部成功）
  - topoFail == 0（拓扑读取全部成功）
  - disconnected == 0（无断线）
程序正常退出由探针退出码判定；本工具只针对汇总统计。

退出码：
  0 = 观察窗口通过（可正式签收）
  1 = 存在未达标项（未通过）
  2 = 参数/汇总解析错误

离线自测（无需 PLC）：python tools/plc_shadow_watch.py --selftest
"""
from __future__ import annotations

import argparse
import re
import sys
from typing import Dict, List, Optional, Tuple


# ---------------------------------------------------------------------------
# 解析 probe 观察窗口汇总
# ---------------------------------------------------------------------------

# 匹配 #summary 或 [summary] 主行；duration 字段仅在 --json 时出现（可选）。
SUMMARY_RE = re.compile(
    r"^(?:#summary|\[summary\])\s+polls=(\d+)\s+"
    r"topo\(ok=(\d+)\s+fail=(\d+)\)\s+"
    r"runtime\(trusted=(\d+)\s+notTrusted=(\d+)\)\s+"
    r"safety\(ok=(\d+)\s+fail=(\d+)\)\s+"
    r"disconnected=(\d+)"
    r"(?:\s+durationMin=(\d+)\s+durationAvg=(\d+)\s+"
    r"durationMax=(\d+)\s+durationN=(\d+))?",
    re.MULTILINE,
)


def parse_summary(text: str) -> Optional[Dict[str, int]]:
    """从观察窗口日志/汇总文本中提取最后一个 summary 行。"""
    best: Optional[Dict[str, int]] = None
    for m in SUMMARY_RE.finditer(text):
        best = {
            "polls": int(m.group(1)),
            "topoOk": int(m.group(2)),
            "topoFail": int(m.group(3)),
            "runtimeTrusted": int(m.group(4)),
            "runtimeNotTrusted": int(m.group(5)),
            "safetyOk": int(m.group(6)),
            "safetyFail": int(m.group(7)),
            "disconnected": int(m.group(8)),
            "durationMin": int(m.group(9)) if m.group(9) else None,
            "durationAvg": int(m.group(10)) if m.group(10) else None,
            "durationMax": int(m.group(11)) if m.group(11) else None,
            "durationN": int(m.group(12)) if m.group(12) else None,
        }
    return best


# ---------------------------------------------------------------------------
# 签收判定
# ---------------------------------------------------------------------------

def judge(stats: Dict[str, int]) -> List[str]:
    """按 §10.2 签收标准返回未达标原因列表；空列表表示通过。"""
    problems: List[str] = []
    if stats["polls"] <= 0:
        problems.append("polls=0：观察窗口无有效轮询")
    if stats["topoFail"] > 0:
        problems.append(f"拓扑读取失败 topoFail={stats['topoFail']} 次")
    if stats["runtimeNotTrusted"] > 0:
        problems.append(
            f"runtime 不可信 runtimeNotTrusted={stats['runtimeNotTrusted']} 次"
            "（含 Partial/TransportFailed，不得视为通过）")
    if stats["safetyFail"] > 0:
        problems.append(f"急停读取失败 safetyFail={stats['safetyFail']} 次")
    if stats["disconnected"] > 0:
        problems.append(f"断连 disconnected={stats['disconnected']} 次")
    return problems


# ---------------------------------------------------------------------------
# 报告与主流程
# ---------------------------------------------------------------------------

def report(stats: Dict[str, int]) -> List[str]:
    problems = judge(stats)
    print("=== 阶段 2 观察窗口签收判定 ===")
    print(f"  polls={stats['polls']} topo(ok={stats['topoOk']} fail={stats['topoFail']}) "
          f"runtime(trusted={stats['runtimeTrusted']} notTrusted={stats['runtimeNotTrusted']}) "
          f"safety(ok={stats['safetyOk']} fail={stats['safetyFail']}) "
          f"disconnected={stats['disconnected']}")
    if stats["durationN"]:
        print(f"  runtime durationMs: min={stats['durationMin']} avg={stats['durationAvg']} "
              f"max={stats['durationMax']} (n={stats['durationN']})")
    if problems:
        print("  [FAIL] 观察窗口未达标：")
        for p in problems:
            print(f"    - {p}")
    else:
        print("  [PASS] 观察窗口达标：runtime/safety/topo 全部成功、无断线、"
              "无 Partial/TransportFailed。可正式签收阶段 2。")
    return problems


# ---------------------------------------------------------------------------
# 离线自测
# ---------------------------------------------------------------------------

def _run_selftest() -> int:
    print("=== plc_shadow_watch 离线自测（无需 PLC） ===\n")

    ok = (
        "#summary polls=2000 topo(ok=2000 fail=0) runtime(trusted=2000 notTrusted=0) "
        "safety(ok=2000 fail=0) disconnected=0 durationMin=1 durationAvg=2 "
        "durationMax=5 durationN=2000\n"
    )
    st = parse_summary(ok)
    assert st is not None, "应解析出 summary"
    assert st["polls"] == 2000 and st["runtimeNotTrusted"] == 0 and st["safetyFail"] == 0
    assert judge(st) == [], f"合格样本不应有问题: {judge(st)}"
    print("  [PASS] 合格样本判定通过（runtime/safety/topo 全成功、无断线）")

    txt = (
        "[summary] polls=100 topo(ok=99 fail=1) runtime(trusted=100 notTrusted=0) "
        "safety(ok=100 fail=0) disconnected=0\n"
    )
    st2 = parse_summary(txt)
    assert st2 is not None and st2["durationN"] is None
    assert "拓扑读取失败" in judge(st2)[0]
    print("  [PASS] 非 json 分支解析正常；topoFail=1 被检出")

    bad = (
        "#summary polls=2000 topo(ok=2000 fail=0) runtime(trusted=1998 notTrusted=2) "
        "safety(ok=1999 fail=1) disconnected=3 durationMin=1 durationAvg=2 "
        "durationMax=5 durationN=2000\n"
    )
    st3 = parse_summary(bad)
    ps = judge(st3)
    assert any("runtime 不可信" in p for p in ps), ps
    assert any("safetyFail" in p for p in ps), ps
    assert any("disconnected" in p for p in ps), ps
    assert len(ps) == 3, ps
    print("  [PASS] 缺陷样本检出 runtime 不可信 / safety 失败 / 断连")

    assert parse_summary("some random log\nwithout summary") is None
    print("  [PASS] 无 summary 行时返回 None（判定为解析失败）")

    print("\n=== 自测全部通过 ===")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="阶段 2 观察窗口签收判定")
    ap.add_argument("--summary", help="探针观察窗口汇总/日志文件")
    ap.add_argument("--selftest", action="store_true", help="离线自测（无需 PLC）")
    args = ap.parse_args()
    if args.selftest:
        return _run_selftest()
    if not args.summary:
        print("错误: 必须提供 --summary（或 --selftest）", file=sys.stderr)
        return 2
    try:
        with open(args.summary, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as e:
        print(f"错误: 读取文件失败: {e}", file=sys.stderr)
        return 2

    stats = parse_summary(text)
    if stats is None:
        print(f"错误: 在 {args.summary} 中未找到 probe 汇总行"
              "（#summary 或 [summary]）", file=sys.stderr)
        return 2
    problems = report(stats)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())


