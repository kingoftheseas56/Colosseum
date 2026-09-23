import argparse
import socket
import struct
import time

# BEP 29 header: type/version, extension, connection id, timestamp,
# timestamp difference, window, seq_nr, ack_nr.
ST_STATE = 2
ST_SYN = 4


def syn_packet(connection_id):
    timestamp = int(time.monotonic() * 1_000_000) & 0xFFFFFFFF
    return struct.pack(">BBHIIIHH", (ST_SYN << 4) | 1, 0, connection_id,
                       timestamp, 0, 1 << 20, 1, 0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--syns", type=int, default=3)
    parser.add_argument("--wait-ms", type=int, default=700)
    args = parser.parse_args()
    lines = []

    try:
        with socket.create_connection(("127.0.0.1", args.port), timeout=3):
            lines.append(f"TCP_ACCEPTED port={args.port}")
    except OSError as error:
        lines.append(f"TCP_REFUSED port={args.port} error={error}")

    state_replies = 0
    datagrams = 0
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind(("127.0.0.1", 0))
        probe.settimeout(0.05)
        for attempt in range(args.syns):
            probe.sendto(syn_packet(12345 + attempt), ("127.0.0.1", args.port))
            lines.append(f"UTP_SYN_SENT attempt={attempt + 1}")
            deadline = time.monotonic() + args.wait_ms / 1000
            while time.monotonic() < deadline:
                try:
                    data, _ = probe.recvfrom(2048)
                except socket.timeout:
                    continue
                except OSError as error:
                    lines.append(f"UDP_ERROR error={error}")
                    continue
                datagrams += 1
                kind = data[0] >> 4 if data else -1
                version = data[0] & 0x0F if data else -1
                lines.append(f"UDP_REPLY bytes={len(data)} type={kind} version={version}")
                if len(data) >= 20 and kind == ST_STATE and version == 1:
                    state_replies += 1

    lines.append(f"SUMMARY syns={args.syns} utp_state_replies={state_replies} datagrams={datagrams}")
    with open(args.log, "w", encoding="utf-8") as output:
        output.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
