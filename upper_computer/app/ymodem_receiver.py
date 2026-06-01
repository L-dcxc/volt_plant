"""Minimal YMODEM-1K receiver for the plant logger.

Drives a pyserial.Serial that has just been handed off from Modbus to YMODEM
mode by the device. Single-file transfer; no batch.
"""

from __future__ import annotations

import time
from typing import Callable, Optional, Tuple


SOH = 0x01
STX = 0x02
EOT = 0x04
ACK = 0x06
NAK = 0x15
CAN = 0x18
C   = ord("C")
SUB = 0x1A

PROBE_RETRIES = 10            # 'C' polls before giving up on block 0 / data start
NAK_RETRIES = 6               # per-block bad-CRC retries before sending CAN-CAN
BLOCK_HEADER_TIMEOUT = 2.0
PAYLOAD_TIMEOUT = 3.0
PROBE_RESEND_INTERVAL = 1.5   # resend 'C' this often while waiting for header


class YModemError(Exception):
    pass


class YModemCanceled(YModemError):
    pass


def _crc16(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def _read_exact(ser, n: int, deadline: float) -> bytes:
    """Read exactly n bytes, honoring a wall-clock deadline."""
    buf = bytearray()
    while len(buf) < n:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise YModemError(f"读超时 (got {len(buf)}/{n} bytes)")
        ser.timeout = min(remaining, 0.5)
        chunk = ser.read(n - len(buf))
        if chunk:
            buf.extend(chunk)
    return bytes(buf)


def _read_one(ser, timeout: float) -> Optional[int]:
    """Read a single byte within timeout. Returns None on timeout."""
    ser.timeout = timeout
    b = ser.read(1)
    return b[0] if b else None


def _drain_for_eot_or_block(ser, deadline: float) -> int:
    """Wait for the next packet leader: SOH, STX, EOT, or CAN. Skips noise."""
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise YModemError("等待块首字节超时")
        b = _read_one(ser, min(remaining, 0.5))
        if b is None:
            continue
        if b in (SOH, STX, EOT, CAN):
            return b


def _send_cancel(ser) -> None:
    try:
        ser.write(bytes([CAN, CAN, CAN, CAN, CAN, CAN]))
        ser.flush()
    except Exception:
        pass


def receive_file(
    ser,
    out_fp,
    *,
    on_progress: Optional[Callable[[int, int], None]] = None,
    is_canceled: Optional[Callable[[], bool]] = None,
    timeout_first_c: float = 10.0,
) -> Tuple[str, int]:
    """Receive one file. Returns (filename, size). Raises YModemError on failure.

    on_progress(bytes_received, total_size) is called after each data block.
    is_canceled() is polled before each ACK; if it returns True, sends CAN-CAN
    and raises YModemCanceled.
    """
    expected_seq = 0
    bytes_received = 0
    total_size = 0
    filename = ""
    saw_header = False

    # 1) Probe with 'C' until we get block 0 (or run out of attempts).
    for attempt in range(PROBE_RETRIES):
        if is_canceled and is_canceled():
            _send_cancel(ser)
            raise YModemCanceled("用户取消")

        ser.write(bytes([C]))
        ser.flush()
        leader_deadline = time.monotonic() + (
            timeout_first_c if attempt == 0 else PROBE_RESEND_INTERVAL
        )
        try:
            leader = _drain_for_eot_or_block(ser, leader_deadline)
        except YModemError:
            continue

        if leader == CAN:
            # second CAN must arrive promptly to confirm cancel
            b2 = _read_one(ser, 0.5)
            if b2 == CAN:
                raise YModemError("发送端取消传输")
            continue

        if leader in (SOH, STX):
            block_size = 128 if leader == SOH else 1024
            try:
                rest = _read_exact(ser, block_size + 4, time.monotonic() + PAYLOAD_TIMEOUT)
            except YModemError:
                ser.write(bytes([NAK]))
                ser.flush()
                continue

            seq = rest[0]
            seq_inv = rest[1]
            data = rest[2:2 + block_size]
            crc_recv = (rest[2 + block_size] << 8) | rest[3 + block_size]
            crc_calc = _crc16(data)

            if seq != 0 or seq_inv != 0xFF or crc_recv != crc_calc:
                ser.write(bytes([NAK]))
                ser.flush()
                continue

            # Parse block 0: filename\0size_ascii\0...
            nul = data.find(b"\x00")
            if nul < 0:
                ser.write(bytes([NAK]))
                ser.flush()
                continue
            filename = data[:nul].decode("ascii", errors="replace")
            tail = data[nul + 1:]
            size_end = tail.find(b"\x00")
            if size_end < 0:
                size_end = len(tail)
            try:
                total_size = int(tail[:size_end].decode("ascii").strip() or "0")
            except ValueError:
                total_size = 0

            ser.write(bytes([ACK]))
            ser.flush()
            saw_header = True
            break

        # leader == EOT here means sender skipped block 0; not supported
        ser.write(bytes([NAK]))
        ser.flush()

    if not saw_header:
        _send_cancel(ser)
        raise YModemError("未收到 YMODEM 头块")

    if on_progress:
        on_progress(0, total_size)

    expected_seq = 1
    nak_count = 0

    # 2) Trigger data with another 'C', then loop until EOT.
    ser.write(bytes([C]))
    ser.flush()

    while True:
        if is_canceled and is_canceled():
            _send_cancel(ser)
            raise YModemCanceled("用户取消")

        leader = _drain_for_eot_or_block(ser, time.monotonic() + BLOCK_HEADER_TIMEOUT + 8.0)

        if leader == EOT:
            # End of file. Standard YMODEM dance: NAK -> EOT -> ACK -> C -> empty block 0 -> ACK
            ser.write(bytes([NAK]))
            ser.flush()
            b = _read_one(ser, BLOCK_HEADER_TIMEOUT)
            if b == EOT:
                ser.write(bytes([ACK]))
                ser.flush()
            ser.write(bytes([C]))
            ser.flush()
            # Optional trailing null header (some senders skip it).
            tail_leader = _read_one(ser, BLOCK_HEADER_TIMEOUT)
            if tail_leader in (SOH, STX):
                block_size = 128 if tail_leader == SOH else 1024
                try:
                    _read_exact(ser, block_size + 4, time.monotonic() + PAYLOAD_TIMEOUT)
                    ser.write(bytes([ACK]))
                    ser.flush()
                except YModemError:
                    pass
            return filename, bytes_received

        if leader == CAN:
            b2 = _read_one(ser, 0.5)
            if b2 == CAN:
                raise YModemError("发送端取消传输")
            continue

        # SOH or STX: data block
        block_size = 128 if leader == SOH else 1024
        try:
            rest = _read_exact(ser, block_size + 4, time.monotonic() + PAYLOAD_TIMEOUT)
        except YModemError:
            ser.write(bytes([NAK]))
            ser.flush()
            nak_count += 1
            if nak_count >= NAK_RETRIES:
                _send_cancel(ser)
                raise YModemError("连续 NAK 超过上限")
            continue

        seq = rest[0]
        seq_inv = rest[1]
        data = rest[2:2 + block_size]
        crc_recv = (rest[2 + block_size] << 8) | rest[3 + block_size]
        crc_calc = _crc16(data)

        if seq_inv != (0xFF ^ seq) or crc_recv != crc_calc:
            ser.write(bytes([NAK]))
            ser.flush()
            nak_count += 1
            if nak_count >= NAK_RETRIES:
                _send_cancel(ser)
                raise YModemError("连续 NAK 超过上限")
            continue

        if seq == ((expected_seq - 1) & 0xFF):
            # Duplicate (sender retried). Ack but don't write again.
            ser.write(bytes([ACK]))
            ser.flush()
            continue

        if seq != (expected_seq & 0xFF):
            ser.write(bytes([NAK]))
            ser.flush()
            nak_count += 1
            if nak_count >= NAK_RETRIES:
                _send_cancel(ser)
                raise YModemError(f"块序号错位 (got {seq}, want {expected_seq & 0xFF})")
            continue

        # Honor file size: trim trailing CP/M pad on the last block.
        remaining = total_size - bytes_received if total_size > 0 else len(data)
        writable = min(remaining, len(data)) if total_size > 0 else len(data)
        if writable > 0:
            out_fp.write(data[:writable])
            bytes_received += writable

        ser.write(bytes([ACK]))
        ser.flush()
        nak_count = 0
        expected_seq = (expected_seq + 1) & 0xFF

        if on_progress:
            on_progress(bytes_received, total_size)
