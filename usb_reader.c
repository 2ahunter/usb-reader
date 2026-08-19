#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define VENDOR_ID     0x2e8a    // Raspberry Pi Pico
#define PRODUCT_ID    0x0009    
#define INTERFACE_NUM 1         // CDC Data Interface
#define ENDPOINT_OUT  0x02      // Bulk OUT
#define ENDPOINT_IN   0x82      // Bulk IN
#define TIMEOUT_MS    1000
#define BUFFER_SIZE   512

#define UDP_PORT      8080      // UDP Listening Port

// --- Module-Level Variables (Latest values for monitoring/UDP) ---
float tdc0_time = 0.0f;
float tdc1_time = 0.0f;
pthread_mutex_t time_mutex = PTHREAD_MUTEX_INITIALIZER;
int logging = 0; // logging disabled
const char *logfile = "data.csv";

static volatile int continuous_reading = 1;

void signal_handler(int signum) {
    (void)signum;
    continuous_reading = 0;
}

int tdc_init(libusb_device_handle *dev_handle) {
    int bytes_sent = 0;
    int r;

    const char *cmd1 = "tdc count 1000\r";
    r = libusb_bulk_transfer(dev_handle, ENDPOINT_OUT, (unsigned char *)cmd1, (int)strlen(cmd1), &bytes_sent, TIMEOUT_MS);
    if (r < 0) {
        fprintf(stderr, "Failed to set tdc count: %s\n", libusb_error_name(r));
        return r;
    }

    const char *cmd2 = "tdc1 count 1000\r";
    r = libusb_bulk_transfer(dev_handle, ENDPOINT_OUT, (unsigned char *)cmd2, (int)strlen(cmd2), &bytes_sent, TIMEOUT_MS);
    if (r < 0) {
        fprintf(stderr, "Failed to set tdc1 count: %s\n", libusb_error_name(r));
        return r;
    }

    return 0;
}

// --- USB Worker Thread Function (High-Resolution Capture & Logging) ---
void *usb_thread_func(void *arg) {
    (void)arg;
    libusb_context *ctx = NULL;
    libusb_device_handle *dev_handle = NULL;
    int r;
    FILE *csv_file = NULL;

    if(logging){
        csv_file = fopen(logfile, "w");
        if (!csv_file) {
            perror("USB Thread: Failed to open data.csv");
            return NULL;
        }
        fprintf(csv_file, "TDC0,TDC1\n");
    }

    if (libusb_init(&ctx) < 0) {
        fprintf(stderr, "USB Thread: Error initializing libusb\n");
        if(csv_file) fclose(csv_file);
        return NULL;
    }

    dev_handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!dev_handle) {
        fprintf(stderr, "USB Thread: Could not find or open device (0x%04X:0x%04X)\n", VENDOR_ID, PRODUCT_ID);
        if(csv_file) fclose(csv_file);
        libusb_exit(ctx);
        return NULL;
    }

    if (libusb_kernel_driver_active(dev_handle, INTERFACE_NUM) == 1) {
        libusb_detach_kernel_driver(dev_handle, INTERFACE_NUM);
    }

    r = libusb_claim_interface(dev_handle, INTERFACE_NUM);
    if (r < 0) {
        fprintf(stderr, "USB Thread: Cannot claim interface: %s\n", libusb_error_name(r));
        if(csv_file) fclose(csv_file);
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return NULL;
    }

    libusb_control_transfer(dev_handle, 0x21, 0x22, 0x0003, 0, NULL, 0, 1000);

    if (tdc_init(dev_handle) < 0) {
        if(csv_file) fclose(csv_file);
        libusb_release_interface(dev_handle, INTERFACE_NUM);
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return NULL;
    }

    const char *start_cmd = "both continuous start\r";
    int bytes_sent = 0;
    r = libusb_bulk_transfer(dev_handle, ENDPOINT_OUT, (unsigned char *)start_cmd, (int)strlen(start_cmd), &bytes_sent, TIMEOUT_MS);
    if (r < 0) {
        fprintf(stderr, "USB Thread: Failed to send start command: %s\n", libusb_error_name(r));
        if(csv_file) fclose(csv_file);
        libusb_release_interface(dev_handle, INTERFACE_NUM);
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return NULL;
    }
    printf("USB Thread: Started streaming (%d bytes sent)\n", bytes_sent);

    unsigned char buffer[BUFFER_SIZE];
    int actual_length = 0;

    while (continuous_reading) {
        r = libusb_bulk_transfer(dev_handle, ENDPOINT_IN, buffer, sizeof(buffer) - 1, &actual_length, TIMEOUT_MS);

        if (r == 0 && actual_length > 0) {
            buffer[actual_length] = '\0';

            char *saveptr;
            char *line = strtok_r((char *)buffer, "\r\n", &saveptr);
            while (line != NULL) {
                float val1, val2;
                if (sscanf(line, "%f,%f", &val1, &val2) == 2) {
                    // Log high-resolution sample to CSV
                    if(csv_file) fprintf(csv_file, "%.4f,%.4f\n", val1, val2);
                    
                    // Update shared variables for UDP/monitoring
                    pthread_mutex_lock(&time_mutex);
                    tdc0_time = val1;
                    tdc1_time = val2;
                    pthread_mutex_unlock(&time_mutex);
                }
                line = strtok_r(NULL, "\r\n", &saveptr);
            }

            if(csv_file) fflush(csv_file);

        } else if (r == LIBUSB_ERROR_TIMEOUT) {
            continue;
        } else {
            fprintf(stderr, "USB Thread: Read error: %s\n", libusb_error_name(r));
            break;
        }
    }

    printf("\nUSB Thread: Stopping stream...\n");
    const char *stop_cmd = "stop\r";
    libusb_bulk_transfer(dev_handle, ENDPOINT_OUT, (unsigned char *)stop_cmd, (int)strlen(stop_cmd), &bytes_sent, TIMEOUT_MS);

    libusb_control_transfer(dev_handle, 0x21, 0x22, 0x0000, 0, NULL, 0, 500);
    
    
    if(csv_file) fclose(csv_file);
    
    libusb_release_interface(dev_handle, INTERFACE_NUM);
    libusb_close(dev_handle);
    libusb_exit(ctx);

    printf("USB Thread: Saved data and exited cleanly.\n");
    return NULL;
}

