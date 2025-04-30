import subprocess
import time
import atexit

procs = []

for i in range(5):
    c = f"kiln run -- {i} logfile-{i}.bin".split()

    if i == 0:
        cmd = subprocess.Popen(c)
    else:
        cmd = subprocess.Popen(c, stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    procs.append(cmd)

    def kill_proc():
        cmd.kill()
    atexit.register(kill_proc)

    time.sleep(1)


for p in procs:
    p.wait()
    print('\n\n\n')
    print('PYTHON: ', p)
    


