/*
 *  This file is part of Permafrost Engine. 
 *  Copyright (C) 2020 Eduard Permyakov 
 *
 *  Permafrost Engine is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Permafrost Engine is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 *  Linking this software statically or dynamically with other modules is making 
 *  a combined work based on this software. Thus, the terms and conditions of 
 *  the GNU General Public License cover the whole combination. 
 *  
 *  As a special exception, the copyright holders of Permafrost Engine give 
 *  you permission to link Permafrost Engine with independent modules to produce 
 *  an executable, regardless of the license terms of these independent 
 *  modules, and to copy and distribute the resulting executable under 
 *  terms of your choice, provided that you also meet, for each linked 
 *  independent module, the terms and conditions of the license of that 
 *  module. An independent module is a module which is not derived from 
 *  or based on Permafrost Engine. If you modify Permafrost Engine, you may 
 *  extend this exception to your version of Permafrost Engine, but you are not 
 *  obliged to do so. If you do not wish to do so, delete this exception 
 *  statement from your version.
 *
 */

#include "py_region.h"
#include "py_entity.h"
#include "../lib/public/pf_string.h"
#include "../lib/public/khash.h"
#include "../game/public/game.h"

#include <assert.h>


#define ARR_SIZE(a) (sizeof(a)/sizeof(a[0]))

KHASH_MAP_INIT_STR(PyObject, PyObject*)

typedef struct {
    PyObject_HEAD
    enum region_type type;
    const char *name;
}PyRegionObject;

static PyObject *PyRegion_new(PyTypeObject *type, PyObject *args, PyObject *kwds);
static void      PyRegion_dealloc(PyRegionObject *self);

static PyObject *PyRegion_curr_ents(PyRegionObject *self);
static PyObject *PyRegion_contains(PyRegionObject *self, PyObject *args);
static PyObject *PyRegion_pickle(PyRegionObject *self, PyObject *args, PyObject *kwargs);
static PyObject *PyRegion_unpickle(PyObject *cls, PyObject *args, PyObject *kwargs);

static PyObject *PyRegion_get_pos(PyRegionObject *self, void *closure);
static int       PyRegion_set_pos(PyRegionObject *self, PyObject *value, void *closure);

/*****************************************************************************/
/* STATIC VARIABLES                                                          */
/*****************************************************************************/

static PyMethodDef PyRegion_methods[] = {
    {"curr_ents", 
    (PyCFunction)PyRegion_curr_ents, METH_NOARGS,
    "Get a list of all the entities currently within the region."},

    {"contains", 
    (PyCFunction)PyRegion_contains, METH_VARARGS,
    "Returns True if the specified entity is currently within the region."},

    {"__pickle__", 
    (PyCFunction)PyRegion_pickle, METH_KEYWORDS,
    "Serialize a Permafrost Engine region object to a string."},

    {"__unpickle__", 
    (PyCFunction)PyRegion_unpickle, METH_VARARGS | METH_KEYWORDS | METH_CLASS,
    "Create a new pf.Region instance from a string earlier returned from a __pickle__ method."
    "Returns a tuple of the new instance and the number of bytes consumed from the stream."},

    {NULL}  /* Sentinel */
};

static PyGetSetDef PyRegion_getset[] = {
    {"position",
    (getter)PyRegion_get_pos, (setter)PyRegion_set_pos,
    "The current worldspace position of the region", 
    NULL},
    {NULL}  /* Sentinel */
};

