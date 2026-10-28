"""Compare engine states and real VCD value changes across activity levels."""
from pathlib import Path
import subprocess
import tempfile

binary = str(Path(__file__).resolve().parents[1] / 'dtp_ca_sim')
with tempfile.TemporaryDirectory() as tmp:
    for n, k, r in [(1, 0, 1), (1, 1, 4), (32, 1, 1), (32, 8, 12), (32, 32, 3)]:
        run = subprocess.run([binary, '200', str(n), str(k), str(r), '--vcd'],
                             cwd=tmp, text=True, capture_output=True, check=True)
        assert 'PASS' in run.stdout, run.stdout
        poll = (Path(tmp) / 'wave_poll.vcd').read_bytes()
        dispatch = (Path(tmp) / 'wave_dispatch.vcd').read_bytes()
        assert poll == dispatch, (n, k, r, 'wave mismatch')
        if k:
            assert b'#' in poll, 'VCD has declarations but no time samples'
        print(f'PASS: cycles=200 N={n} K={k} R={r}, exact final state + VCD')
    for args in [('0', '32', '1', '1'), ('10', '0', '1', '1'),
                 ('10', '32', '33', '1'), ('10', '32', '1', '0')]:
        result = subprocess.run([binary, *args], cwd=tmp, capture_output=True)
        assert result.returncode == 2, args
print('PASS: invalid dimensions rejected')
