"""Compare 20 rising-edge accumulator values from RTL, SystemC and an oracle."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def trace(command, cwd):
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=True)
    return [(int(c), int(v)) for c, v in re.findall(r'^TRACE (\d+) (\d+)$', result.stdout, re.M)]
with tempfile.TemporaryDirectory() as tmp:
    rtl = trace(['vvp', str(root / 'rtl/dtp_sim.vvp')], tmp)
    sc = trace([str(root / 'dtp_systemc'), '20'], tmp)
    expected, accum = [], 0
    for cycle in range(1, 21):
        accum += cycle + (cycle & 3)
        expected.append((cycle, accum))
    assert rtl == expected, ('RTL mismatch', rtl, expected)
    assert sc == expected, ('SystemC mismatch', sc, expected)
    print(f'PASS: RTL == SystemC == reference, 20 cycles, final accum={accum}')
