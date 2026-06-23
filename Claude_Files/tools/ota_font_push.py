#!/usr/bin/env python3
"""ESP8266 OTA Font Push Tool -- Phase A: 4KB (95 ASCII + 76 CJK)

Parses TFT_Font_Data.h, builds a binary matching the W25Q128 layout written by
App_Storage_Burn_Font_From_SRAM(), then pushes it page-by-page over TCP to the
ESP8266 WiFiServer.  Zero pip dependencies -- Python 3.6+ standard library only.

Usage:
    python ota_font_push.py --ip <ESP_IP> [--font-data path/to/TFT_Font_Data.h] [--port 8266]
"""

import argparse
import base64
import os
import re
import socket
import struct
import sys
import time


# ---------------------------------------------------------------------------
#  Binary-format constants (must match W25Q_Driver.h / App_Storage.c)
# ---------------------------------------------------------------------------
FONT_MAGIC        = 0x574B        # "WK"
FONT_VERSION      = 1
ASCII_BASE        = 0x0020        # offset of first ASCII glyph (space)
ASCII_SIZE         = 1520          # 95 * 16
CJK_BASE           = 0x0700        # offset of first CJK glyph
CJK_BASE_UNICODE   = 0x4E00        # "一"
CJK_COUNT          = 20902          # range capacity  U+4E00..U+9FFF
CJK_GLYPH_BYTES    = 32            # 16x16 LSB-first
PAGE_SIZE          = 256

HEADER_SIZE        = 32
CRC32_OFFSET       = 4             # uint32 at +4, zero until finalised


# ---------------------------------------------------------------------------
#  parser helpers
# ---------------------------------------------------------------------------

_HEX_BYTE_RE = re.compile(r'0[xX]([0-9A-Fa-f]{2})')

def _hex_tokens_to_bytes(blob):
    """Convert a brace-enclosed hex list (e.g. ``{0x00,0x01,...}``) to bytes."""
    return bytes(int(m.group(1), 16) for m in _HEX_BYTE_RE.finditer(blob))


def _extract_c_array(text, name):
    """Extract ``static const uint8_t NAME[][N] = {{...},{...},...};``.

    Returns ``(element_size, [bytes,...])`` where *element_size* is the
    number of bytes per inner brace group and the list length equals the
    number of glyphs.
    """
    # match the opening line and capture the element size N
    head_pat = re.escape(name) + r'\[(?:\w+)?\]\[(\d+)\]\s*=\s*\{'
    m = re.search(head_pat, text)
    if m is None:
        raise ValueError(f"array '{name}' not found")
    elem_size = int(m.group(1))

    # find the closing ``};`` that belongs to this initialiser
    pos = m.end()
    brace_depth = 0
    end = pos
    for c in text[pos:]:
        if c == '{':
            brace_depth += 1
        elif c == '}':
            if brace_depth == 0:
                end = pos
                break
            brace_depth -= 1
        pos += 1
    else:
        raise ValueError(f"unclosed initialiser for '{name}'")
    body = text[m.end():end]

    glyphs = []
    for g in re.finditer(r'\{([^}]+)\}', body):
        glyphs.append(_hex_tokens_to_bytes(g.group(0)))
    return elem_size, glyphs


# ---------------------------------------------------------------------------
#  CRC32  (poly 0x04C11DB7  -- matches STM32 CRC peripheral / WinRAR)
# ---------------------------------------------------------------------------

def crc32_bytes(data):
    """Compute uint32 CRC32 (big-endian representation in Flash header)."""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= (b & 0xFF) << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


# ---------------------------------------------------------------------------
#  builder
# ---------------------------------------------------------------------------

def build_font_bin(header_path):
    """Parse *header_path* (TFT_Font_Data.h) and return ``(bytes, n_pages)``.

    The returned binary exactly replicates what
    ``App_Storage_Burn_Font_From_SRAM()`` writes to the first 4-KiB sector of
    the W25Q128::

        0x0000  header     32 B  (magic, version, crc32 placeholder, sizes)
        0x0020  ASCII      95 x 16 B  (1520 B)
        0x0700  CJK        76 x 32 B  (2432 B)
        ...     padding    zero-fill to 256 B boundary
    """
    with open(header_path, 'r', encoding='utf-8', errors='replace') as fh:
        text = fh.read()

    # -- ASCII 8x16 ----------------------------------------------------------
    _, ascii_data = _extract_c_array(text, 'TFT_FONT_8X16')
    if len(ascii_data) < 95:
        raise ValueError(f"need 95 ASCII glyphs, found {len(ascii_data)}")
    for i, g in enumerate(ascii_data[:95]):
        if len(g) != 16:
            raise ValueError(f"ASCII glyph {i} is {len(g)} B, expected 16")

    # -- CJK 16x16 -----------------------------------------------------------
    _, cjk_data = _extract_c_array(text, 'CN_FONT_16X16')
    cjk_count = len(cjk_data)
    for i, g in enumerate(cjk_data):
        if len(g) != 32:
            raise ValueError(f"CJK glyph {i} is {len(g)} B, expected 32")

    # Build payload via a bytearray, inserting at the exact offsets the STM32
    # Flash driver expects.
    buf = bytearray()

    # Header  (32 B)
    header = bytearray(HEADER_SIZE)
    struct.pack_into('<H', header, 0,  FONT_MAGIC)
    struct.pack_into('<H', header, 2,  FONT_VERSION)
    struct.pack_into('<I', header, 4,  0)               # CRC32 placeholder
    struct.pack_into('<I', header, 8,  ASCII_SIZE)
    struct.pack_into('<I', header, 12, CJK_BASE_UNICODE)
    struct.pack_into('<I', header, 16, CJK_COUNT)
    # remainder already zero
    buf.extend(header)

    # Pad to ASCII_BASE
    if len(buf) < ASCII_BASE:
        buf.extend(b'\x00' * (ASCII_BASE - len(buf)))

    # ASCII glyphs
    for g in ascii_data[:95]:
        buf.extend(g)

    # Pad to CJK_BASE
    if len(buf) < CJK_BASE:
        buf.extend(b'\x00' * (CJK_BASE - len(buf)))

    # CJK glyphs
    for g in cjk_data:
        buf.extend(g)

    # Pad to PAGE_SIZE multiple
    surplus = len(buf) % PAGE_SIZE
    if surplus:
        buf.extend(b'\x00' * (PAGE_SIZE - surplus))

    total_pages = len(buf) // PAGE_SIZE
    print(f"[Build] ASCII x95 + CJK x{cjk_count}  ->  {len(buf)} B  ({total_pages} pages)")
    return bytes(buf), total_pages


