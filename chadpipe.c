#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#define STR1(x) #x
#define STR(x) STR1(x)

#define CAT1(a,b) a##b
#define CAT(a,b) CAT1(a,b)

#define MODULE_NAME chadpipe

#define ERROR_PREF __FILE__ ":" STR(__LINE__) ": "

const char* cstr(PyObject* obj, Py_ssize_t* len) {
  const char* str = PyUnicode_AsUTF8AndSize(obj,len);
  if (str) return str;
  str = PyBytes_AsString(obj);
  if (str) {
    PyErr_Clear();
    *len = PyBytes_GET_SIZE(obj);
    return str;
  }
  PyErr_SetString(PyExc_TypeError,
    "expected a unicode or bytes string");
  return NULL;
}

typedef struct {
  PyObject_HEAD
  unsigned nargs;
  char*** args;
} pipe_args;

// static
// PyObject* pipe_new(PyTypeObject* type, PyObject* args, PyObject* kwargs) {
//   pipe* self = (pipe*) type->tp_alloc(type, 0);
//   return (PyObject*) self;
// }

static
void pipe_dealloc(pipe_args* self) {
  if (self->args) {
    for (unsigned i=0; i<self->nargs; ++i) {
      char** a = self->args[i];
      if (!a) break;
      for (char** b=a; *b; ++b) {
        // printf("free \"%s\"\n",*b);
        free(*b);
      }
      // printf("free %u\n",i);
      free(a);
    }
    free(self->args);
  }
  Py_TYPE(self)->tp_free((PyObject*)self);
}

static
int pipe_init(pipe_args* self, PyTupleObject* targs, PyObject* kwargs) {
  const unsigned nargs = Py_SIZE(targs);
  if (nargs < 1) {
    PyErr_SetString(PyExc_ValueError,
      ERROR_PREF "pipe requires at least 1 argument");
    return -1;
  }
  PyObject** const args = targs->ob_item;
  self->nargs = nargs;

  // reserve args
  char*** exec_args = self->args = malloc(sizeof(char**)*nargs);
  for (unsigned i=0; i<nargs; ++i)
    exec_args[i] = NULL;

  for (unsigned ai=0; ai<nargs; ++ai) {
    PyObject* iter = PyObject_GetIter(args[ai]);
    if (!iter) {
      // PyErr_SetString(PyExc_TypeError,
      //   ERROR_PREF "invalid pipe argument, not iterable");
      goto err;
    }

    unsigned i=0, cap=0;
    for (PyObject* item; (item = PyIter_Next(iter)); ++i) {
      Py_ssize_t len = 0;
      const char* str = cstr(item,&len);
      if (!str) {
        PyErr_SetString(PyExc_TypeError,
          ERROR_PREF "all exec args must be strings");
        goto err_item;
      }

      if (cap < i+2) { // extend args capacity
        *exec_args = *exec_args
          ? realloc(*exec_args, sizeof(char*)*(cap <<= 1))
          : malloc(sizeof(char*)*(cap = 2));
        for (unsigned j=i; j<cap; ++j)
          (*exec_args)[j] = NULL;
      }

      char* const arg = (*exec_args)[i] = malloc(len+1);
      memcpy(arg,str,len+1);

      Py_DECREF(item);
      continue;

err_item:
      Py_DECREF(item);
      Py_DECREF(iter);
      goto err;
    }
    if (i==0) {
      PyErr_SetString(PyExc_ValueError,
        ERROR_PREF "pipe argument with zero values");
      goto err;
    }

    ++exec_args;
    Py_DECREF(iter);
  }

  return 0;

err:
  return -1;
  // Note: Python calls dealloc even if init returns -1
}

static
PyObject* pipe_str(pipe_args* self) {
  // TODO: handle quotes
  // TODO: efficiently merge strings
  PyObject* str = PyUnicode_FromString("");
  for (unsigned i=0; i<self->nargs; ++i) {
    bool first = true;
    for (char **argv = self->args[i], *arg; (arg=*argv); ++argv) {
      const bool q = !*arg || strpbrk(arg," \t\n\r"); // need to quote

      PyObject* str2 = PyUnicode_FromFormat(
        "%U%s%s%s%s", str, (first ? (i?" | ":"") : " "),
        (q?"'":""), arg, (q?"'":"")
      );

      Py_DECREF(str);
      str = str2;
      if (first) first = false;
    }
  }
  return str;
}

// bool all_ascii(const char* s, size_t n) {
//   for (size_t i=0; i<n; ++i)
//     if (s[i] < 0) return false;
//   return true;
// }

enum source_type_enum { source_none, source_str };

