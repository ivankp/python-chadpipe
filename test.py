#!/usr/bin/env python3

from chadpipe import pipe

p = pipe(
    'test text',
    ['cat'],
    ['exe','-v'],
    # ['exe','-v',1],
    # None # TODO: segfault
)
