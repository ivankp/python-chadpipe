#!/usr/bin/env python3

from chadpipe import pipe

print(pipe(
    ['seq','5'],
    ['head','-n','2']
).open())
