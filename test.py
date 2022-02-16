#!/usr/bin/env python3

from chadpipe import pipe

print(pipe(
    'test text',
    ['cat'],
    ['exe','-v'],
    ['exe','-v','two words'],
    ['exe',''],
    # ( str(i) for i in range(5) ),
    # ['exe','-v',1], # bad
    # None # bad
    # ( str(i) for i in range(0) ), # bad
))
