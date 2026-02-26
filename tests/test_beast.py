"""
Beast protocol integration tests.

F: Beast binary input
H: Multi-output format (Beast input -> Beast/Raw/SBS output)
R: Beast Reduce output
S: Beast output format and escaping
"""

import socket
import time
import unittest

from conftest import (
    ReadsbInstance, beast_frame, feed_sbs,
    make_df17_msg, make_df17_position, modes_checksum,
    poll_aircraft_json, sbs_msg3,
)


# ===================================================================
# F: Beast Binary Input
# ===================================================================

class TestBeastInput(unittest.TestCase):
    """Feed Beast binary data on --net-bi-port, verify JSON output."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(beast_in=True)
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _beast_feeder(self):
        """Return a TCP connection to the Beast-in port."""
        return socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

    def test_f1_beast_df17(self):
        """DF17 via Beast binary -> aircraft appears in JSON."""
        # Known-good DF17 message (ICAO 4840D6, identification)
        msg = bytes.fromhex("8D4840D6202CC371C32CE0576098")
        frame = beast_frame(msg, timestamp=0x000102030405)
        sock = self._beast_feeder()
        # Send multiple times for reliable detection
        for _ in range(5):
            sock.sendall(frame)
            time.sleep(0.3)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="4840d6")
        self.assertIsNotNone(data, "aircraft.json never contained 4840d6")
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("4840d6", hexes)

    def test_f2_beast_escape_handling(self):
        """Beast frame with 0x1A in timestamp is handled correctly."""
        # Construct a DF17 with a known ICAO, use timestamp containing 0x1A
        msg = make_df17_msg("1A1A1A", typecode=4, payload_bytes=b'\x20\x30\x40\x50\x60\x70')
        # Timestamp intentionally contains 0x1A bytes
        frame = beast_frame(msg, timestamp=0x001A001A001A)
        sock = self._beast_feeder()
        for _ in range(5):
            sock.sendall(frame)
            time.sleep(0.3)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="1a1a1a")
        self.assertIsNotNone(data, "aircraft.json never contained 1a1a1a")
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("1a1a1a", hexes)

    def test_f3_beast_no_crash_on_garbage(self):
        """Random bytes on Beast-in don't crash readsb."""
        sock = self._beast_feeder()
        # Send garbage data
        garbage = bytes(range(256)) * 4
        sock.sendall(garbage)
        sock.close()
        time.sleep(1)
        # Process should still be running
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb crashed on garbage Beast input")


# ===================================================================
# H: Multi-Output Format
# ===================================================================

