import socket
import struct

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
server_address = ('127.0.0.1', 8080)

# Send trigger packet
sock.sendto(b'ping', server_address)

# Receive 8-byte response
data, _ = sock.recvfrom(8)

# '<ff' means: Little-Endian (<), two floats (ff)
tdc0, tdc1 = struct.unpack('<ff', data)

print(f"Received -> TDC0: {tdc0:.4f}, TDC1: {tdc1:.4f}")