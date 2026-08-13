#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
plc_shadow_run.py —— 阶段 2 现场一键签收（真机验证 + 对拍 + 观察窗口）
==========================================================================
把阶段 2 正式签收的三项现场证据串成一条命令，自动逐项执行并汇总：
  A. 真机只读验证（探针 --poll 1）：safety.ok、M224/M225、退出码 0
  B. legacy 采集（plc_read_validate.py --json）
  C. legacy/vnext 逐字段对拍（plc_shadow_compare.py，需 exit 0 无 DIFF/SKIP）
  D. 连续观察窗口（探针 --poll N --interval M，plc_shadow_watch.py 签收判定）

用法（从仓库根目录）：
  python tools/plc_shadow_run.py --host 192.168.1.88 --port 502 --unit 1 \
      --poll 2000 --interval 200 --outdir shadow_evidence
离线验证脚本本身（无需 PLC，用预置样本走通全流程）：
  python tools/plc_shadow_run.py --dry-run --outdir shadow_evidence_dry

退出码：0 = A/B/C/D 全部通过（阶段 2 可正式签收）
        1 = 任一环节未通过（详细报告见 --outdir/run_report.txt）
        2 = 参数/环境错误（探针或子工具缺失）
任一环节失败即停止后续环节（避免在无 PLC 时白跑观察窗口）。
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from typing import Dict, Optional, Tuple

# 脚本位于 tools/，仓库根为上一级
TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS_DIR)
DEFAULT_PROBE = os.path.join(ROOT, "build", "plc_vnext_readonly_probe.exe")


def _tool(name: str) -> str:
    return os.path.join(TOOLS_DIR, name)


def run(cmd, stdout=None, stderr=None):
    """执行外部命令。
    stdout/stderr 传文件句柄（二进制）时重定向写文件；
    否则捕获输出返回 CompletedProcess（r.returncode / r.stdout / r.stderr）。"""
    if stdout is not None or stderr is not None:
        # 重定向到文件：不能与 capture_output 同时使用
        return subprocess.run(cmd, stdout=stdout, stderr=stderr)
    return subprocess.run(cmd, capture_output=True, text=True,
                          encoding="utf-8", errors="replace")


def load_json(path: str) -> Optional[Dict]:
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:  # noqa: BLE001
        return None


class StepResult:
    def __init__(self, name: str):
        self.name = name
        self.passed = False
        self.detail: list[str] = []

    def ok(self, *detail: str) -> None:
        self.passed = True
        self.detail.extend(detail)

    def fail(self, *detail: str) -> None:
        self.passed = False
        self.detail.extend(detail)


def _probe_args(args, poll: int, interval: int):
    return [args.probe, "--host", args.host, "--port", str(args.port),
            "--unit", str(args.unit), "--poll", str(poll),
            "--interval", str(interval), "--json"]


def run_steps(args, outdir: str):
    """执行 A→D 四环节；任一失败即停止，返回已执行步骤列表。"""
    if args.dry_run:
        return _dry_run_steps(outdir, args.tol)

    steps = []

    # A. 真机只读验证（单快照，poll=1 产生合法 JSON）
    a = StepResult("A. 真机只读验证(readSafety/poll=1)")
    vnext_path = os.path.join(outdir, "vnext.json")
    verify_err = os.path.join(outdir, "verify_summary.txt")
    with open(vnext_path, "wb") as f, \
         open(verify_err, "wb") as fe:
        r = run(_probe_args(args, 1, 500), stdout=f, stderr=fe)
    if r.returncode != 0:
        a.fail(f"探针退出码 {r.returncode}（应 0）。详见 {verify_err}")
    else:
        data = load_json(vnext_path)
        if data is None:
            a.fail("vnext.json 无法解析")
        else:
            safety = data.get("safety") or {}
            if not safety.get("ok"):
                a.fail("safety.ok=false（急停状态未知）")
            else:
                a.ok(f"探针退出码 0；safety.ok=true；M224={safety.get('M224_emergencyStop')} "
                     f"M225={safety.get('M225_release')}")
    steps.append(a)
    if not a.passed:
        return steps

    # B. legacy 采集
    b = StepResult("B. legacy 采集")
    legacy_path = os.path.join(outdir, "legacy.json")
    with open(legacy_path, "wb") as f:
        r = run([sys.executable, _tool("plc_read_validate.py"),
                 "--host", args.host, "--port", str(args.port),
                 "--unit", str(args.unit), "--json"], stdout=f)
    if r.returncode != 0 or load_json(legacy_path) is None:
        b.fail(f"legacy 采集失败，退出码 {r.returncode}")
    else:
        b.ok("legacy.json 已生成且可解析")
    steps.append(b)
    if not b.passed:
        return steps

    # C. legacy/vnext 逐字段对拍
    c = StepResult("C. legacy/vnext 逐字段对拍")
    r = run([sys.executable, _tool("plc_shadow_compare.py"),
             "--vnext", vnext_path, "--legacy", legacy_path,
             "--tol", str(args.tol)])
    if r.returncode != 0:
        c.fail(f"对拍退出码 {r.returncode}（应 0，无 DIFF/SKIP）\n{r.stdout}")
    else:
        c.ok("对拍一致（exit 0）")
    steps.append(c)
    if not c.passed:
        return steps

    # D. 连续观察窗口
    d = StepResult("D. 连续观察窗口")
    shadow_path = os.path.join(outdir, "shadow.json")
    summary_path = os.path.join(outdir, "summary.txt")
    with open(shadow_path, "wb") as f, \
         open(summary_path, "wb") as fe:
        r = run(_probe_args(args, args.poll, args.interval), stdout=f, stderr=fe)
    if r.returncode != 0:
        d.fail(f"观察窗口探针退出码 {r.returncode}（应 0）")
    else:
        w = run([sys.executable, _tool("plc_shadow_watch.py"),
                 "--summary", summary_path])
        if w.returncode != 0:
            d.fail(f"观察窗口签收判定退出码 {w.returncode}\n{w.stdout}")
        else:
            d.ok("观察窗口达标（runtime/safety/topo 全成功、无断线）")
    steps.append(d)
    return steps


