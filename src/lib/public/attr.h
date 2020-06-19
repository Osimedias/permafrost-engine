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

#ifndef ATTR_H
#define ATTR_H

#include "../../pf_math.h"
#include <stdbool.h>

struct SDL_RWops;

struct attr{
    char key[64];
    enum{
        TYPE_STRING,
        TYPE_FLOAT,
        TYPE_INT,
        TYPE_VEC2,
        TYPE_VEC3,
        TYPE_QUAT,
        TYPE_BOOL,
    }type;
    union{
        char   as_string[256];
        float  as_float;
        int    as_int;
        vec2_t as_vec2;
        vec3_t as_vec3;
        quat_t as_quat;
        bool   as_bool;
    }val;
};

/* 'named' attributes start with a single token for the name */
bool Attr_Parse(struct SDL_RWops *stream, struct attr *out, bool named);
bool Attr_Write(struct SDL_RWops *stream, const struct attr *in, const char name[static 0]);

#endif

