#!/usr/bin/env python3
"""LD2451 串口数据流实测工具（需要 pyserial，仅在诊断/采集时使用，不参与 C++ 构建）。

用法：
  # 被动采集真实字节流并统计帧率、帧长、分片、垃圾字节
  python3 tests/serial_probe.py --port /dev/ttyACM0 --baud 460800 --seconds 5 --dump sample.bin

  # 扫描协议支持的全部波特率，找出模块当前工作波特率（不匹配时是纯乱码）
  python3 tests/serial_probe.py --scan

  # 测量握手 ACK 往返时延、丢 ACK 概率与快速重发策略的成功率/耗时
  python3 tests/serial_probe.py --rtt --rounds 10

注意：--rtt 会发送「使能配置 / 查询 / 结束配置」，属只读查询，不改模块参数。
"""
import argparse
import collections
import json
import statistics
import time

try:
    import serial
except ImportError:
    raise SystemExit("需要 pyserial：python3 -m pip install pyserial")

DATA_HEAD = bytes.fromhex("F4F3F2F1")
DATA_TAIL = bytes.fromhex("F8F7F6F5")
CMD_HEAD = bytes.fromhex("FDFCFBFA")
CMD_TAIL = bytes.fromhex("04030201")
BAUDS = (9600, 19200, 38400, 57600, 115200, 230400, 256000, 460800)

ENABLE = bytes.fromhex("FDFCFBFA0400FF00010004030201")
VERSION = bytes.fromhex("FDFCFBFA0200A00004030201")
PARAMS = bytes.fromhex("FDFCFBFA0200120004030201")
SENS = bytes.fromhex("FDFCFBFA0200130004030201")
END = bytes.fromhex("FDFCFBFA0200FE0004030201")
FLOW = (("enable", ENABLE, 0x01FF), ("version", VERSION, 0x01A0),
        ("params", PARAMS, 0x0112), ("sens", SENS, 0x0113),
        ("end", END, 0x01FE))


def open_port(port, baud):
    return serial.Serial(port=port, baudrate=baud, bytesize=8, parity="N",
                         stopbits=1, timeout=0.005)


def parse_frames(buf):
    """切出所有完整帧，返回 (帧列表, 已消费字节数)。兼容长度字段 ±4 笔误。"""
    out, i = [], 0
    while i + 6 <= len(buf):
        di, ci = buf.find(DATA_HEAD, i), buf.find(CMD_HEAD, i)
        cand = [(p, "data") for p in [di] if p >= 0] + [(p, "cmd") for p in [ci] if p >= 0]
        if not cand:
            break
        pos, kind = min(cand)
        if pos > i:
            i = pos
        tail = DATA_TAIL if kind == "data" else CMD_TAIL
        declared = buf[pos + 4] | (buf[pos + 5] << 8)
        total = 10 + declared
        if pos + total > len(buf) or buf[pos + total - 4:pos + total] != tail:
            found = None
            for delta in list(range(-4, 0)) + list(range(1, 5)):
                c = total + delta
                if c >= 10 and pos + c <= len(buf) and buf[pos + c - 4:pos + c] == tail:
                    found = c
                    break
            if found is None:
                i = pos + 1
                continue
            total = found
        raw = bytes(buf[pos:pos + total])
        ack = raw[6] | (raw[7] << 8) if kind == "cmd" and len(raw) >= 8 else None
        out.append({"kind": kind, "t": None, "declared": declared,
                    "total": total, "ack": ack, "raw": raw})
        i = pos + total
    return out, i


def capture(port, baud, seconds):
    s = open_port(port, baud)
    s.reset_input_buffer()
    chunks, t0, first = [], time.monotonic(), None
    while time.monotonic() - t0 < seconds:
        data = s.read(65536)
        now = time.monotonic() - t0
        if data:
            if first is None:
                first = now
            chunks.append((now, data))
    elapsed = time.monotonic() - t0
    s.close()
    return chunks, elapsed, first


