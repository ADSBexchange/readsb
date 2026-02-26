"""
Network connection integration tests.

M: Connection resilience (reconnect)
N: Multiple simultaneous clients
P: Net-connector outbound (SBS)
T: Net-connector outbound (Beast)
"""

import socket
import time
import unittest

from conftest import (
    ReadsbInstance, beast_frame, feed_sbs,
    make_df17_position, sbs_msg3,
)


# ===================================================================
# M: Connection Resilience
# ===================================================================

class TestConnectionResilience(unittest.TestCase):
    """Test input/output reconnection behaviour."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_m1_sbs_in_reconnect(self):
        """Disconnect and reconnect SBS-in, new aircraft appears."""
        # First connection: feed aircraft
        sock1 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )
        feed_sbs(sock1, [
            sbs_msg3("B00001", alt=20000, lat=51.5, lon=-0.1),
            sbs_msg3("B00001", alt=20000, lat=51.5, lon=-0.1),
        ])
        time.sleep(0.5)
        sock1.close()

        # Second connection: feed different aircraft
        time.sleep(0.5)
        sock2 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )
        feed_sbs(sock2, [
            sbs_msg3("B00002", alt=25000, lat=52.0, lon=0.0),
            sbs_msg3("B00002", alt=25000, lat=52.0, lon=0.0),
        ])
        time.sleep(0.5)
        sock2.close()

        from conftest import poll_aircraft_json
        data = poll_aircraft_json(self.inst.tmpdir, want_hex="b00002")
        self.assertIsNotNone(data, "B00002 did not appear after reconnect")

    def test_m2_sbs_out_reconnect(self):
        """Disconnect and reconnect SBS-out, still receiving output."""
        # First output connection
        out1 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out1.settimeout(1)
        out1.close()
        time.sleep(0.5)

        # Second output connection
        out2 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out2.settimeout(2)

        # Feed data
        feeder = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )
        received = b""
        deadline = time.monotonic() + 10
        batch = 0
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg3("B00003", alt=30000 + batch, lat=51.5, lon=-0.1),
                sbs_msg3("B00003", alt=30000 + batch, lat=51.5, lon=-0.1),
            ])
            batch += 1
            time.sleep(0.5)
            try:
                chunk = out2.recv(4096)
                if chunk:
                    received += chunk
                    if b"B00003" in received.upper():
                        break
            except socket.timeout:
                pass
        feeder.close()
        out2.close()
        self.assertIn(b"B00003", received.upper(),
                       "ICAO not found on reconnected SBS output")

    def test_m3_process_survives_disconnect(self):
        """After all clients disconnect, process still alive."""
        # Connect and disconnect a client
        sock = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )
        sock.close()
        time.sleep(1)
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb died after client disconnect")


# ===================================================================
# N: Multiple Simultaneous Clients
# ===================================================================

class TestMultipleClients(unittest.TestCase):
    """Test multiple simultaneous readers on SBS output."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance()
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_n1_two_readers(self):
        """Two SBS-out sockets both receive data."""
        out1 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out1.settimeout(2)
        out2 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out2.settimeout(2)

        feeder = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )

        rx1 = b""
        rx2 = b""
        deadline = time.monotonic() + 10
        batch = 0
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg3("C00001", alt=20000 + batch, lat=51.5, lon=-0.1),
                sbs_msg3("C00001", alt=20000 + batch, lat=51.5, lon=-0.1),
            ])
            batch += 1
            time.sleep(0.5)
            for sock, buf_name in [(out1, "rx1"), (out2, "rx2")]:
                try:
                    chunk = sock.recv(4096)
                    if buf_name == "rx1":
                        rx1 += chunk
                    else:
                        rx2 += chunk
                except socket.timeout:
                    pass
            if b"C00001" in rx1.upper() and b"C00001" in rx2.upper():
                break

        feeder.close()
        out1.close()
        out2.close()

        self.assertIn(b"C00001", rx1.upper(), "Reader 1 missing data")
        self.assertIn(b"C00001", rx2.upper(), "Reader 2 missing data")

    def test_n2_disconnect_one_reader(self):
        """Disconnect one reader, remaining one still works."""
        out1 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out1.settimeout(2)
        out2 = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        out2.settimeout(2)

        # Disconnect one
        out1.close()
        time.sleep(0.5)

        feeder = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_in_port), timeout=5
        )

        rx2 = b""
        deadline = time.monotonic() + 10
        batch = 0
        while time.monotonic() < deadline:
            feed_sbs(feeder, [
                sbs_msg3("C00002", alt=25000 + batch, lat=52.0, lon=0.0),
                sbs_msg3("C00002", alt=25000 + batch, lat=52.0, lon=0.0),
            ])
            batch += 1
            time.sleep(0.5)
            try:
                chunk = out2.recv(4096)
                if chunk:
                    rx2 += chunk
                    if b"C00002" in rx2.upper():
                        break
            except socket.timeout:
                pass

        feeder.close()
        out2.close()

        self.assertIn(b"C00002", rx2.upper(),
                       "Remaining reader not receiving after other disconnect")
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb crashed after partial disconnect")


# ===================================================================
# P: Net Connector (outbound client mode)
# ===================================================================

