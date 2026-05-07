#!/usr/bin/env python3

from __future__ import annotations

import argparse
import base64
import json
import threading
import time
import urllib.parse
import webbrowser
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Optional, Tuple, Type

import serial
from serial import SerialException
from serial.tools import list_ports

FRAME_PREFIX = "@CO2MON "
DEFAULT_BAUD = 115200
DEFAULT_LISTEN = "127.0.0.1:8765"
RESPONSE_TIMEOUT_SEC = 90
ROOT_DIR = Path(__file__).resolve().parents[1]
INDEX_PATH = ROOT_DIR / "data" / "index.html"


class DeviceUnavailableError(RuntimeError):
    pass


class SerialRpcClient:
    def __init__(self, port: Optional[str], baud: int) -> None:
        self._preferred_port = port
        self._baud = baud
        self._lock = threading.Lock()
        self._serial: Optional[serial.Serial] = None
        self._next_request_id = 1

    def close(self) -> None:
        with self._lock:
            self._close_locked()

    def request(
        self, method: str, path: str, query: str = "", body: str = ""
    ) -> Tuple[int, str, Optional[str], bytes]:
        with self._lock:
            try:
                self._ensure_connected_locked()
                return self._perform_request_locked(method, path, query, body)
            except (SerialException, OSError) as exc:
                self._close_locked()
                raise DeviceUnavailableError(str(exc)) from exc

    def _ensure_connected_locked(self) -> None:
        if self._serial is not None and self._serial.is_open:
            return

        port = self._preferred_port or self._auto_detect_port()
        try:
            ser = serial.Serial(
                port=port,
                baudrate=self._baud,
                timeout=0.25,
                write_timeout=2,
            )
        except SerialException as exc:
            raise DeviceUnavailableError(f"无法打开串口 {port}: {exc}") from exc

        try:
            ser.dtr = False
            ser.rts = False
        except OSError:
            pass

        time.sleep(2.0)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        self._serial = ser

        try:
            self._perform_request_locked(
                "POST", "set-time", f"epoch={int(time.time())}", "", ensure_connected=False
            )
        except Exception:
            self._close_locked()
            raise

    def _perform_request_locked(
        self,
        method: str,
        path: str,
        query: str,
        body: str,
        *,
        ensure_connected: bool = True,
    ) -> Tuple[int, str, Optional[str], bytes]:
        if ensure_connected:
            self._ensure_connected_locked()
        if self._serial is None:
            raise DeviceUnavailableError("串口未连接")

        request_id = self._next_request_id
        self._next_request_id += 1

        payload = json.dumps(
            {
                "id": request_id,
                "method": method,
                "path": path,
                "query": query,
                "body": body,
            },
            separators=(",", ":"),
        )
        self._serial.write(f"{FRAME_PREFIX}{payload}\n".encode("utf-8"))
        self._serial.flush()

        status_code: Optional[int] = None
        content_type: Optional[str] = None
        filename: Optional[str] = None
        body_bytes = bytearray()
        deadline = time.monotonic() + RESPONSE_TIMEOUT_SEC

        while time.monotonic() < deadline:
            raw_line = self._serial.readline()
            if not raw_line:
                continue

            line = raw_line.decode("utf-8", errors="replace").strip()
            if not line.startswith(FRAME_PREFIX):
                continue

            try:
                frame = json.loads(line[len(FRAME_PREFIX) :])
            except json.JSONDecodeError:
                continue

            if frame.get("id") != request_id:
                continue

            event = frame.get("event")
            if event == "begin":
                status_code = int(frame["status"])
                content_type = frame["contentType"]
                filename = frame.get("filename")
                continue

            if event == "chunk":
                body_bytes.extend(base64.b64decode(frame["data"]))
                continue

            if event == "end":
                if status_code is None or content_type is None:
                    raise DeviceUnavailableError("设备返回了不完整的响应")
                return status_code, content_type, filename, bytes(body_bytes)

        self._close_locked()
        raise DeviceUnavailableError("设备响应超时")

    def _auto_detect_port(self) -> str:
        ports = list(list_ports.comports())
        if not ports:
            raise DeviceUnavailableError("没有找到可用串口，请先插上设备")

        usb_ports = [
            port
            for port in ports
            if port.vid is not None
            or "usb" in (port.description or "").lower()
            or "usb" in (port.hwid or "").lower()
        ]
        candidates = usb_ports or ports
        if len(candidates) == 1:
            return candidates[0].device

        choices = ", ".join(port.device for port in candidates)
        raise DeviceUnavailableError(
            f"检测到多个串口，请用 --port 指定：{choices}"
        )

    def _close_locked(self) -> None:
        if self._serial is None:
            return
        try:
            self._serial.close()
        finally:
            self._serial = None