static
PyObject* pipe_call(pipe_args* self, PyTupleObject* targs, PyObject* kwargs) {
  const unsigned nargs = Py_SIZE(targs);
  if (nargs > 1) {
    PyErr_SetString(PyExc_ValueError,
      ERROR_PREF "pipe called with more than 1 positional argument");
    return NULL;
  }
  PyObject** const args = targs->ob_item;

  // validate first argument
  enum source_type_enum source_type = source_none;
  PyObject* source = nargs ? args[0] : NULL;
  if (source) {
    if (source == Py_None) { // None
      source = NULL;
    } else if (PyUnicode_Check(source) || PyBytes_Check(source)) { // str
      source_type = source_str;
    } else {
      PyErr_SetString(PyExc_TypeError,
        ERROR_PREF "unexpected source argument type");
      return NULL;
    }
  }

  char delim;
  bool delim_set = false;
  if (kwargs) {
    PyObject* d = PyDict_GetItemString(kwargs,"d");
    if (d && d!=Py_None) {
      if (PyLong_Check(d)) {
        const long x = PyLong_AsLong(d);
        if (0 <= x && x < 256) {
          delim = x;
          goto d_ok;
        }
      } else {
        Py_ssize_t len;
        const char* dstr = cstr(d,&len);
        if (dstr && len==1) {
          delim = *dstr;
          goto d_ok;
        }
      }
      PyErr_SetString(PyExc_ValueError,
        ERROR_PREF "d must represent a single byte character");
      return NULL;
d_ok:
      delim_set = true;
    }
  }

#define ERR(FCN) { \
  const int e = errno; \
  char msg[1<<8]; \
  snprintf( \
    msg, sizeof(msg), \
    ERROR_PREF FCN "(): [%d] %s", \
    e, strerror(e) \
  ); \
  PyErr_SetString(PyExc_RuntimeError,msg); \
  goto err; \
}

  // https://youtu.be/6xbLgZpOBi8
  // https://youtu.be/VzCawLzITh0

  const unsigned nprocs = self->nargs;
  pid_t* const pids = calloc(nprocs,sizeof(pid_t));
  // TODO: do I need all pids?

  int pipes[2][2]; // 0 - read end, 1 - write end
  if (pipe(pipes[0])) ERR("pipe")

  for (unsigned i=0; i<nprocs; ++i) {
    if (pipe(pipes[1])) ERR("pipe")
    const pid_t pid = fork();
    if (pid < 0) ERR("fork")
    if (pid == 0) { // this is the child process
      if (dup2(pipes[0][0], STDIN_FILENO ) < 0) ERR("dup2")
      if (dup2(pipes[1][1], STDOUT_FILENO) < 0) ERR("dup2")
      // Note: dup2 doesn't close original fd

      // Note: fds are open separately in both processes
      close(pipes[0][0]);
      close(pipes[0][1]);
      close(pipes[1][0]);
      close(pipes[1][1]);

      // TODO: how to handle child process failure???

      if (execvp(self->args[i][0],self->args[i]) < 0) ERR("execvp")
      // child process is replaced by exec()
    }
    // original process
    pids[i] = pid;
    close(pipes[0][0]); // read end of input pipe
    close(pipes[1][1]); // write end of output pipe
    pipes[0][0] = pipes[1][0];
  }

  // this is the original process
  switch (source_type) {
    case source_none:
      break;
    case source_str:
      Py_ssize_t len = 0;
      const char* str = cstr(source,&len);
      if (write(pipes[0][1],str,len) < 0) ERR("write")
      break;
  }
  close(pipes[0][1]); // send EOF to input pipe

  if (!delim_set) {
    size_t bufcap = 1 << 8, buflen = 0;
    char* buf = malloc(bufcap);
    for (;;) {
      const size_t avail = bufcap-buflen-1;
      const ssize_t nread = read(pipes[0][0],buf+buflen,avail);
      if (nread == 0) break; // TODO: is this correct?
      if (nread < 0) {
        free(buf);
        ERR("read")
      }
      if (nread == avail)
        buf = realloc(buf, bufcap <<= 1);
      buflen += nread;
    }
    buf[buflen] = '\0';
    close(pipes[0][0]); // close read end of output pipe

    for (unsigned i=0; i<nprocs; ++i) // wait for all children
      waitpid(pids[i],NULL,0);
    free(pids);

    PyObject* str = PyUnicode_FromStringAndSize(buf,buflen);
    free(buf);
    return str;

  } else {
    // TODO: construct and return a generator
    Py_RETURN_NONE;
  }

err:
  free(pids);
  return NULL;
}

// static
// PyObject* pipe_fcn(hist* self, PyTupleObject* targs, PyObject* kwargs) {
// }
//
// static
// PyMethodDef pipe_methods[] = {
//   { "fcn", (PyCFunction) pipe_fcn, METH_VARARGS,
//     "description"
//   },
//   { NULL }
// };

static
PyTypeObject pipe_type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = STR(MODULE_NAME) ".pipe",
  .tp_doc = "Pipe object",
  .tp_basicsize = sizeof(pipe_args),
  .tp_itemsize = 0,
  .tp_flags = Py_TPFLAGS_DEFAULT,
  .tp_new = PyType_GenericNew,
  .tp_init = (initproc) pipe_init,
  .tp_dealloc = (destructor) pipe_dealloc,
  .tp_str = (reprfunc) pipe_str,
  .tp_call = (ternaryfunc) pipe_call,
  // .tp_members = pipe_members,
  // .tp_methods = pipe_methods,
};

// static PyMethodDef CAT(MODULE_NAME,_methods)[] = {
//   { NULL }
// };

static
PyModuleDef CAT(MODULE_NAME,_module) = {
  PyModuleDef_HEAD_INIT,
  .m_name = STR(MODULE_NAME),
  .m_doc = "Lay Unix pipes like a Chad",
  .m_size = -1,
  // .m_methods = CAT(MODULE_NAME,_methods),
};

PyMODINIT_FUNC CAT(PyInit_,MODULE_NAME)(void) {
  if (PyType_Ready(&pipe_type) < 0) return NULL;

  PyObject *m = PyModule_Create(&CAT(MODULE_NAME,_module));
  if (m == NULL) return NULL;

  Py_INCREF(&pipe_type);
  if (PyModule_AddObject(m,"pipe",(PyObject*)&pipe_type) < 0) {
    Py_DECREF(&pipe_type);
    Py_DECREF(m);
    return NULL;
  }

  return m;
}
