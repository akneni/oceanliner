import subprocess
import time
import atexit
import sys
import os
import shutil

if '--delete-rsm-state' in sys.argv:
    for i in range(5):
        f = f'logfile-{i}.bin'
        if os.path.exists(f):
            os.remove(f)
        
        f = f'kv-store-{i}.bin'
        if os.path.exists(f):
            shutil.rmtree(f)


for i in range(5):
    f = f'raft-process-{i}.log'
    if os.path.exists(f):
        os.remove(f)

build_cmd = ['kiln', 'build']
build_mode = 'debug'

if '--release' in sys.argv:
    build_mode = 'release'
    build_cmd.append('--release')

if '--build' in sys.argv:
    c = subprocess.run(build_cmd)
    if c.returncode != 0:
        print('compilation command failed')
        exit(0)

procs = []

for i in range(5):
    if i == 0 and '--skip-first' in sys.argv:
        continue

    c = f"build/{build_mode}/oceanliner {i} logfile-{i}.bin".split()

    if (i == 0 and '--valgrind-first' in sys.argv) or '--valgrind-all' in sys.argv:
        c = ['valgrind', '--leak-check=full'] + c

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
    


