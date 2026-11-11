import io
import pathlib
import sqlite3
import subprocess
import sys
import tempfile
import unittest
import zlib
ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from gateway import Gateway, serve

def frame(seq=1, temp=2200):
    payload=f"DATA 000000000001 {seq} {temp} 5000".encode()
    return payload+f" {zlib.crc32(payload):08x}\n".encode()

class Tests(unittest.TestCase):
    def test_durable_dedup_restart(self):
        with tempfile.TemporaryDirectory() as temp:
            db=str(pathlib.Path(temp)/"gateway.sqlite")
            g=Gateway(db)
            self.assertEqual(g.accept(frame()),b"ACK 1\n")
            g.close()
            g=Gateway(db)
            self.assertEqual(g.accept(frame()),b"ACK 1\n")
            self.assertEqual(g.db.execute("SELECT count(*) FROM readings").fetchone()[0],1)
            self.assertIsNone(g.accept(frame(temp=2300)))
            g.close()
    def test_malformed_and_crc(self):
        g=Gateway(":memory:")
        for value in [b"",b"x"*100,frame(0),frame(4294967296),frame(temp=-4500),frame().replace(b"2200",b"2201")]:
            self.assertIsNone(g.accept(value))
        g.close()
    def test_stream_resync(self):
        g=Gateway(":memory:"); output=io.BytesIO()
        serve(io.BytesIO(b"x"*200+b"\n"+frame()),output,g)
        self.assertEqual(output.getvalue(),b"ACK 1\n");g.close()
    def test_no_ack_when_storage_fails(self):
        g=Gateway(":memory:");g.db.execute("PRAGMA query_only=ON")
        with self.assertRaises(sqlite3.OperationalError):g.accept(frame())
        g.close()
    def test_cpp_to_python_process_and_restart(self):
        frames=subprocess.check_output([str(ROOT/".build/simulate"),"--frames"])
        with tempfile.TemporaryDirectory() as temp:
            db=str(pathlib.Path(temp)/"db.sqlite")
            command=[sys.executable,str(ROOT/"tools/gateway.py"),"--db",db]
            for _ in range(2):
                result=subprocess.run(command,input=frames,stdout=subprocess.PIPE,check=True)
                self.assertEqual(result.stdout,b"ACK 1\nACK 2\nACK 2\nACK 3\n")
            with sqlite3.connect(db) as con:self.assertEqual(con.execute("SELECT count(*) FROM readings").fetchone()[0],3)
if __name__=="__main__":unittest.main(verbosity=2)