def build_handler(client: SerialRpcClient) -> Type[BaseHTTPRequestHandler]:
    class LocalUiHandler(BaseHTTPRequestHandler):
        server_version = "CO2MonitorLocalUI/1.0"

        def do_GET(self) -> None:  # noqa: N802
            self._handle()

        def do_POST(self) -> None:  # noqa: N802
            self._handle()

        def do_PUT(self) -> None:  # noqa: N802
            self._handle()

        def log_message(self, fmt: str, *args: object) -> None:
            print(f"{self.address_string()} - {fmt % args}")

        def _handle(self) -> None:
            parsed = urllib.parse.urlsplit(self.path)

            if parsed.path in {"/", "/index.html"}:
                self._serve_index()
                return

            if parsed.path == "/favicon.ico":
                self.send_response(HTTPStatus.NO_CONTENT)
                self.end_headers()
                return

            if parsed.path.startswith("/api/wifi/"):
                self._send_bytes(
                    HTTPStatus.GONE,
                    '{"ok":false,"message":"Wi-Fi API 已废弃"}'.encode("utf-8"),
                    "application/json; charset=utf-8",
                )
                return

            if not (
                parsed.path.startswith("/api/")
                or parsed.path.startswith("/export/")
            ):
                self._send_bytes(
                    HTTPStatus.NOT_FOUND,
                    b'{"ok":false,"message":"Not found"}',
                    "application/json; charset=utf-8",
                )
                return

            content_length = int(self.headers.get("Content-Length", "0") or "0")
            body = self.rfile.read(content_length) if content_length > 0 else b""
            try:
                status_code, content_type, filename, response_body = client.request(
                    self.command,
                    parsed.path,
                    parsed.query,
                    body.decode("utf-8", errors="replace"),
                )
            except DeviceUnavailableError as exc:
                message = json.dumps(
                    {"ok": False, "message": f"设备不可用：{exc}"},
                    ensure_ascii=False,
                ).encode("utf-8")
                self._send_bytes(
                    HTTPStatus.SERVICE_UNAVAILABLE,
                    message,
                    "application/json; charset=utf-8",
                )
                return

            self.send_response(status_code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(response_body)))
            self.send_header("Cache-Control", "no-store")
            if filename:
                self.send_header(
                    "Content-Disposition", f'attachment; filename="{filename}"'
                )
            self.end_headers()
            if response_body:
                self.wfile.write(response_body)

        def _serve_index(self) -> None:
            body = INDEX_PATH.read_bytes()
            self._send_bytes(
                HTTPStatus.OK,
                body,
                "text/html; charset=utf-8",
            )

        def _send_bytes(
            self, status: int, body: bytes, content_type: str
        ) -> None:
            self.send_response(int(status))
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            if body:
                self.wfile.write(body)

    return LocalUiHandler


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="CO2 Monitor local USB UI")
    parser.add_argument("--port", help="串口设备，例如 /dev/tty.usbserial-0001")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="串口波特率")
    parser.add_argument(
        "--listen",
        default=DEFAULT_LISTEN,
        help="监听地址，默认 127.0.0.1:8765",
    )
    parser.add_argument(
        "--open",
        action="store_true",
        help="启动后自动打开浏览器",
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    host, port_text = args.listen.rsplit(":", 1)
    port = int(port_text)

    client = SerialRpcClient(args.port, args.baud)
    handler = build_handler(client)
    server = ThreadingHTTPServer((host, port), handler)

    url = f"http://{host}:{port}"
    print(f"Serving local UI at {url}")
    if args.open:
        webbrowser.open(url)

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        client.close()


if __name__ == "__main__":
    main()
