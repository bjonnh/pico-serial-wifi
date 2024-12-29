import serial
import time
import numpy as np
from tqdm import tqdm
import statistics
import argparse
import os

def setup_serial_ports(client_port, server_port):
    print(f"Client port: {client_port}")
    print(f"Server port: {server_port}")

    client = serial.Serial(client_port, 115200, timeout=1)
    server = serial.Serial(server_port, 115200, timeout=1)

    client.reset_input_buffer()
    client.reset_output_buffer()
    server.reset_input_buffer()
    server.reset_output_buffer()

    return client, server

def measure_latency(client, server, test_data="ABC"):
    start_time = time.perf_counter()
    client.write(test_data.encode())
    received = server.read(len(test_data))
    c2s_latency = (time.perf_counter() - start_time) * 1000

    start_time = time.perf_counter()
    server.write(test_data.encode())
    received_back = client.read(len(test_data))
    s2c_latency = (time.perf_counter() - start_time) * 1000

    integrity = (received.decode() == test_data and received_back.decode() == test_data)

    return c2s_latency, s2c_latency, integrity

def measure_throughput(sender, receiver, chunk_size=1024, duration=1.0):
    test_data = b'X' * chunk_size
    total_bytes = 0
    start_time = time.perf_counter()

    while (time.perf_counter() - start_time) < duration:
        sender.write(test_data)
        sender.flush()

        received = receiver.read(chunk_size)
        if received:
            total_bytes += len(received)

        time.sleep(0.001)

    while receiver.in_waiting:
        received = receiver.read(receiver.in_waiting)
        total_bytes += len(received)

    elapsed_time = time.perf_counter() - start_time
    throughput = (total_bytes * 8) / (elapsed_time * 1000)  # kbps
    return throughput, total_bytes, elapsed_time

def transfer_large_data(sender, receiver, data):
    chunk_size = 1024
    total_sent = 0
    received_data = bytearray()

    start_time = time.perf_counter()

    # Send data in chunks
    while total_sent < len(data):
        chunk = data[total_sent:total_sent + chunk_size]
        sender.write(chunk)
        sender.flush()
        total_sent += len(chunk)
        time.sleep(0.01)

        while receiver.in_waiting:
            received_data.extend(receiver.read(receiver.in_waiting))

    # Wait for any remaining data
    # Read any remaining data
    while receiver.in_waiting:
        received_data.extend(receiver.read(receiver.in_waiting))
        time.sleep(1)

    elapsed_time = time.perf_counter() - start_time - 1 # We remove the 1 second sleep
    integrity = (data == received_data)
    # Check length and print if it doesn't match
    if len(data) != len(received_data):
        print(f"Data length mismatch: {len(data)} sent vs {len(received_data)} received")
    else:
        diff_count = sum([1 for i in range(len(data)) if data[i] != received_data[i]])

        if diff_count > 0:
            print(f"Data integrity check failed: {diff_count} bytes are different")

    throughput = (len(received_data) * 8) / (elapsed_time * 1000)  # kbps

    return elapsed_time, throughput, integrity

def flush(serial_port):
    while serial_port.in_waiting:
        serial_port.read(serial_port.in_waiting)

def print_stats(latencies, direction):
    min_lat = min(latencies)
    max_lat = max(latencies)
    min_idx = latencies.index(min_lat)
    max_idx = latencies.index(max_lat)
    avg = statistics.mean(latencies)
    sd = statistics.stdev(latencies)

    print(f"\n{direction} Statistics (ms):")
    print(f"Min: {min_lat:.3f} (at index {min_idx})")
    print(f"Max: {max_lat:.3f} (at index {max_idx})")
    print(f"Avg: {avg:.3f}")
    print(f"SD: {sd:.3f}")
    print(f"P90: {np.percentile(latencies, 90):.3f}")
    print(f"P95: {np.percentile(latencies, 95):.3f}")
    print(f"P99: {np.percentile(latencies, 99):.3f}")

def main():
    parser = argparse.ArgumentParser(description='Measure serial port latency and throughput')
    parser.add_argument('--client', type=str, default='/dev/ttyACM1',
                        help='Client serial port (default: /dev/ttyACM1)')
    parser.add_argument('--server', type=str, default='/dev/ttyACM2',
                        help='Server serial port (default: /dev/ttyACM2)')
    parser.add_argument('--tests', type=int, default=1000,
                        help='Number of latency tests to run (default: 1000)')
    parser.add_argument('--chunk-size', type=int, default=1024,
                        help='Chunk size for throughput test in bytes (default: 1024)')
    parser.add_argument('--throughput-duration', type=float, default=1.0,
                        help='Duration of each throughput test in seconds (default: 1.0)')
    parser.add_argument('--large-transfer', action='store_true',
                        help='Perform 128KB random data transfer test')

    args = parser.parse_args()
    client, server = setup_serial_ports(args.client, args.server)

    try:
        # Latency Tests
        flush(client)
        flush(server)
        print("\n=== Latency Tests ===")
        c2s_latencies = []
        s2c_latencies = []
        integrity_failures = 0

        for _ in tqdm(range(args.tests), desc="Running latency tests"):
            c2s_lat, s2c_lat, integrity = measure_latency(client, server)
            c2s_latencies.append(c2s_lat)
            s2c_latencies.append(s2c_lat)
            if not integrity:
                integrity_failures += 1
        time.sleep(1)
        flush(client)
        flush(server)

        print_stats(c2s_latencies, "Client to Server")
        print_stats(s2c_latencies, "Server to Client")
        print(f"\nIntegrity Failures: {integrity_failures}/{args.tests}")

        # Throughput Tests
        print("\n=== Throughput Tests ===")
        print("\nClient to Server:")
        c2s_throughput, c2s_bytes, c2s_time = measure_throughput(
            client, server, args.chunk_size, args.throughput_duration)
        print(f"Throughput: {c2s_throughput:.2f} kbps")
        print(f"Bytes transferred: {c2s_bytes:,}")
        print(f"Time elapsed: {c2s_time:.2f} seconds")
        time.sleep(1)
        flush(client)
        flush(server)

        print("\nServer to Client:")
        s2c_throughput, s2c_bytes, s2c_time = measure_throughput(
            server, client, args.chunk_size, args.throughput_duration)
        print(f"Throughput: {s2c_throughput:.2f} kbps")
        print(f"Bytes transferred: {s2c_bytes:,}")
        print(f"Time elapsed: {s2c_time:.2f} seconds")
        print("\n=== 128KB Random Data Transfer Test ===")
        # Generate random data
        random_data = os.urandom(128 * 1024)

        time.sleep(1)
        flush(client)
        flush(server)

        print("\nClient to Server:")
        time_c2s, throughput_c2s, integrity_c2s = transfer_large_data(client, server, random_data)
        print(f"Time: {time_c2s:.2f} seconds")
        print(f"Throughput: {throughput_c2s:.2f} kbps")
        print(f"Data integrity: {'OK' if integrity_c2s else 'FAILED'}")

        time.sleep(1)
        flush(client)
        flush(server)

        print("\nServer to Client:")
        time_s2c, throughput_s2c, integrity_s2c = transfer_large_data(server, client, random_data)
        print(f"Time: {time_s2c:.2f} seconds")
        print(f"Throughput: {throughput_s2c:.2f} kbps")
        print(f"Data integrity: {'OK' if integrity_s2c else 'FAILED'}")


        time.sleep(1)
        flush(client)
        flush(server)

    finally:
        client.close()
        server.close()

if __name__ == "__main__":
    main()