// --- Main Thread (UDP Server & Control Loop) ---
int main(int argc, char *argv[]){
    int cli_opt = 0; //command line parsing
    int port = UDP_PORT;

    while((cli_opt = getopt(argc, argv, "hp:lf:")) != -1) {
        switch(cli_opt) {
            case 'h':
                printf("Usage: %s [-h] [-p port] [-l] [-f logfile]\n", argv[0]);
                printf("  -h : Show this help message\n");
                return 0;
            case 'p':
                port = atoi(optarg);
                printf("UDP port set to: %d\n", port);
                break;
            case 'l':
                logging = 1;
                printf("Logging enabled\n");
                break;
            case 'f':
                logfile = optarg;
                logging = 1; // Enable logging if logfile is specified
                printf("Log file set to: %s\n", logfile);
                break;
            default:
                printf("Usage: %s [-h] [-p port] [-l] [-f logfile]\n", argv[0]);
                return -1; // Exit on invalid option
        }
    }


    signal(SIGINT, signal_handler);

    // Define a packed 8-byte payload structure
    typedef struct __attribute__((packed)) {
        float tdc0;
        float tdc1;
    } tdc_packet_t;

    // Create UDP Socket
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        perror("Failed to create UDP socket");
        return 1;
    }

    // Allow socket port reuse
    int opt = 1;
    setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set 200ms recvfrom timeout so loop condition is checked periodically
    struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
    setsockopt(udp_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Bind UDP server to port 8080
    struct sockaddr_in servaddr, cliaddr; //server address, client address
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);

    if (bind(udp_sock, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("UDP Bind failed");
        close(udp_sock);
        return 1;
    }

    // Start USB thread
    pthread_t usb_thread;
    if (pthread_create(&usb_thread, NULL, usb_thread_func, NULL) != 0) {
        perror("Failed to create USB thread");
        close(udp_sock);
        return 1;
    }

    printf("Main Thread: UDP Server running on port %d. Waiting for requests...\n", port);

    char recv_buffer[128];
    socklen_t len = sizeof(cliaddr);

    // 3. Main UDP Server Loop
    while (continuous_reading) {
        int n = recvfrom(udp_sock, recv_buffer, sizeof(recv_buffer) - 1, 0,
                         (struct sockaddr *)&cliaddr, &len);

        if (n > 0) {
            tdc_packet_t packet;

            // Retrieve thread-safe values
            pthread_mutex_lock(&time_mutex);
            packet.tdc0 = tdc0_time;
            packet.tdc1 = tdc1_time;
            pthread_mutex_unlock(&time_mutex);

            // Transmit back to requesting client
            sendto(udp_sock, &packet, sizeof(packet), 0, (struct sockaddr *)&cliaddr, len);

        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("UDP receive error");
        }
    }

    printf("\nMain Thread: Shutting down UDP server & waiting for USB thread...\n");
    
    close(udp_sock);
    pthread_join(usb_thread, NULL);
    pthread_mutex_destroy(&time_mutex);

    printf("Program finished successfully.\n");
    return 0;
}