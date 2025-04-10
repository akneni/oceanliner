import random
import sys


if len(sys.argv) <= 1:
    print("Expect argument for the number of rows to generate")
    exit()


num_cmds = int(sys.argv[1])
corpus = ''

for _ in range(num_cmds):
    key = ''.join(chr(random.randint(97, 97+25)) for i in range(random.randint(1, 100)))
    value = ','.join(str(random.randint(0, 255)) for _ in range(10, 1000))

    corpus += f"SET|{key}|{value}\n"

with open('assets/log-file-example-rand.txt', 'w') as f:
    f.write(corpus.strip())










