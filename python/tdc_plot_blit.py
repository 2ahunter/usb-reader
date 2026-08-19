import socket
import struct
import collections
import threading
import time
import matplotlib
# matplotlib.use('Qt5Agg') # Or 'TkAgg'
matplotlib.use('TkAgg')
import matplotlib.pyplot as plt
import matplotlib.animation as animation

# --- Configuration ---
SERVER_IP = "127.0.0.1"
PORT = 8080
WINDOW_SIZE = 2000

# Deques start completely empty and will fill up to WINDOW_SIZE
tdc0_data = collections.deque(maxlen=WINDOW_SIZE)
tdc1_data = collections.deque(maxlen=WINDOW_SIZE)
data_lock = threading.Lock()

running = True

# --- Background Network Thread ---
def udp_worker():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.2)
    
    while running:
        try:
            sock.sendto(b'ping', (SERVER_IP, PORT))
            data, _ = sock.recvfrom(1024)
            
            if len(data) >= 8:
                v0, v1 = struct.unpack("<ff", data[:8])
                with data_lock:
                    tdc0_data.append(v0)
                    tdc1_data.append(v1)
        except Exception:
            pass
            
        time.sleep(0.0005) # poll delay
            
    sock.close()

net_thread = threading.Thread(target=udp_worker, daemon=True)
net_thread.start()

# --- Matplotlib Setup ---
fig, ax = plt.subplots()

# Initialize lines empty
line0, = ax.plot([], [], label="TDC0", color='teal', animated=True)
line1, = ax.plot([], [], label="TDC1", color='coral', animated=True)

ax.set_title("Real-Time TDC Data")
ax.set_ylabel("TDC Reading")
ax.set_xlabel("Samples Window")
ax.grid(True)
ax.set_xlim(0, WINDOW_SIZE)

# Set initial limits to infinity so first incoming point triggers autoscale
current_ymin = float('inf')
current_ymax = float('-inf')

def update_plot(frame):
    global current_ymin, current_ymax
    
    with data_lock:
        y0 = list(tdc0_data)
        y1 = list(tdc1_data)
        
    # Wait until we have at least one sample
    if not y0 or not y1:
        return line0, line1
        
    x_vals = list(range(len(y0)))
    
    line0.set_data(x_vals, y0)
    line1.set_data(x_vals, y1)
    
    # Compute bounds strictly from real data
    min_val = min(min(y0), min(y1))
    max_val = max(max(y0), max(y1))
    data_range = max_val - min_val
    
    # Scale axes if data moves out of current bounds
    if min_val < current_ymin:
        padding = max(0.1, data_range * 0.1)
        current_ymin = min_val - padding
        current_ymax = min_val + data_range + padding
        ax.set_ylim(current_ymin, current_ymax)
        fig.canvas.draw()
    elif max_val > current_ymax:
        padding = max(0.1, data_range * 0.1)
        current_ymax = max_val + padding
        current_ymin = max_val - data_range - padding
        ax.set_ylim(current_ymin, current_ymax)
        fig.canvas.draw()
    elif data_range < (current_ymax - current_ymin) * 0.5:
        # If data range is significantly smaller than current view, shrink view
        padding = max(0.1, data_range * 0.1)
        current_ymin = min_val - padding
        current_ymax = max_val + padding
        ax.set_ylim(current_ymin, current_ymax)
        fig.canvas.draw()
            
    return line0, line1

# Render GUI at 50 Hz (20 ms interval)
ani = animation.FuncAnimation(
    fig, 
    update_plot, 
    blit=True, 
    interval=20, 
    cache_frame_data=False
)

plt.legend(loc="upper right")
plt.show()

running = False