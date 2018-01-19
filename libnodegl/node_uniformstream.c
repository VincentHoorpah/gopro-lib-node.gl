/*
 * Copyright 2018 GoPro Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "math_utils.h"
#include "nodegl.h"
#include "nodes.h"
#include "transforms.h"

#define OFFSET(x) offsetof(struct uniformstream, x)
static const struct node_param uniformstreamvec4_params[] = {
    {"data",  PARAM_TYPE_DATA, OFFSET(data), .flags=PARAM_FLAG_CONSTRUCTOR},
    {"update_interval", PARAM_TYPE_DBL, OFFSET(update_interval), .flags=PARAM_FLAG_CONSTRUCTOR},
    {NULL}
};

static const struct node_param uniformstreamquat_params[] = {
    {"data",  PARAM_TYPE_DATA, OFFSET(data), .flags=PARAM_FLAG_CONSTRUCTOR},
    {"update_interval", PARAM_TYPE_DBL, OFFSET(update_interval), .flags=PARAM_FLAG_CONSTRUCTOR},
    {NULL}
};


static const struct node_param uniformstreammat4_params[] = {
    {"data",  PARAM_TYPE_DATA, OFFSET(data), .flags=PARAM_FLAG_CONSTRUCTOR},
    {"update_interval", PARAM_TYPE_DBL, OFFSET(update_interval), .flags=PARAM_FLAG_CONSTRUCTOR},
    {NULL}
};

static int uniformstream_init(struct ngl_node *node)
{
    struct uniformstream *s = node->priv_data;

    switch(node->class->id) {
    case NGL_NODE_UNIFORMSTREAMVEC4:
        s->data_comp = 4;
        s->data_stride = s->data_comp * sizeof(float);
        break;
    case NGL_NODE_UNIFORMSTREAMQUAT:
        s->data_comp = 4;
        s->data_stride = s->data_comp * sizeof(float);
        break;
    case NGL_NODE_UNIFORMSTREAMMAT4:
        s->data_comp = 4 * 4;
        s->data_stride = s->data_comp * sizeof(float);
        break;
    default:
        ngli_assert(0);
    }
    s->count = s->data_size / s->data_stride;

    return 0;
}

const struct node_class ngli_uniformstreamvec4_class = {
    .id        = NGL_NODE_UNIFORMSTREAMVEC4,
    .name      = "UniformStreamVec4",
    .init    = uniformstream_init,
    .priv_size = sizeof(struct uniformstream),
    .params    = uniformstreamvec4_params,
    .file      = __FILE__,
};

const struct node_class ngli_uniformstreamquat_class = {
    .id        = NGL_NODE_UNIFORMSTREAMQUAT,
    .name      = "UniformStreamQuat",
    .init    = uniformstream_init,
    .priv_size = sizeof(struct uniformstream),
    .params    = uniformstreamquat_params,
    .file      = __FILE__,
};

const struct node_class ngli_uniformstreammat4_class = {
    .id        = NGL_NODE_UNIFORMSTREAMMAT4,
    .name      = "UniformStreamMat4",
    .init    = uniformstream_init,
    .priv_size = sizeof(struct uniformstream),
    .params    = uniformstreammat4_params,
    .file      = __FILE__,
};
