import subprocess
import time
import atexit
import sys
import os

if '--delete-rsm-state' in sys.argv:
    for i in range(5):
        f = f'logfile-{i}.bin'
        if os.path.exists(f):
            os.remove(f)

for i in range(5):
    f = f'raft-process-{i}.log'
    if os.path.exists(f):
        os.remove(f)

procs = []

for i in range(5):
    if i == 0 and '--skip-first' in sys.argv:
        continue

    c = f"build/debug/oceanliner {i} logfile-{i}.bin".split()

    if i == 0:
        cmd = subprocess.Popen(c)
    else:
        fp = open(f'raft-process-{i}.log', 'wb')
        cmd = subprocess.Popen(c, stdout=fp, stderr=fp)
    procs.append(cmd)

    def kill_proc():
        cmd.kill()
    atexit.register(kill_proc)

    time.sleep(0.5)


for p in procs:
    p.wait()
    print('\n\n\n')
    print('PYTHON: ', p)
    


