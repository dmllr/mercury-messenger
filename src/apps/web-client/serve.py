import ssl
from http.server import HTTPServer, SimpleHTTPRequestHandler

httpd = HTTPServer(('localhost', 4443), SimpleHTTPRequestHandler)

httpd.socket = ssl.wrap_socket(httpd.socket,
                               keyfile="localhost-key.pem",
                               certfile='localhost.pem', server_side=True)

httpd.serve_forever()