def _dry_run_steps(outdir: str, tol: float):
    """离线用预置样本走通全流程（验证脚本编排，不连 PLC）。"""
    sys.path.insert(0, TOOLS_DIR)
    import plc_shadow_compare as cmp
    legacy_s, vnext_s = cmp._make_samples()
    vnext_path = os.path.join(outdir, "vnext.json")
    legacy_path = os.path.join(outdir, "legacy.json")
    summary_path = os.path.join(outdir, "summary.txt")
    for path, data in ((vnext_path, vnext_s), (legacy_path, legacy_s)):
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False)
    with open(summary_path, "w", encoding="utf-8") as f:
        f.write("#summary polls=2000 topo(ok=2000 fail=0) runtime(trusted=2000 "
                "notTrusted=0) safety(ok=2000 fail=0) disconnected=0 durationMin=1 "
                "durationAvg=2 durationMax=5 durationN=2000\n")

    a = StepResult("A.(dry) 真机只读验证")
    a.ok("dry-run：预置样本，safety.ok=true")
    b = StepResult("B.(dry) legacy 采集")
    b.ok("dry-run：legacy.json 预置")
    c = StepResult("C. legacy/vnext 逐字段对拍")
    r = run([sys.executable, _tool("plc_shadow_compare.py"),
             "--vnext", vnext_path, "--legacy", legacy_path, "--tol", str(tol)])
    if r.returncode != 0:
        c.fail(f"对拍退出码 {r.returncode}\n{r.stdout}")
    else:
        c.ok("对拍一致（exit 0）")
    d = StepResult("D.(dry) 连续观察窗口")
    w = run([sys.executable, _tool("plc_shadow_watch.py"), "--summary", summary_path])
    if w.returncode != 0:
        d.fail(f"观察窗口判定退出码 {w.returncode}\n{w.stdout}")
    else:
        d.ok("观察窗口达标（dry）")
    return [a, b, c, d]


def main() -> int:
    ap = argparse.ArgumentParser(description="阶段 2 现场一键签收")
    ap.add_argument("--host", default="192.168.1.88", help="PLC IP")
    ap.add_argument("--port", type=int, default=502)
    ap.add_argument("--unit", type=int, default=1)
    ap.add_argument("--poll", type=int, default=2000, help="观察窗口轮询次数")
    ap.add_argument("--interval", type=int, default=200, help="观察窗口轮询间隔 ms")
    ap.add_argument("--tol", type=float, default=0.001, help="对拍浮点容差")
    ap.add_argument("--probe", default=DEFAULT_PROBE, help="探针 exe 路径")
    ap.add_argument("--outdir", default="shadow_evidence", help="证据输出目录")
    ap.add_argument("--dry-run", action="store_true",
                    help="离线用预置样本走通全流程（不连 PLC）")
    args = ap.parse_args()

    if not args.dry_run and not os.path.isfile(args.probe):
        print(f"错误: 探针不存在: {args.probe}", file=sys.stderr)
        return 2
    for t in ("plc_read_validate.py", "plc_shadow_compare.py", "plc_shadow_watch.py"):
        if not os.path.isfile(_tool(t)):
            print(f"错误: 子工具缺失: {_tool(t)}", file=sys.stderr)
            return 2
    try:
        os.makedirs(args.outdir, exist_ok=True)
    except OSError as e:
        print(f"错误: 无法创建输出目录: {e}", file=sys.stderr)
        return 2

    mode = "（dry-run 离线）" if args.dry_run else f"（{args.host}:{args.port} unit={args.unit}）"
    print(f"=== 阶段 2 现场一键签收 {mode} ===")
    if not args.dry_run:
        print(f"观察窗口: --poll {args.poll} --interval {args.interval} "
              f"(约 {args.poll * args.interval / 1000.0:.0f}s)\n")
    else:
        print()

    steps = run_steps(args, args.outdir)
    lines = []
    all_ok = True
    for s in steps:
        mark = "PASS" if s.passed else "FAIL"
        lines.append(f"[{mark}] {s.name}")
        for dline in s.detail:
            lines.append(f"        {dline}")
        all_ok = all_ok and s.passed
        print(f"  [{mark}] {s.name}")
        for dline in s.detail:
            print(f"      {dline}")

    report_path = os.path.join(args.outdir, "run_report.txt")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")

    if all_ok:
        print("\n==== 结果: 全部通过。阶段 2 可正式签收。"
              f"证据见 {os.path.abspath(args.outdir)} ====")
        return 0
    print("\n==== 结果: 存在未通过环节，阶段 2 不可签收。"
          f"详见 {os.path.abspath(report_path)} ====")
    return 1


if __name__ == "__main__":
    sys.exit(main())


