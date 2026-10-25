"""Synthetic Magpie peer for NOTE4 acceptance; never use a production Web key."""
import argparse
import json
import os
from pathlib import Path
import ssl
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs


def cards(count):
    return [{"provider": ["Codex", "Claude", "Copilot", "DeepSeek", "Unknown"][i % 5],
             "user": f"fixture-{i}", "name": f"Account {i} 中文 🧪", "kind": "subscription",
             "plan": "Synthetic plan", "readAt": "2026-10-26T10:00:00Z",
             "balance": "$12.34", "balanceSplit": [{"label": "Credit", "text": "$12.34"}],
             "windows": [{"name": f"Window {w}", "used": [0, 37, 110][w % 3],
                          "amount": w, "limit": 100, "unit": "requests"} for w in range(i % 5 + 1)]}
            for i in range(count)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8787)
    parser.add_argument("--prefix", default="/note4")
    parser.add_argument("--count", type=int, default=30)
    parser.add_argument("--scenario", type=Path, help="JSON list of GET responses; last response repeats")
    parser.add_argument("--cert", type=Path, help="PEM chain for a hostname trusted by the device public CA bundle")
    parser.add_argument("--private-key", type=Path)
    args = parser.parse_args()
    key = os.environ.get("NOTE4_FIXTURE_KEY", "")
    if not 16 <= len(key) <= 256 or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~" for c in key):
        parser.error("Set NOTE4_FIXTURE_KEY to a synthetic 16–256 character Web key")
    if bool(args.cert) != bool(args.private_key):
        parser.error("--cert and --private-key are required together")
    if args.count < 0 or not args.prefix.startswith("/") or any(c in args.prefix for c in "?#\\"):
        parser.error("Invalid count or prefix")
    prefix = args.prefix.rstrip("/")
    responses = json.loads(args.scenario.read_text(encoding="utf-8")) if args.scenario else [{"body": cards(args.count)}]
    if not isinstance(responses, list) or not responses or any(not isinstance(item, dict) for item in responses):
        parser.error("Scenario must be a nonempty list of response objects")
    lock = threading.Lock()
    sequence = 0

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, *_):
            pass

        def send(self, status, payload=b"", headers=None, truncate=False, drip_ms=0):
            self.send_response(status)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Connection", "close")
            for name, value in (headers or {}).items():
                self.send_header(name, str(value))
            self.end_headers()
            self.close_connection = True
            try:
                body = payload[:len(payload) // 2] if truncate else payload
                if drip_ms:
                    for byte in body:
                        self.wfile.write(bytes([byte]))
                        self.wfile.flush()
                        time.sleep(drip_ms / 1000)
                else:
                    self.wfile.write(body)
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                pass

        def do_POST(self):
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                self.send(400)
                return
            if self.path != prefix + "/_magpie/sign-in" or not 0 < length <= 2048:
                self.send(400)
                return
            fields = parse_qs(self.rfile.read(length).decode("ascii"), keep_blank_values=True)
            if fields.get("key") != [key] or fields.get("back") != [prefix + "/"]:
                self.send(401)
                return
            self.send(303, headers={"Set-Cookie": f"magpie_web_3210={key}; Path={prefix or '/'}; HttpOnly" + ("; Secure" if args.cert else ""),
                                    "Location": prefix + "/"})
            print("fixture login status=303", flush=True)

        def do_GET(self):
            nonlocal sequence
            if self.path != prefix + "/api/usage/quotas":
                self.send(404)
                return
            if f"magpie_web_3210={key}" not in self.headers.get("Cookie", "").split("; "):
                self.send(401)
                return
            with lock:
                index = sequence
                sequence += 1
            response = responses[min(index, len(responses) - 1)]
            time.sleep(max(0, float(response.get("delay", 0))))
            payload = response.get("raw")
            if payload is None:
                payload = json.dumps(response.get("body", cards(args.count)), ensure_ascii=False)
            payload = payload.encode("utf-8")
            headers = {"Content-Type": response.get("contentType", "application/json")}
            for field, header in [("reading", "X-Magpie-Reading"), ("retryAfter", "Retry-After")]:
                if field in response:
                    headers[header] = response[field]
            status = int(response.get("status", 200))
            print(f"fixture get sequence={index} status={status} bytes={len(payload)}", flush=True)
            self.send(status, payload, headers, response.get("truncate", False), max(0, int(response.get("dripMs", 0))))

    server = ThreadingHTTPServer((args.bind, args.port), Handler)
    server.daemon_threads = True
    if args.cert:
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(args.cert, args.private_key)
        server.socket = context.wrap_socket(server.socket, server_side=True)
    print(f"Synthetic fixture listening port={args.port} prefix={prefix or '/'} tls={bool(args.cert)}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