def analyze(chunks, elapsed, first, port, baud, dump=None):
    stream = bytearray()
    stamps = []
    for t, data in chunks:
        stream.extend(data)
        stamps.extend([t] * len(data))
    if dump:
        with open(dump, "wb") as f:
            f.write(stream)

    frames = []
    garbage, i = 0, 0
    while i + 6 <= len(stream):
        di = stream.find(DATA_HEAD, i)
        ci = stream.find(CMD_HEAD, i)
        cand = [(p, "data") for p in [di] if p >= 0] + [(p, "cmd") for p in [ci] if p >= 0]
        if not cand:
            garbage += len(stream) - i
            break
        pos, kind = min(cand)
        garbage += pos - i
        tail = DATA_TAIL if kind == "data" else CMD_TAIL
        declared = stream[pos + 4] | (stream[pos + 5] << 8)
        total = 10 + declared
        if pos + total > len(stream) or stream[pos + total - 4:pos + total] != tail:
            found = None
            for delta in list(range(-4, 0)) + list(range(1, 5)):
                c = total + delta
                if c >= 10 and pos + c <= len(stream) and stream[pos + c - 4:pos + c] == tail:
                    found = c
                    break
            if found is None:
                garbage += 1
                i = pos + 1
                continue
            total = found
        frames.append({"kind": kind, "declared": declared, "total": total,
                       "t": stamps[pos], "raw": bytes(stream[pos:pos + total])})
        i = pos + total
    garbage += max(0, len(stream) - i)

    intervals = [frames[k]["t"] - frames[k - 1]["t"] for k in range(1, len(frames))]
    sizes = [len(c[1]) for c in chunks]
    targets = collections.Counter()
    for f in frames:
        if f["kind"] == "data" and f["declared"] >= 2:
            targets[f["raw"][6]] += 1
    return {
        "port": port, "baud": baud, "elapsed_s": round(elapsed, 3),
        "bytes": len(stream), "byte_rate_Bps": round(len(stream) / elapsed, 1),
        "first_byte_s": round(first, 4) if first is not None else None,
        "read_chunks": len(chunks),
        "chunk_size_min": min(sizes) if sizes else 0,
        "chunk_size_max": max(sizes) if sizes else 0,
        "chunk_size_mean": round(statistics.mean(sizes), 1) if sizes else 0,
        "frames": len(frames), "frame_rate_fps": round(len(frames) / elapsed, 2),
        "frame_kinds": dict(collections.Counter(f["kind"] for f in frames)),
        "frame_lengths": dict(sorted(collections.Counter(f["total"] for f in frames).items())),
        "declared_lengths": dict(sorted(collections.Counter(f["declared"] for f in frames).items())),
        "target_count_hist": dict(sorted(targets.items())),
        "garbage_bytes": garbage,
        "interval_ms": {
            "min": round(min(intervals) * 1000, 2) if intervals else None,
            "mean": round(statistics.mean(intervals) * 1000, 2) if intervals else None,
            "p95": round(sorted(intervals)[int(len(intervals) * .95)] * 1000, 2)
            if intervals else None,
            "max": round(max(intervals) * 1000, 2) if intervals else None,
        },
        "ack_hex_sample": [f["raw"].hex(" ") for f in frames if f["kind"] == "cmd"][:5],
    }


def rtt_round(s, retry, timeout, gap):
    """完整握手一轮，单条命令超时即重发；返回每条命令的尝试次数与总耗时。"""
    buf, steps, t_start = bytearray(), [], time.monotonic()
    for name, frame, want in FLOW:
        attempts, got = 0, False
        for _ in range(retry):
            attempts += 1
            deadline = time.monotonic() + timeout
            sent = time.monotonic()
            s.write(frame)
            while time.monotonic() < deadline:
                data = s.read(4096)
                if not data:
                    continue
                buf.extend(data)
                frames, used = parse_frames(buf)
                del buf[:used]
                if any(f["ack"] == want for f in frames):
                    got = True
                    break
            if got:
                break
            time.sleep(gap)
        steps.append({"cmd": name, "attempts": attempts, "ok": got,
                      "last_rtt_ms": round((time.monotonic() - sent) * 1000, 1)})
        if not got:
            break
    s.write(END)  # 无论成功与否都尝试退出配置模式
    time.sleep(0.08)
    return {"ok": all(x["ok"] for x in steps), "steps": steps,
            "elapsed_ms": round((time.monotonic() - t_start) * 1000, 1)}


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=460800)
    ap.add_argument("--seconds", type=float, default=3.0)
    ap.add_argument("--dump", default="", help="把原始字节流保存为二进制文件")
    ap.add_argument("--scan", action="store_true", help="扫描全部候选波特率")
    ap.add_argument("--rtt", action="store_true", help="测量握手 ACK 时延与重发成功率")
    ap.add_argument("--rounds", type=int, default=10)
    ap.add_argument("--retry", type=int, default=3, help="单条命令最大发送次数")
    ap.add_argument("--timeout", type=float, default=0.18, help="单条 ACK 超时（秒）")
    ap.add_argument("--gap", type=float, default=0.06, help="重发间隔（秒）")
    args = ap.parse_args()

    if args.scan:
        for b in BAUDS:
            chunks, elapsed, first = capture(args.port, b, max(1.0, args.seconds))
            st = analyze(chunks, elapsed, first, args.port, b)
            print(json.dumps({k: st[k] for k in ("baud", "bytes", "frames",
                                                 "frame_kinds", "garbage_bytes")},
                             ensure_ascii=False), flush=True)
        return

    if args.rtt:
        results = []
        for i in range(args.rounds):
            s = open_port(args.port, args.baud)
            r = rtt_round(s, args.retry, args.timeout, args.gap)
            s.close()
            results.append(r)
            print(json.dumps({"round": i, "ok": r["ok"], "elapsed_ms": r["elapsed_ms"],
                              "attempts": [x["attempts"] for x in r["steps"]]},
                             ensure_ascii=False), flush=True)
            time.sleep(0.25)
        print(json.dumps({"success": sum(r["ok"] for r in results), "rounds": args.rounds,
                          "mean_elapsed_ms": round(statistics.mean(
                              r["elapsed_ms"] for r in results), 1),
                          "total_retries": sum(sum(x["attempts"] - 1 for x in r["steps"])
                                               for r in results)},
                         ensure_ascii=False))
        return

    chunks, elapsed, first = capture(args.port, args.baud, args.seconds)
    print(json.dumps(analyze(chunks, elapsed, first, args.port, args.baud, args.dump or None),
                     ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
