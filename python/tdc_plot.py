import socket
import struct
import collections
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# --- Configuration ---
print("Starting TDC Real-Time Plotting Client...")
SERVER_IP = "127.0.0.1"  # Replace with target IP if running remotely
PORT = 8080
WINDOW_SIZE = 500        # Number of data points to show on screen at once

# Deques handle rolling window array structures efficiently
x_data = collections.deque(maxlen=WINDOW_SIZE)
tdc0_data = collections.deque(maxlen=WINDOW_SIZE)
tdc1_data = collections.deque(maxlen=WINDOW_SIZE)

# Initialize deque values with defaults
for i in range(WINDOW_SIZE):
    x_data.append(i)
    tdc0_data.append(0.0)
    tdc1_data.append(0.0)

# Set up UDP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.settimeout(0.2)

# Set up Matplotlib Figure with two plot lines
fig, ax = plt.subplots()
line0, = ax.plot(x_data, tdc0_data, label="TDC0", color='teal', linewidth=1.5)
line1, = ax.plot(x_data, tdc1_data, label="TDC1", color='coral', linewidth=1.5)

ax.set_title("Real-Time TDC Data")
ax.set_ylabel("TDC Reading")
ax.set_xlabel("Samples Window")
ax.grid(True)

def fetch_data():
    try:
        # Send a ping payload to trigger response
        sock.sendto(b'ping', (SERVER_IP, PORT))
        data, _ = sock.recvfrom(1024)
        
        # Unpack two 32-bit Little-Endian floats (<ff) from 8 bytes
        tdc0, tdc1 = struct.unpack("<ff", data[:8])
        return tdc0, tdc1
    except Exception:
        # Fall back to returning the last values if packet drops/times out
        return tdc0_data[-1], tdc1_data[-1]

def update_plot(frame):
    v0, v1 = fetch_data()
    tdc0_data.append(v0)
    tdc1_data.append(v1)
    
    line0.set_ydata(tdc0_data)
    line1.set_ydata(tdc1_data)
    
    # Dynamically scale view bounds based on combined local window min/max
    combined_min = min(min(tdc0_data), min(tdc1_data))
    combined_max = max(max(tdc0_data), max(tdc1_data))
    padding = max(0.1, (combined_max - combined_min) * 0.1)
    
    ax.set_ylim(combined_min - padding, combined_max + padding)
    
    return line0, line1

# Animate at ~50Hz refresh rate (every 20ms)
ani = animation.FuncAnimation(fig, update_plot, blit=False, interval=20, cache_frame_data=False)
plt.legend(loc="upper right")
plt.show()

sock.close()