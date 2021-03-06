/*
 * Copyright 2016 GoPro Inc.
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

#include "log.h"
#include "nodegl.h"
#include "transforms.h"

const float *ngli_get_last_transformation_matrix(const struct ngl_node *node)
{
    while (node) {
        const int id = node->class->id;
        if (id == NGL_NODE_ROTATE) {
            const struct rotate *rotate = node->priv_data;
            node = rotate->child;
        } else if (id == NGL_NODE_TRANSFORM) {
            const struct transform *transform = node->priv_data;
            node = transform->child;
        } else if (id == NGL_NODE_TRANSLATE) {
            const struct translate *translate = node->priv_data;
            node = translate->child;
        } else if (id == NGL_NODE_SCALE) {
            const struct scale *scale = node->priv_data;
            node = scale->child;
        } else if (id == NGL_NODE_IDENTITY) {
            return node->modelview_matrix;
        } else {
            LOG(ERROR, "%s (%s) is not an allowed type for a camera transformation",
                node->name, node->class->name);
            break;
        }
    }

    return NULL;
}
