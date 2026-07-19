#!/usr/bin/env python3
"""Small loopback-only HTTP/CONNECT proxy for A26 bring-up over adb reverse."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import sys
from urllib.parse import urlsplit

MAX_HEADER = 64 * 1024


async def pump(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    try:
        while data := await reader.read(64 * 1024):
            writer.write(data)
            await writer.drain()
    except (ConnectionError, asyncio.CancelledError):
        pass
    finally:
        with contextlib.suppress(Exception):
            writer.write_eof()


def split_authority(authority: str, default_port: int) -> tuple[str, int]:
    if authority.startswith("["):
        end = authority.find("]")
        if end < 0:
            raise ValueError("invalid IPv6 authority")
        host = authority[1:end]
        port = int(authority[end + 2 :]) if authority[end + 1 :].startswith(":") else default_port
        return host, port
    host, separator, port_text = authority.rpartition(":")
    if separator and port_text.isdigit():
        return host, int(port_text)
    return authority, default_port


async def read_headers(reader: asyncio.StreamReader) -> tuple[bytes, list[bytes]]:
    request_line = await reader.readline()
    if not request_line:
        return b"", []
    headers: list[bytes] = []
    size = len(request_line)
    while True:
        line = await reader.readline()
        size += len(line)
        if size > MAX_HEADER:
            raise ValueError("request headers too large")
        if line in (b"\r\n", b"\n", b""):
            break
        headers.append(line)
    return request_line, headers


async def handle(client_reader: asyncio.StreamReader, client_writer: asyncio.StreamWriter) -> None:
    peer = client_writer.get_extra_info("peername")
    try:
        request_line, headers = await read_headers(client_reader)
        if not request_line:
            return
        method, target, version = request_line.decode("latin-1").strip().split(" ", 2)

        if method.upper() == "CONNECT":
            host, port = split_authority(target, 443)
            upstream_reader, upstream_writer = await asyncio.open_connection(host, port)
            client_writer.write(f"{version} 200 Connection Established\r\n\r\n".encode())
            await client_writer.drain()
            print(f"CONNECT {host}:{port}", flush=True)
            await asyncio.gather(
                pump(client_reader, upstream_writer),
                pump(upstream_reader, client_writer),
            )
            upstream_writer.close()
            with contextlib.suppress(Exception):
                await upstream_writer.wait_closed()
            return

        parsed = urlsplit(target)
        if parsed.scheme.lower() != "http" or not parsed.hostname:
            raise ValueError("only HTTP absolute-form requests and CONNECT are supported")
        host = parsed.hostname
        port = parsed.port or 80
        path = parsed.path or "/"
        if parsed.query:
            path += "?" + parsed.query
        upstream_reader, upstream_writer = await asyncio.open_connection(host, port)
        upstream_writer.write(f"{method} {path} {version}\r\n".encode("latin-1"))
        for header in headers:
            name = header.split(b":", 1)[0].strip().lower()
            if name not in (b"proxy-connection", b"connection"):
                upstream_writer.write(header)
        upstream_writer.write(b"Connection: close\r\n\r\n")
        await upstream_writer.drain()
        print(f"{method} http://{host}:{port}{path}", flush=True)
        await asyncio.gather(
            pump(client_reader, upstream_writer),
            pump(upstream_reader, client_writer),
        )
        upstream_writer.close()
        with contextlib.suppress(Exception):
            await upstream_writer.wait_closed()
    except Exception as error:
        print(f"proxy error from {peer}: {error}", file=sys.stderr, flush=True)
        with contextlib.suppress(Exception):
            client_writer.write(b"HTTP/1.1 502 Bad Gateway\r\nConnection: close\r\n\r\n")
            await client_writer.drain()
    finally:
        client_writer.close()
        with contextlib.suppress(Exception):
            await client_writer.wait_closed()


async def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--listen", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18777)
    args = parser.parse_args()
    server = await asyncio.start_server(handle, args.listen, args.port)
    addresses = ", ".join(str(sock.getsockname()) for sock in server.sockets or [])
    print(f"A26 adb proxy listening on {addresses}", flush=True)
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    asyncio.run(main())
