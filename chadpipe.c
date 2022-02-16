#include <stdio.h>
#include <stdlib.h>
/* #include <stdarg.h> */
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

typedef struct {
  PyObject_HEAD
  PyObject* source;
  unsigned nexec;
  char*** args;
} pipe_obj;

// static
// PyObject* pipe_new(PyTypeObject* type, PyObject* args, PyObject* kwargs) {
//   pipe* self = (pipe*) type->tp_alloc(type, 0);
//   return (PyObject*) self;
// }

static
void pipe_dealloc(pipe_obj* self) {
  if (self->source) Py_DECREF(self->source);
  if (self->args) {
    for (unsigned i=0; i<self->nexec; ++i) {
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
int pipe_init(pipe_obj* self, PyTupleObject* targs, PyObject* kwargs) {
  const unsigned nargs = Py_SIZE(targs);
  if (nargs < 2) {
    PyErr_SetString(PyExc_ValueError,
      ERROR_PREF "pipe requires at least 2 arguments");
    return -1;
  }
  PyObject** const args = targs->ob_item;

  // validate first argument
  PyObject* const source = args[0];
  if (source == Py_None) {
    // no source, nothing to do nothing
  } else if (PyUnicode_Check(source)) {
    // source is a string
    Py_INCREF(source);
    self->source = source;
  } else {
    PyErr_SetString(PyExc_ValueError,
      ERROR_PREF "unexpected first argument type");
    return -1;
  }

  const unsigned nexec = self->nexec = nargs-1;

  // reserve args
  char*** exec_args = self->args = malloc(sizeof(char**)*nexec);
  for (unsigned i=0; i<nexec; ++i)
    exec_args[i] = NULL;

  for (unsigned ai=1; ai<nargs; ++ai) {
    PyObject* iter = PyObject_GetIter(args[ai]);
    if (!iter) {
      // PyErr_SetString(PyExc_ValueError,
      //   ERROR_PREF "invalid pipe argument, not iterable");
      goto err;
    }

    unsigned i=0, cap=0;
    for (PyObject* item; (item = PyIter_Next(iter)); ++i) {
      Py_ssize_t len = 0;
      const char* str = PyUnicode_AsUTF8AndSize(item,&len);
      if (!str) {
        PyErr_SetString(PyExc_ValueError,
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
PyObject* pipe_str(pipe_obj* self) {
  PyObject* str = PyUnicode_FromString("");
  for (unsigned i=0; i<self->nexec; ++i) {
    bool first = true;
    for (char** argv = self->args[i]; *argv; ++argv) {
      const bool q = !*argv || strpbrk(*argv," \t\n\r"); // need to quote

      PyObject* str2 = PyUnicode_FromFormat(
        "%U%s%s%s%s", str, (first ? (i?" | ":"") : " "),
        (q?"'":""), *argv, (q?"'":"")
      );

      Py_DECREF(str);
      str = str2;
      if (first) first = false;
    }
  }
  return str;
}

/*
static
int pipe_run(pipe_obj* self) {
  const unsigned n = self->nexec;
  for (unsigned i=0; i<n; ++i) {
    if (pipe(self->fd[i])) {
      const int e = errno;
      char msg[1<<8];
      snprintf(
        msg, sizeof(msg),
        ERROR_PREF "pipe(): [%d] %s",
        e, strerror(e)
      );
      PyErr_SetString(PyExc_ValueError,msg);
      return -1;
    }
  }
}
*/

// static
// PyObject* hist_fill(hist* self, PyTupleObject* targs, PyObject* kwargs) {
// }

static
PyMethodDef pipe_methods[] = {
  // { "fill", (PyCFunction) hist_fill, METH_VARARGS,
  //   "Fill histogram bin corresponding to the provided point"
  // },
  { NULL }
};

static
PyTypeObject pipe_type = {
  PyVarObject_HEAD_INIT(NULL, 0)
  .tp_name = STR(MODULE_NAME) ".pipe",
  .tp_doc = "Pipe object",
  .tp_basicsize = sizeof(pipe_obj),
  .tp_itemsize = 0,
  .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
  .tp_new = PyType_GenericNew,
  .tp_init = (initproc) pipe_init,
  .tp_dealloc = (destructor) pipe_dealloc,
  .tp_str = (reprfunc) pipe_str,
  // .tp_members = pipe_members,
  .tp_methods = pipe_methods,
};

/* static PyMethodDef CAT(MODULE_NAME,_methods)[] = { */
/*   { NULL } */
/* }; */

static
PyModuleDef CAT(MODULE_NAME,_module) = {
  PyModuleDef_HEAD_INIT,
  .m_name = STR(MODULE_NAME),
  .m_doc = "Lay unix pipes like a Chad",
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
