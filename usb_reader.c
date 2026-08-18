#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include <signal.h>

// Replace these values with your specific USB device parameters
#define VENDOR_ID     0x2e8a    // Raspberry pi pico
#define PRODUCT_ID    0x0009    
#define INTERFACE_NUM 1         // CDC Data Interface (contains EP 0x02 and 0x82)
#define ENDPOINT_OUT  0x02      // Bulk OUT
#define ENDPOINT_IN   0x82      // Bulk IN
#define TIMEOUT_MS    1000
#define BUFFER_SIZE   512

static volatile int continuous_reading = 1;

// signal handler to gracefully stop reading on Ctrl+C
void signal_handler(int signum) {
    continuous_reading = 0;
}

int main(void) {
    libusb_context *ctx = NULL;
    libusb_device_handle *dev_handle = NULL;
    int r;

    // Initialize libusb
    if (libusb_init(&ctx) < 0) {
        fprintf(stderr, "Error initializing libusb\n");
        return 1;
    }

    // Open device handle by VID and PID
    dev_handle = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!dev_handle) {
        fprintf(stderr, "Could not find or open device (0x%04X:0x%04X)\n", VENDOR_ID, PRODUCT_ID);
        libusb_exit(ctx);
        return 1;
    }

    // Detach kernel driver if one is attached (common on Linux)
    if (libusb_kernel_driver_active(dev_handle, INTERFACE_NUM) == 1) {
        libusb_detach_kernel_driver(dev_handle, INTERFACE_NUM);
    }

    // Claim the interface
    r = libusb_claim_interface(dev_handle, INTERFACE_NUM);
    if (r < 0) {
        fprintf(stderr, "Cannot claim interface: %s\n", libusb_error_name(r));
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return 1;
    }

    // Send SET_CONTROL_LINE_STATE (0x22) to Interface 0
    // wValue = 0x0003 (Bit 0 = DTR, Bit 1 = RTS)
    r = libusb_control_transfer(
        dev_handle,
        0x21,       // bmRequestType: Host-to-Device | Class | Interface
        0x22,       // bRequest: SET_CONTROL_LINE_STATE
        0x0003,     // wValue: DTR (1) | RTS (1)
        0,          // wIndex: Interface 0
        NULL,       // data
        0,          // wLength
        1000        // timeout
    );

    if (r < 0) {
        fprintf(stderr, "Failed to set CDC line state: %s\n", libusb_error_name(r));
    }

    // Send Start Streaming Command
    const char *cmd = "both continuous start\r"; // works only with '\r' termination
    int bytes_sent = 0;

    r = libusb_bulk_transfer(
        dev_handle,
        ENDPOINT_OUT,
        (unsigned char *)cmd,
        (int)strlen(cmd),
        &bytes_sent,
        TIMEOUT_MS
    );

    if (r < 0) {
        fprintf(stderr, "Failed to send start command: %s\r\n", libusb_error_name(r));
        libusb_release_interface(dev_handle, INTERFACE_NUM);
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return 1;
    }
    printf("Sent start command (%d bytes)\r\n", bytes_sent);

    // Open output CSV file
    FILE *csv_file = fopen("data.csv", "w");
    if (!csv_file) {
        fprintf(stderr,"Failed to open output.csv");
        libusb_release_interface(dev_handle, INTERFACE_NUM);
        libusb_close(dev_handle);
        libusb_exit(ctx);
        return 1;
    }
    
    // Write CSV header
    fprintf(csv_file, "TDC0,TDC1\n");

    unsigned char buffer[BUFFER_SIZE];
    int actual_length = 0;
    int read_count = 0;
    const int max_reads = 100; // Total number of bulk transfers to process

    printf("Reading streaming data from USB...\r\n");

    // Streaming loop
    while (continuous_reading && read_count < max_reads) {
        r = libusb_bulk_transfer(
            dev_handle,
            ENDPOINT_IN,
            buffer,
            sizeof(buffer) - 1,
            &actual_length,
            TIMEOUT_MS
        );

        if (r == 0 && actual_length > 0) {
            buffer[actual_length] = '\0'; // Null-terminate incoming ASCII string

            // Tokenize lines by newline characters
            char *line = strtok((char *)buffer, "\r\n");
            while (line != NULL) {
                float val1, val2;
                // Parse two comma-separated floats per line
                if (sscanf(line, "%f,%f", &val1, &val2) == 2) {
                    fprintf(csv_file, "%.4f,%.4f\n", val1, val2);
                }
                line = strtok(NULL, "\r\n");
            }
            
            fflush(csv_file);
            read_count++;
        } else if (r == LIBUSB_ERROR_TIMEOUT) {
            fprintf(stderr, "Read timeout, retrying...\r\n");
            continue; // Retry transfer on timeout
        } else {
            fprintf(stderr, "Read error: %s\n", libusb_error_name(r));
            break;
        }
    }

    // Tell the Pico to stop streaming
    const char *stop_cmd = "stop\r"; // Or "both continuous stop\r"
    r = libusb_bulk_transfer(
        dev_handle,
        ENDPOINT_OUT,
        (unsigned char *)stop_cmd,
        (int)strlen(stop_cmd),
        &bytes_sent,
        TIMEOUT_MS
    );
    if (r == 0) {
        printf("Sent stop command (%d bytes)\n", bytes_sent);
    }

    // Cleanup resources
    fclose(csv_file);
    libusb_release_interface(dev_handle, INTERFACE_NUM);
    libusb_close(dev_handle);
    libusb_exit(ctx);

    printf("Successfully captured data to data.csv\n");
    return 0;
}