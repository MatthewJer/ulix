# A tiny web server: every request gets "hello world" as an HTML page.
# On Ulix:  /musl/python /musl/hello_http.py       then open  http://<address of the machine>:8080/  in a browser.
import socketserver
from http.server import BaseHTTPRequestHandler, HTTPServer

PORT = 8080
PAGE = b"<html><body><h1>hello world</h1></body></html>\n"

class Hello(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(PAGE)))
        self.end_headers()
        self.wfile.write(PAGE)

class Server(HTTPServer):
    def server_bind(self):                       # (HTTPServer asks the name service for its own name, which takes seconds here and is not needed)
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = "ulix", PORT

if __name__ == "__main__":
    print("serving hello world on port %d (Ctrl-C ends it)" % PORT, flush=True)
    Server(("", PORT), Hello).serve_forever()
