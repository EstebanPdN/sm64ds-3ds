#!/usr/bin/env python3
"""Static LAN file server for FBI "Remote Install" (one verified CIA directory).

The stock `python -m http.server` speaks HTTP/1.0 with small writes; a 3DS on
Wi-Fi then stalls long enough for FBI to report a timeout (0xD820A069, seen
with BUILD 88). This server answers HTTP/1.1 with Content-Length, disables
Nagle and writes 256 KiB chunks. Usage: lan_server.py PORT DIRECTORY
"""
import functools,http.server,socket,sys

class Handler(http.server.SimpleHTTPRequestHandler):
    protocol_version='HTTP/1.1'
    def setup(self):
        super().setup()
        self.connection.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
        self.connection.setsockopt(socket.SOL_SOCKET,socket.SO_SNDBUF,1<<20)
    def copyfile(self,source,outputfile):
        while chunk:=source.read(256*1024):outputfile.write(chunk)
    def log_message(self,format,*args):
        sys.stderr.write('%s %s\n'%(self.address_string(),format%args));sys.stderr.flush()

def main():
    port=int(sys.argv[1]);directory=sys.argv[2]
    server=http.server.ThreadingHTTPServer(('0.0.0.0',port),functools.partial(Handler,directory=directory))
    server.daemon_threads=True
    print(f'serving {directory} on :{port}',flush=True)
    server.serve_forever()

if __name__=='__main__':main()
