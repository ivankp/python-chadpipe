# Chadpipe

The library provides a terse and flexible syntax for piping between processes
on Unix-like systems.

The motivation is to provide the ability to write chains of piped commands
in Python with very little clutter like in shell scripts, yet with full
flexibility of a general-purpoes language.
This is in contrast with the Python's
[subprocess](https://docs.python.org/3/library/subprocess.html)
library, with it's verbose syntax, particularly for piping between processes.

# Compatibility

The library is written in C for Linux and makes use of
[`fork()`](https://man7.org/linux/man-pages/man2/fork.2.html),
[`pipe()`](https://man7.org/linux/man-pages/man2/pipe.2.html),
[`dup2()`](https://man7.org/linux/man-pages/man2/dup.2.html), and
[`exec()`](https://man7.org/linux/man-pages/man3/exec.3.html),
functions declared in
[`<unistd.h>`](https://man7.org/linux/man-pages/man0/unistd.h.0p.html).

Compatibility with other systems is not guaranteed.

The library is for Python 3.

# Design and examples

The library is implemented in a single source file.
The primary class provided is `pipe`, which can be imported as follows,
```python
from chadpipe import pipe
```

The `pipe` class models a pipeline of execution and data processing rather
than a single Linux pipe created by the `pipe()` function.

The usage of `pipe` is split into two stages:
1. definition implemented via `pipe` object construction, and
2. execution via the call function.

A `pipe` can be defined as follows:
```python
p = pipe(['echo','test text'],['sed','s/t/T/g'])
```
This definition is similar to the following line of shell script:
```shell
echo 'test text' | sed 's/t/T/g'
```
In fact, the equivalent shell command can be obtained by converting the `pipe`
instance `p` into `str`.
```python
print(p)
```
will print
```text
echo 'test text' | sed s/t/T/g
```

A defined `pipe` is executed by calling it. The call returns what the last
process in the pipe writes to stdout as a single byte string.

The same instance of `pipe` can be executed multiple times.
The pipe object doesn't keep any state besides the list of arguments,
which is not modified by execution.

```python
print( pipe(['echo','test text'],['tr','t','T'])() )
```
prints
```text
b'TesT TexT\n'
```

```python
print( pipe(['echo','test text'],['tr','t','T'])().decode() )
```
prints
```text
TesT TexT

```

Instead of using `echo`, a string can be piped directly into the first process.
The input string is specified as a positional argument of the call function
and can be either a byte string or a unicode string.
```python
print( pipe(['tr','t','T'])('test text') )
```
prints
```text
b'TesT TexT'
```

# Additional options

A number of options can be specified using `kwargs` of the call function.

## Delimiter
If the `d` argument, accepting a single byte value, is provided, that byte is
used as a delimiter. In this case, instead of returning a single byte string,
the call function returns a generator, that can be iterated to get delimited
segments of output.

For example, the `pipe` output can be iterated over line-by-line as follows:
```python
for i,line in enumerate( pipe(['seq','3'])(d='\n') ):
    print(i,': ',line.decode(),sep='')
```
which prints
```text
0: 1
1: 2
2: 3
```

The behavior with `d=None` is the same as if the argument was not passed,
in which case the whole output is returned as one byte string directly rather
than being yielded via a generator.

## Initial buffer capacity
Either the call function or the iterator maintain a buffer into which the
`pipe` output is read.
By default, the initial size of that buffer is equal to the value returned by
`getpagesize()`, which on Linux is typically 4096 bytes.
The initial capacity of the buffer can be changed by passing a `cap` argument
to the call function.
The behavior with `cap=None` is the same as if the argument was not passed.
Whether `cap` is provided or not, if the buffer size is not large enough, it
is reallocated in powers of 2, starting at the initial capacity.