static PyTypeObject PyRegion_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "pf.Region",               /* tp_name */
    sizeof(PyRegionObject),    /* tp_basicsize */
    0,                         /* tp_itemsize */
    (destructor)PyRegion_dealloc, /* tp_dealloc */
    0,                         /* tp_print */
    0,                         /* tp_getattr */
    0,                         /* tp_setattr */
    0,                         /* tp_reserved */
    0,                         /* tp_repr */
    0,                         /* tp_as_number */
    0,                         /* tp_as_sequence */
    0,                         /* tp_as_mapping */
    0,                         /* tp_hash  */
    0,                         /* tp_call */
    0,                         /* tp_str */
    0,                         /* tp_getattro */
    0,                         /* tp_setattro */
    0,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /* tp_flags */
    "Permafrost Engine region object.                               \n" 
    "                                                               \n"
    "The regions takes the following (mandatory) keyword arguments  \n"
    "in its' constructor:                                           \n"
    "                                                               \n"
    "  - type {pf.REGION_CIRCLE, pf.REGION_RECTANGLE}               \n"
    "  - name (string)                                              \n"
    "  - position (tuple of 2 floats)                               \n"
    "                                                               \n"
    "In addition, it takes the following arguments depending on the \n"
    "type:                                                          \n"
    "                                                               \n"
    "  - radius (float) [circle regions only]                       \n"
    "  - dimensions (tuple of 2 floats) [rectangular regions only]  \n"
    ,                          /* tp_doc */
    0,                         /* tp_traverse */
    0,                         /* tp_clear */
    0,                         /* tp_richcompare */
    0,                         /* tp_weaklistoffset */
    0,                         /* tp_iter */
    0,                         /* tp_iternext */
    PyRegion_methods,          /* tp_methods */
    0,                         /* tp_members */
    PyRegion_getset,           /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,                         /* tp_init */
    0,                         /* tp_alloc */
    PyRegion_new,              /* tp_new */
};

static khash_t(PyObject) *s_name_pyobj_table;

/*****************************************************************************/
/* STATIC FUNCTIONS                                                          */
/*****************************************************************************/

static PyObject *PyRegion_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = {"type", "name", "position", "radius", "dimensions", NULL};

    enum region_type regtype;
    const char *name;
    vec2_t position;

    float radius;
    vec2_t dims;

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "is(ff)|f(ff)", kwlist, &regtype, &name, 
        &position.x, &position.z, &radius, &dims.x, &dims.z)) {
        assert(PyErr_Occurred());
        return NULL;
    }

    if(regtype != REGION_CIRCLE && regtype != REGION_RECTANGLE) {
        PyErr_SetString(PyExc_TypeError, 
            "regtype keyword argument must be one of {pf.REGION_CIRCLE, pf.REGION_RECTANGLE}.");
        return NULL;
    }

    if(regtype == REGION_CIRCLE 
    && (PyDict_GetItemString(kwds, "dimensions") || !PyDict_GetItemString(kwds, "radius"))) {
        PyErr_SetString(PyExc_TypeError, "CIRCLE regions must have a radius but no dimensions.");
        return NULL;
    }

    if(regtype == REGION_RECTANGLE
    && (!PyDict_GetItemString(kwds, "dimensions") || PyDict_GetItemString(kwds, "radius"))) {
        PyErr_SetString(PyExc_TypeError, "RECTANGLE regions must have dimensions but no radius.");
        return NULL;
    }

    PyRegionObject *self = (PyRegionObject*)type->tp_alloc(type, 0);
    if(!self) {
        assert(PyErr_Occurred());
        return NULL;
    }

    const char *copy = pf_strdup(name);
    if(!name) {
        Py_DECREF(self);
        return PyErr_NoMemory();
    }

    self->type = regtype;
    self->name = copy;

    bool status = false;
    switch(regtype) {
    case REGION_CIRCLE:
        status = G_Region_AddCircle(name, position, radius);
        break;
    case REGION_RECTANGLE:
        status = G_Region_AddRectangle(name, position, dims.x, dims.z);
        break;
    default: assert(0);
    }

    if(!status) {
        char errbuff[256];
        pf_snprintf(errbuff, sizeof(errbuff), "Unable to create region (%s) of type (%d)\n", 
            copy, regtype);
        PyErr_SetString(PyExc_RuntimeError, errbuff);

        free((void*)copy);
        Py_DECREF(self);
        return NULL;
    }

    int ret;
    khiter_t k = kh_put(PyObject, s_name_pyobj_table, self->name, &ret);
    assert(ret != -1 && ret != 0);
    kh_value(s_name_pyobj_table, k) = (PyObject*)self;

    return (PyObject*)self;
}

