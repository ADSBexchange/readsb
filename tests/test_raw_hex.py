"""
Raw hex protocol integration tests.

G: Raw hex input via --net-ri-port
"""

import socket
import time
import unittest

from conftest import (
    ReadsbInstance, modes_checksum, poll_aircraft_json,
)


# ===================================================================
# G: Raw Hex Input
# ===================================================================

class TestRawHexInput(unittest.TestCase):
    """Feed raw hex messages on --net-ri-port, verify JSON output."""

    @classmethod
    def setUpClass(cls):
        cls.inst = ReadsbInstance(raw_in=True)
        cls.inst.__enter__()

    @classmethod
    def tearDownClass(cls):
        cls.inst.__exit__(None, None, None)

    def _raw_feeder(self):
        """Return a TCP connection to the raw-in port."""
        return socket.create_connection(
            ("127.0.0.1", self.inst.raw_in_port), timeout=5
        )

    def test_g1_raw_hex_df17(self):
        """DF17 as *HEXHEX...;\n on raw-in -> aircraft in JSON."""
        # Known-good DF17 (ICAO 4840D6)
        hex_msg = "8D4840D6202CC371C32CE0576098"
        raw_line = f"*{hex_msg};\n"
        sock = self._raw_feeder()
        for _ in range(5):
            sock.sendall(raw_line.encode())
            time.sleep(0.3)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="4840d6")
        self.assertIsNotNone(data, "aircraft.json never contained 4840d6")
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("4840d6", hexes)

    def test_g2_raw_hex_at_prefix(self):
        """@-prefixed raw hex (with 12-char timestamp) is accepted."""
        hex_msg = "8D40621D58C382D690C8AC2863A7"
        # @ + 12-char hex timestamp + hex message + ;
        raw_line = f"@000000000001{hex_msg};\n"
        sock = self._raw_feeder()
        for _ in range(5):
            sock.sendall(raw_line.encode())
            time.sleep(0.3)
        sock.close()

        data = poll_aircraft_json(self.inst.tmpdir, want_hex="40621d")
        self.assertIsNotNone(data, "aircraft.json never contained 40621d")
        hexes = {a["hex"] for a in data["aircraft"]}
        self.assertIn("40621d", hexes)

    def test_g3_raw_hex_malformed_no_crash(self):
        """Malformed hex input doesn't crash readsb."""
        sock = self._raw_feeder()
        bad_lines = [
            b"*;\n",               # empty message
            b"*ZZZZZZ;\n",         # non-hex characters
            b"*8D;\n",             # too short
            b"*8D4840D6202C;\n",   # truncated DF17
            b"garbage\n",          # no framing at all
            b"*" + b"FF" * 100 + b";\n",  # way too long
        ]
        for line in bad_lines:
            sock.sendall(line)
            time.sleep(0.1)
        sock.close()
        time.sleep(1)
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb crashed on malformed raw hex input")

    def test_g4_raw_hex_short_message(self):
        """7-byte (56-bit) Mode-S message via raw hex doesn't crash."""
        # Build a DF11 (all-call reply): 5D + ICAO(3 bytes) + PI(3 bytes)
        msg = bytearray(7)
        msg[0] = 0x5D
        msg[1] = 0xAB
        msg[2] = 0xCD
        msg[3] = 0xEF
        # Compute CRC and embed
        crc = modes_checksum(msg)
        msg[4] = (crc >> 16) & 0xFF
        msg[5] = (crc >> 8) & 0xFF
        msg[6] = crc & 0xFF
        hex_msg = msg.hex().upper()
        raw_line = f"*{hex_msg};\n"
        sock = self._raw_feeder()
        for _ in range(3):
            sock.sendall(raw_line.encode())
            time.sleep(0.2)
        sock.close()
        time.sleep(1)
        self.assertIsNone(self.inst.proc.poll(),
                          "readsb crashed on short Mode-S message")


if __name__ == "__main__":
    unittest.main(verbosity=2)