# ---------------------------------------------------------------------------
#  TCP push helper
# ---------------------------------------------------------------------------

class OTAFontPusher:
    """Connect to the ESP8266 TCP WiFiServer and push font pages."""

    def __init__(self, ip, port=8266, timeout=5.0, retries=3):
        self.ip = ip
        self.port = port
        self.timeout = timeout
        self.retries = retries
        self.sock = None

    def connect(self):
        """Return True on success, False on failure with diagnostics."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        try:
            self.sock.connect((self.ip, self.port))
            print(f"[Connect] TCP {self.ip}:{self.port}  OK")
            return True
        except (socket.timeout, ConnectionRefusedError, OSError) as exc:
            print(f"[Error]  cannot connect to ESP8266: {exc}")
            print("[Hint]   confirm PC and ESP8266 are on the same WiFi and "
                  "the WiFiServer is listening")
            return False

    def _send_frame(self, s):
        self.sock.sendall((s + '\n').encode('ascii'))

    def _recv_line(self):
        """Read one \\n-terminated line (strip CR, return str or None)."""
        data = b''
        while len(data) < 512:
            try:
                ch = self.sock.recv(1)
            except socket.timeout:
                return data.decode('ascii', errors='replace').strip() if data else None
            if not ch:
                break
            if ch == b'\n':
                break
            if ch != b'\r':
                data += ch
        return data.decode('ascii', errors='replace').strip()

    def push(self, bin_data, total_pages):
        """Push *bin_data* as PAGE_SIZE-pages.  Return True on success."""
        # --- wait for STM32 ready signal ------------------------------------
        reply = self._recv_line()
        if reply is None or 'OTA:READY' not in reply:
            print(f"[Error]  STM32 not ready  (reply: {reply})")
            return False

        ok = 0
        err = 0
        for seq in range(total_pages):
            page = bin_data[seq * PAGE_SIZE:(seq + 1) * PAGE_SIZE]
            b64 = base64.b64encode(page).decode('ascii')
            frame = f"OTA:{seq},{b64}"
            self._send_frame(frame)

            acked = False
            for attempt in range(self.retries):
                reply = self._recv_line()
                if reply and f'OTA:ACK:{seq}' in reply:
                    ok += 1
                    acked = True
                    break
                if attempt < self.retries - 1:
                    print(f"  [Retry]  seq={seq}  (attempt {attempt + 2}/{self.retries})")
                    self._send_frame(frame)
                else:
                    err += 1
                    print(f"  [Fail]   seq={seq}: {reply}")

            if (seq + 1) % 4 == 0 or seq == total_pages - 1:
                print(f"  [{seq + 1}/{total_pages}]  ok={ok}  err={err}")

        if err:
            print(f"[Abort]  {err} page(s) failed")
            return False

        # --- final handshake -------------------------------------------------
        self._send_frame("OTA:END")
        reply = self._recv_line()
        if reply and 'OTA:DONE' in reply:
            print(f"[Done]   font updated  ({total_pages} pages, {ok} acknowledged)")
            return True
        print(f"[Fail]   final handshake: {reply}")
        return False

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
            self.sock = None


# ---------------------------------------------------------------------------
#  CLI entry point
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='ESP8266 OTA Font Push Tool -- Phase A (4 KB font binary)')
    parser.add_argument('--ip', required=True,
                        help='ESP8266 IP address (e.g. 192.168.4.1)')
    parser.add_argument('--port', type=int, default=8266,
                        help='TCP port of the WiFiServer  [default: 8266]')
    parser.add_argument('--font-data', default=None,
                        help='path to TFT_Font_Data.h  '
                             '[default: ../../Keil_Project/Hardware/TFT_Font_Data.h]')
    args = parser.parse_args()

    # resolve font data path
    font_path = args.font_data
    if font_path is None:
        font_path = os.path.join(
            os.path.dirname(__file__), '..', '..',
            'Keil_Project', 'Hardware', 'TFT_Font_Data.h')
    font_path = os.path.normpath(os.path.abspath(font_path))
    if not os.path.exists(font_path):
        print(f"[Error]  font data not found: {font_path}")
        sys.exit(1)

    print(f"[Font]   {font_path}")

    try:
        bin_data, total_pages = build_font_bin(font_path)
    except Exception as exc:
        print(f"[Error]  {exc}")
        sys.exit(1)

    pusher = OTAFontPusher(args.ip, args.port)
    if not pusher.connect():
        sys.exit(1)
    try:
        ok = pusher.push(bin_data, total_pages)
        sys.exit(0 if ok else 1)
    finally:
        pusher.close()


if __name__ == '__main__':
    main()