class TestNetConnector(unittest.TestCase):
    """Test --net-connector outbound connection mode."""

    def test_p1_net_connector_sbs_out(self):
        """Readsb connects to a test server with sbs_out protocol."""
        # Start a TCP server
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))
        server_port = server.getsockname()[1]
        server.listen(1)
        server.settimeout(15)

        # Start readsb with --net-connector pointing to our server
        inst = ReadsbInstance(
            extra_args=[
                "--net-connector",
                f"127.0.0.1,{server_port},sbs_out",
            ]
        )
        inst.__enter__()
        try:
            # Accept the incoming connection from readsb
            try:
                client, _ = server.accept()
                client.settimeout(3)
            except socket.timeout:
                self.fail("readsb did not connect to test server")

            # Feed data via SBS input
            feeder = inst.feeder()
            received = b""
            deadline = time.monotonic() + 10
            batch = 0
            while time.monotonic() < deadline:
                feed_sbs(feeder, [
                    sbs_msg3("E00001", alt=20000 + batch, lat=51.5, lon=-0.1),
                    sbs_msg3("E00001", alt=20000 + batch, lat=51.5, lon=-0.1),
                ])
                batch += 1
                time.sleep(0.5)
                try:
                    chunk = client.recv(4096)
                    if chunk:
                        received += chunk
                        if b"E00001" in received.upper():
                            break
                except socket.timeout:
                    pass

            client.close()
            self.assertIn(b"E00001", received.upper(),
                           "ICAO not found in net-connector SBS output")
        finally:
            inst.__exit__(None, None, None)
            server.close()


# ===================================================================
# T: Net-Connector Beast Out
# ===================================================================

class TestNetConnectorBeastOut(unittest.TestCase):
    """Test --net-connector with beast_out protocol."""

    def test_t1_net_connector_beast_out(self):
        """Net-connector beast_out sends Beast data to remote server."""
        # Start a TCP server to receive the net-connector connection
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("127.0.0.1", 0))
        server_port = server.getsockname()[1]
        server.listen(1)
        server.settimeout(15)

        try:
            # Launch readsb with net-connector pointing to our server
            inst = ReadsbInstance(
                beast_in=True,
                extra_args=[
                    "--json-reliable", "1",
                    "--net-connector",
                    f"127.0.0.1,{server_port},beast_out",
                ],
            )
            inst.__enter__()

            try:
                # Accept the incoming connection from readsb
                client, _ = server.accept()
                client.settimeout(10)

                # Feed DF17 position messages via Beast input
                beast_sock = socket.create_connection(
                    ("127.0.0.1", inst.beast_in_port), timeout=5
                )

                ts = 90000
                for batch in range(15):
                    lat = 51.5 + batch * 0.002
                    lon = -0.1 + batch * 0.002
                    even = make_df17_position("D00001", lat, lon, 35000, odd=0)
                    odd = make_df17_position("D00001", lat, lon, 35000, odd=1)
                    beast_sock.sendall(beast_frame(even, timestamp=ts))
                    ts += 500
                    beast_sock.sendall(beast_frame(odd, timestamp=ts))
                    ts += 500
                    time.sleep(0.15)

                # Collect data from the net-connector
                received = b""
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    try:
                        chunk = client.recv(8192)
                        if chunk:
                            received += chunk
                            # Check if we have what we need
                            if len(received) > 20:
                                break
                        else:
                            break
                    except socket.timeout:
                        if received:
                            break

                beast_sock.close()
                client.close()

                self.assertTrue(len(received) > 0,
                                "No data received from net-connector beast_out")

                # Verify Beast framing present
                self.assertIn(b'\x1a', received,
                              "Beast escape byte not found in net-connector output")

                # Parse frames and look for ICAO D00001
                frames_found = 0
                icao_found = False
                i = 0
                while i < len(received) - 1:
                    if received[i] == 0x1A:
                        if i + 1 < len(received) and received[i + 1] == 0x1A:
                            i += 2
                            continue
                        type_byte = received[i + 1:i + 2]
                        if type_byte in (b'1', b'2', b'3'):
                            frames_found += 1
                            # For type '3', try to extract ICAO
                            if type_byte == b'3':
                                # Unescape and extract: 6 ts + 1 sig + 14 msg
                                payload = bytearray()
                                j = i + 2
                                while j < len(received) and len(payload) < 21:
                                    if (received[j] == 0x1A and j + 1 < len(received)):
                                        if received[j + 1] == 0x1A:
                                            payload.append(0x1A)
                                            j += 2
                                            continue
                                        else:
                                            break
                                    payload.append(received[j])
                                    j += 1
                                if len(payload) >= 21:
                                    # ICAO at positions 7+1, 7+2, 7+3 (after ts+sig)
                                    icao = ((payload[8] << 16) |
                                            (payload[9] << 8) |
                                            payload[10])
                                    if icao == 0xD00001:
                                        icao_found = True
                        i += 2
                    else:
                        i += 1

                self.assertGreater(frames_found, 0,
                                   "No Beast frames in net-connector output")
                self.assertTrue(icao_found,
                                "ICAO D00001 not in net-connector beast_out")

            finally:
                inst.__exit__(None, None, None)
        finally:
            server.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
