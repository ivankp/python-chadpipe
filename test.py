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

print(pipe(
    ['sed','s/a/A/'],
    ['sed','s/b/B/g']
)('abc\n'))

# print(pipe(
#     ['ping','-c','5','google.com'],
#     ['grep','^rtt'],
#     ['sed','s/=/:/']
# )())

# print(pipe(
#     ['seq','1000000000'],
#     ['head','-1']
# )()) # seq: write error: Broken pipe

print(pipe(
    ['seq','5'],
    ['head','-n','2']
)())

it = pipe(['seq','5000'])(d='\n',cap=15)
print(type(it))
for line in it:
    i = int(line)
    if i%100 == 0:
        print(line)

# for i,line in enumerate(pipe(['seq','5'])(d='\n')):
#     print(i,': ',line,sep='')
