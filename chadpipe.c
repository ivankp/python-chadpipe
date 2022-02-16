#include <stdio.h>
#include <stdlib.h>
/* #include <stdarg.h> */
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
  PyObject* input;
  unsigned nexec;
  int (*fd)[2];
  char*** args;
} pipe_obj;

// static
// PyObject* pipe_new(PyTypeObject* type, PyObject* args, PyObject* kwargs) {
//   pipe* self = (pipe*) type->tp_alloc(type, 0);
//   return (PyObject*) self;
// }

static
void pipe_dealloc(pipe_obj* self) {
  if (self->input) Py_DECREF(self->input);
  if (self->fd) free(self->fd);
  if (self->args) {
    for (unsigned i=0; i<self->nexec; ++i) {
      char** a = self->args[i];
      for (char** b=a; *b; ++b) {
        printf("free \"%s\"\n",*b);
        free(*b);
      }
      printf("free %u\n",i);
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

  PyObject* const arg0 = args[0];
  // TODO: allow None
  if (PyUnicode_Check(arg0)) {

  } else {
    PyErr_SetString(PyExc_ValueError,
      ERROR_PREF "unexpected first argument type");
    return -1;
  }

  // reserve pipes
  self->fd = malloc(sizeof(int[2])*nargs);
  for (unsigned i=0; i<nargs; ++i) {
    int* fd = self->fd[i];
    fd[0] = -1;
    fd[1] = -1;
  }

  const unsigned nexec = nargs-1; // TODO
  self->nexec = nexec;
  /* TODO: if (*arg == Py_None) */

  // reserve args
  char*** exec_args = self->args = malloc(sizeof(char**)*(nexec+1));
  for (unsigned i=0; i<=nexec; ++i)
    exec_args[i] = NULL;

  for (unsigned ei=0; ei<nexec; ++ei) {
    PyObject* iter = PyObject_GetIter(args[ei+1]);
    if (!iter) {
      if (!PyErr_Occurred())
        PyErr_SetString(PyExc_ValueError,
          ERROR_PREF "invalid argument: not iterable");
      goto err;
    }
    unsigned i=0, cap=0;
    for (PyObject* item; (item = PyIter_Next(iter)); ++i) {
      Py_ssize_t len = 0;
      const char* str = PyUnicode_AsUTF8AndSize(item,&len);
      if (!str) {
        PyErr_SetString(PyExc_ValueError,
          ERROR_PREF "all exec arguments must be strings");
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

      Py_DECREF(item); // TODO: make sure this is needed
      continue;

err_item:
      Py_DECREF(item); // TODO: make sure this is needed
      goto err;
    }
    ++exec_args;
  }
  // TODO: error if i < 1

  exec_args = self->args;
  for (unsigned i=0; i<nexec; ++i) {
    char** argv = exec_args[i];
    while (*argv) {
      printf("%s\n",*argv);
      ++argv;
    }
    printf("\n");
  }

  return 0;

err:
  return -1;
  // Note: Python calls destructor even if -1 is returned
}

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