class TestMultiOutput(unittest.TestCase):
    """Feed Beast input, verify data on Beast-out, raw-out, and SBS-out."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            beast_in=True, beast_out=True, raw_out=True,
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_h1_cross_format(self):
        """Beast input appears on Beast-out, raw-out, and SBS-out."""
        # Connect to all output ports first
        beast_out = socket.create_connection(
            ("127.0.0.1", self.inst.beast_out_port), timeout=5
        )
        beast_out.settimeout(2)
        raw_out = socket.create_connection(
            ("127.0.0.1", self.inst.raw_out_port), timeout=5
        )
        raw_out.settimeout(2)
        sbs_out = socket.create_connection(
            ("127.0.0.1", self.inst.sbs_out_port), timeout=5
        )
        sbs_out.settimeout(2)

        # Feed a DF17 via Beast
        msg = bytes.fromhex("8D4840D6202CC371C32CE0576098")
        frame = beast_frame(msg, timestamp=0x000102030405)

        beast_in = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        beast_rx = b""
        raw_rx = b""
        sbs_rx = b""

        deadline = time.monotonic() + 10
        batch = 0
        while time.monotonic() < deadline:
            beast_in.sendall(frame)
            batch += 1
            time.sleep(0.5)

            for sock, buf_name in [(beast_out, "beast"), (raw_out, "raw"), (sbs_out, "sbs")]:
                try:
                    chunk = sock.recv(4096)
                    if buf_name == "beast":
                        beast_rx += chunk
                    elif buf_name == "raw":
                        raw_rx += chunk
                    else:
                        sbs_rx += chunk
                except socket.timeout:
                    pass

            # Check if we have data on all three
            if beast_rx and raw_rx and sbs_rx:
                break

        beast_in.close()
        beast_out.close()
        raw_out.close()
        sbs_out.close()

        # Beast output should contain 0x1A framing
        self.assertIn(b'\x1a', beast_rx,
                       "No Beast framing in Beast output")

        # Raw output should contain the hex of our ICAO
        raw_text = raw_rx.decode(errors="replace").upper()
        self.assertIn("4840D6", raw_text,
                       "ICAO not found in raw output")

        # SBS output should contain the ICAO
        sbs_text = sbs_rx.decode(errors="replace").upper()
        self.assertIn("4840D6", sbs_text,
                       "ICAO not found in SBS output")


# ===================================================================
# R: Beast Reduce Output
# ===================================================================

class TestBeastReduceOutput(unittest.TestCase):
    """Test Beast Reduce output format and deduplication."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            beast_in=True, beast_reduce=True,
            extra_args=["--json-reliable", "1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_r1_beast_reduce_format(self):
        """Beast reduce output has correct Beast framing."""
        reduce_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_reduce_port), timeout=5
        )
        reduce_sock.settimeout(5)

        beast_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        # Feed multiple DF17 position messages
        received = b""
        ts = 1000
        for batch in range(20):
            lat = 51.5 + batch * 0.002
            lon = -0.1 + batch * 0.002
            even_msg = make_df17_position("A00001", lat, lon, 35000, odd=0)
            odd_msg = make_df17_position("A00001", lat, lon, 35000, odd=1)
            beast_sock.sendall(beast_frame(even_msg, timestamp=ts))
            ts += 500
            beast_sock.sendall(beast_frame(odd_msg, timestamp=ts))
            ts += 500
            time.sleep(0.15)

            try:
                chunk = reduce_sock.recv(8192)
                if chunk:
                    received += chunk
            except socket.timeout:
                pass

        # Give extra time for output
        time.sleep(1)
        try:
            chunk = reduce_sock.recv(8192)
            if chunk:
                received += chunk
        except socket.timeout:
            pass

        beast_sock.close()
        reduce_sock.close()

        self.assertTrue(len(received) > 0,
                        "No data received from beast_reduce port")

        # Verify Beast framing: data contains 0x1A + type byte
        self.assertIn(b'\x1a', received,
                      "Beast escape byte (0x1a) not found in output")

        # Parse Beast frames: find 0x1A followed by type '3' (long message)
        frames_found = 0
        i = 0
        while i < len(received) - 1:
            if received[i] == 0x1A:
                if i + 1 < len(received) and received[i + 1] == 0x1A:
                    # Escaped 0x1A, skip
                    i += 2
                    continue
                type_byte = received[i + 1:i + 2]
                if type_byte in (b'1', b'2', b'3'):
                    frames_found += 1
                i += 2
            else:
                i += 1

        self.assertGreater(frames_found, 0,
                           "No valid Beast frames found in reduce output")

    def test_r2_beast_reduce_dedup(self):
        """Beast reduce deduplicates repeated messages."""
        reduce_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_reduce_port), timeout=5
        )
        reduce_sock.settimeout(3)

        beast_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        # Feed the SAME message many times rapidly
        msg = make_df17_position("B00001", 52.0, 1.0, 30000, odd=0)
        ts = 10000
        for _ in range(50):
            beast_sock.sendall(beast_frame(msg, timestamp=ts))
            ts += 1  # minimal time increment

        time.sleep(2)

        received = b""
        try:
            while True:
                chunk = reduce_sock.recv(8192)
                if not chunk:
                    break
                received += chunk
        except socket.timeout:
            pass

        beast_sock.close()
        reduce_sock.close()

        # Count output frames (0x1A followed by non-0x1A)
        output_frames = 0
        i = 0
        while i < len(received) - 1:
            if received[i] == 0x1A:
                if i + 1 < len(received) and received[i + 1] == 0x1A:
                    i += 2
                    continue
                if received[i + 1:i + 2] in (b'1', b'2', b'3'):
                    output_frames += 1
                i += 2
            else:
                i += 1

        # The "reduce" behavior should output significantly fewer frames
        # than the 50 we sent. It might output 0 if the message is fully
        # deduplicated, or a small number. In any case, it should be < 50.
        self.assertLess(output_frames, 50,
                        f"Beast reduce did not deduplicate: got {output_frames} frames for 50 inputs")


# ===================================================================
# S: Beast Output Format
# ===================================================================