static void PyRegion_dealloc(PyRegionObject *self)
{
    khiter_t k = kh_get(PyObject, s_name_pyobj_table, self->name);
    assert(k != kh_end(s_name_pyobj_table));
    kh_del(PyObject, s_name_pyobj_table, k);

    G_Region_Remove(self->name);
    free((void*)self->name);
    Py_DECREF(self);
}

static PyObject *PyRegion_curr_ents(PyRegionObject *self)
{
    struct entity *ents[512];
    size_t nents = G_Region_GetEnts(self->name, ARR_SIZE(ents), ents);

    PyObject *ret = PyList_New(0);
    if(!ret)
        return NULL;

    for(int i = 0; i < nents; i++) {
        PyObject *ent = S_Entity_ObjForUID(ents[i]->uid);
        if(!ent)
            continue;

        if(0 != PyList_Append(ret, ent)) {
            Py_DECREF(ret);
            return NULL;
        }
    }
    return ret;
}

static PyObject *PyRegion_contains(PyRegionObject *self, PyObject *args)
{
    PyObject *obj;

    if(!PyArg_ParseTuple(args, "O", &obj)
    || !S_Entity_Check(obj)) {
        PyErr_SetString(PyExc_TypeError, "Argument must be a single pf.Entity instance.");
        return NULL;
    }

    uint32_t uid = 0;
    S_Entity_UIDForObj(obj, &uid);

    if(G_Region_ContainsEnt(self->name, uid))
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}

static PyObject *PyRegion_pickle(PyRegionObject *self, PyObject *args, PyObject *kwargs)
{
    return NULL;
}

static PyObject *PyRegion_unpickle(PyObject *cls, PyObject *args, PyObject *kwargs)
{
    return NULL;
}

static PyObject *PyRegion_get_pos(PyRegionObject *self, void *closure)
{
    vec2_t pos = {0};
    G_Region_GetPos(self->name, &pos);
    return Py_BuildValue("ff", pos.x, pos.z);
}

static int PyRegion_set_pos(PyRegionObject *self, PyObject *value, void *closure)
{
    if(!PyTuple_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "Argument must be a tuple.");
        return -1;
    }
    
    vec2_t newpos;
    if(!PyArg_ParseTuple(value, "ff", &newpos.x, &newpos.z)) {
        return -1;
    }

    G_Region_SetPos(self->name, newpos);
    return 0;
}

/*****************************************************************************/
/* EXTERN FUNCTIONS                                                          */
/*****************************************************************************/

void S_Region_PyRegister(PyObject *module)
{
    if(PyType_Ready(&PyRegion_type) < 0)
        return;
    Py_INCREF(&PyRegion_type);
    PyModule_AddObject(module, "Region", (PyObject*)&PyRegion_type);
}

bool S_Region_Init(void)
{
    s_name_pyobj_table = kh_init(PyObject);
    return (s_name_pyobj_table != NULL);
}

void S_Region_Shutdown(void)
{
    kh_destroy(PyObject, s_name_pyobj_table);
}

void S_Region_NotifyContentsChanged(const char *name)
{
    khiter_t k = kh_get(PyObject, s_name_pyobj_table, name);
    if(k == kh_end(s_name_pyobj_table))
        return;

    PyObject *reg = kh_value(s_name_pyobj_table, k);
    PyObject *ret = PyObject_CallMethod(reg, "on_contents_changed", NULL);
    if(!ret) {
        PyErr_Clear();
    }
    Py_XDECREF(ret);
}
