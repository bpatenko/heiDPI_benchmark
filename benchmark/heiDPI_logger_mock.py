#!/usr/bin/env python3
import socket

HOST = '127.0.0.1'
PORT = 7000

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(1)
    print(f"Mock-Logger listening on {HOST}:{PORT}")
    conn, addr = srv.accept()
    with conn:
        print("Generator connected:", addr)
        buf = b''
        while True:
            data = conn.recv(1024)
            if not data:
                break
            buf += data
            # Split on newline, print full messages
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                print("Received:", line.decode())
    print("Connection closed.")