class TestBeastOutputFormat(unittest.TestCase):
    """Verify Beast output wire format and escaping."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            beast_in=True, beast_out=True,
            extra_args=["--json-reliable", "1"],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _collect_beast_output(self, input_msgs, timeout=8):
        """Feed Beast input and collect Beast output."""
        out_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_out_port), timeout=5
        )
        out_sock.settimeout(3)

        in_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        for raw_frame in input_msgs:
            in_sock.sendall(raw_frame)
            time.sleep(0.05)

        # Collect output
        received = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                chunk = out_sock.recv(8192)
                if chunk:
                    received += chunk
                else:
                    break
            except socket.timeout:
                if received:
                    break

        in_sock.close()
        out_sock.close()
        return received

    @staticmethod
    def _unescape_beast(data):
        """Remove Beast escaping (doubled 0x1A -> single 0x1A)."""
        result = bytearray()
        i = 0
        while i < len(data):
            if data[i] == 0x1A and i + 1 < len(data) and data[i + 1] == 0x1A:
                result.append(0x1A)
                i += 2
            else:
                result.append(data[i])
                i += 1
        return bytes(result)

    @staticmethod
    def _parse_beast_frames(data):
        """Parse Beast frames from raw data. Returns list of (type, payload)."""
        frames = []
        i = 0
        while i < len(data) - 1:
            if data[i] == 0x1A and i + 1 < len(data):
                if data[i + 1] == 0x1A:
                    i += 2
                    continue
                frame_type = data[i + 1]
                # Determine expected payload length (6 ts + 1 signal + msg)
                if frame_type == ord('3'):
                    msg_len = 14
                elif frame_type == ord('2'):
                    msg_len = 7
                elif frame_type == ord('1'):
                    msg_len = 2
                else:
                    i += 2
                    continue

                # Extract payload (may contain escaped bytes)
                payload = bytearray()
                j = i + 2
                while j < len(data) and len(payload) < 6 + 1 + msg_len:
                    if data[j] == 0x1A and j + 1 < len(data):
                        if data[j + 1] == 0x1A:
                            payload.append(0x1A)
                            j += 2
                            continue
                        else:
                            break  # Start of next frame
                    payload.append(data[j])
                    j += 1

                if len(payload) == 6 + 1 + msg_len:
                    msg = bytes(payload[7:])  # Skip timestamp + signal
                    frames.append((chr(frame_type), msg))
                i = j
            else:
                i += 1
        return frames

    def test_s1_beast_passthrough(self):
        """DF17 fed via Beast input appears in Beast output."""
        input_frames = []
        ts = 50000
        for batch in range(10):
            lat = 51.5 + batch * 0.002
            lon = -0.1 + batch * 0.002
            even = make_df17_position("C00001", lat, lon, 35000, odd=0)
            odd = make_df17_position("C00001", lat, lon, 35000, odd=1)
            input_frames.append(beast_frame(even, timestamp=ts))
            ts += 500
            input_frames.append(beast_frame(odd, timestamp=ts))
            ts += 500

        received = self._collect_beast_output(input_frames)

        self.assertTrue(len(received) > 0,
                        "No data from beast output port")

        # Parse frames and check for ICAO C00001
        frames = self._parse_beast_frames(received)
        icao_found = False
        for ftype, msg_bytes in frames:
            if ftype == '3' and len(msg_bytes) == 14:
                # ICAO is bytes 1-3 of DF17 message
                icao = (msg_bytes[1] << 16) | (msg_bytes[2] << 8) | msg_bytes[3]
                if icao == 0xC00001:
                    icao_found = True
                    break

        self.assertTrue(icao_found,
                        "ICAO C00001 not found in Beast output frames")

    def test_s2_beast_escape_passthrough(self):
        """Beast output correctly escapes 0x1A bytes in data."""
        input_frames = []
        ts = 70000
        for batch in range(10):
            lat = 51.5 + batch * 0.002
            lon = -0.1 + batch * 0.002
            even = make_df17_position("1A1A1A", lat, lon, 35000, odd=0)
            odd = make_df17_position("1A1A1A", lat, lon, 35000, odd=1)
            input_frames.append(beast_frame(even, timestamp=ts))
            ts += 500
            input_frames.append(beast_frame(odd, timestamp=ts))
            ts += 500

        received = self._collect_beast_output(input_frames)

        self.assertTrue(len(received) > 0,
                        "No data from beast output port for 0x1A ICAO")

        # Verify that 0x1A bytes in message data are properly escaped
        # (doubled). Every raw 0x1A in the body should appear as 0x1A 0x1A.
        # Parse and verify at least one frame decodes the ICAO correctly.
        frames = self._parse_beast_frames(received)
        icao_found = False
        for ftype, msg_bytes in frames:
            if ftype == '3' and len(msg_bytes) == 14:
                icao = (msg_bytes[1] << 16) | (msg_bytes[2] << 8) | msg_bytes[3]
                if icao == 0x1A1A1A:
                    icao_found = True
                    break

        self.assertTrue(icao_found,
                        "ICAO 1A1A1A not found after un-escaping Beast output")


# ===================================================================
# T: Beast Reduce Filter by Altitude
# ===================================================================

class TestBeastReduceFilterAlt(unittest.TestCase):
    """Test --net-beast-reduce-filter-alt removes high aircraft."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(
            beast_in=True, beast_reduce=True,
            extra_args=[
                "--json-reliable", "1",
                "--lat", "51.5", "--lon", "-0.1",
                "--net-beast-reduce-filter-alt", "20000",
            ],
        )
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def test_t1_beast_reduce_filter_alt(self):
        """Aircraft above filter altitude excluded from beast-reduce output."""
        reduce_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_reduce_port), timeout=5
        )
        reduce_sock.settimeout(5)

        beast_sock = socket.create_connection(
            ("127.0.0.1", self.inst.beast_in_port), timeout=5
        )

        # Feed two aircraft:
        # D00001 at 15000ft (below filter) — should appear in reduce
        # D00002 at 35000ft (above filter) — should be excluded
        ts = 100000
        for batch in range(25):
            lat = 51.5 + batch * 0.001
            lon = -0.1 + batch * 0.001
            # Low aircraft
            even_low = make_df17_position("D00001", lat, lon, 15000, odd=0)
            odd_low = make_df17_position("D00001", lat, lon, 15000, odd=1)
            beast_sock.sendall(beast_frame(even_low, timestamp=ts))
            ts += 500
            beast_sock.sendall(beast_frame(odd_low, timestamp=ts))
            ts += 500
            # High aircraft
            even_high = make_df17_position("D00002", lat + 1, lon + 1, 35000, odd=0)
            odd_high = make_df17_position("D00002", lat + 1, lon + 1, 35000, odd=1)
            beast_sock.sendall(beast_frame(even_high, timestamp=ts))
            ts += 500
            beast_sock.sendall(beast_frame(odd_high, timestamp=ts))
            ts += 500
            time.sleep(0.15)

        # Collect output
        time.sleep(2)
        received = b""
        try:
            while True:
                chunk = reduce_sock.recv(8192)
                if not chunk:
                    break
                received += chunk
        except socket.timeout:
            pass

        beast_sock.close()
        reduce_sock.close()

        # Parse Beast frames and extract ICAOs
        icaos_seen = set()
        i = 0
        while i < len(received) - 1:
            if received[i] == 0x1A:
                if i + 1 < len(received) and received[i + 1] == 0x1A:
                    i += 2
                    continue
                frame_type = received[i + 1]
                if frame_type == ord('3'):
                    # Extract payload un-escaping
                    payload = bytearray()
                    j = i + 2
                    while j < len(received) and len(payload) < 6 + 1 + 14:
                        if received[j] == 0x1A and j + 1 < len(received):
                            if received[j + 1] == 0x1A:
                                payload.append(0x1A)
                                j += 2
                                continue
                            else:
                                break
                        payload.append(received[j])
                        j += 1
                    if len(payload) == 21:
                        msg = payload[7:]
                        icao = (msg[1] << 16) | (msg[2] << 8) | msg[3]
                        icaos_seen.add(icao)
                    i = j
                else:
                    i += 2
            else:
                i += 1

        # The low aircraft should appear; the high aircraft should be filtered
        self.assertIn(0xD00001, icaos_seen,
                      "Low aircraft D00001 not found in reduce output")
        self.assertNotIn(0xD00002, icaos_seen,
                         "High aircraft D00002 should be filtered by alt limit")


if __name__ == "__main__":
    unittest.main(verbosity=2)
