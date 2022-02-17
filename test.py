#!/usr/bin/env python3

from chadpipe import pipe

print(pipe(
    ['cat'],
    ['exe','-v'],
    ['exe','-v','two words'],
    ['exe',''],
    # ( str(i) for i in range(5) ),
    # ['exe','-v',1], # bad
    # None # bad
    # ( str(i) for i in range(0) ), # bad
))

print(pipe(['cat'])('test text '*2))

print(pipe(['sed','-e','s/t/T/g','-e','s/ex/Ex/g'])('test text'))
