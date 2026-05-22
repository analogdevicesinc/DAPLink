/**
 * @file    circular_buffer.c
 * @brief   Implementation of a circular buffer
 *
 * DAPLink Interface Firmware
 * Copyright (c) 2016-2016, ARM Limited, All Rights Reserved
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "circ_buf.h"

#include "util.h"

void circ_buf_init(circ_buf_t *circ_buf, uint8_t *buffer, uint32_t size)
{
    
    circ_buf->buf = buffer;
    circ_buf->size = size;
    circ_buf->head = 0;
    circ_buf->tail = 0;

}

void circ_buf_push(circ_buf_t *circ_buf, uint8_t data)
{

    circ_buf->buf[circ_buf->tail] = data;
    circ_buf->tail += 1;
    if (circ_buf->tail >= circ_buf->size) {
        util_assert(circ_buf->tail == circ_buf->size);
        circ_buf->tail = 0;
    }

    // Assert no overflow
    util_assert(circ_buf->head != circ_buf->tail);

}

uint8_t circ_buf_pop(circ_buf_t *circ_buf)
{
    uint8_t data;

    // Assert buffer isn't empty
    util_assert(circ_buf->head != circ_buf->tail);

    data = circ_buf->buf[circ_buf->head];
    circ_buf->head += 1;
    if (circ_buf->head >= circ_buf->size) {
        util_assert(circ_buf->head == circ_buf->size);
        circ_buf->head = 0;
    }


    return data;
}

uint32_t circ_buf_count_used(circ_buf_t *circ_buf)
{
    uint32_t cnt;

    if (circ_buf->tail >= circ_buf->head) {
        cnt = circ_buf->tail - circ_buf->head;
    } else {
        cnt = circ_buf->tail + circ_buf->size - circ_buf->head;
    }

    return cnt;
}

uint32_t circ_buf_count_free(circ_buf_t *circ_buf)
{
    uint32_t cnt;

    cnt = circ_buf->size - circ_buf_count_used(circ_buf) - 1;

    return cnt;
}

uint32_t circ_buf_read(circ_buf_t *circ_buf, uint8_t *data, uint32_t size)
{
    uint32_t cnt;
    uint32_t i;

    cnt = circ_buf_count_used(circ_buf);
    cnt = MIN(size, cnt);
    for (i = 0; i < cnt; i++) {
        data[i] = circ_buf_pop(circ_buf);
    }

    return cnt;
}

uint32_t circ_buf_write(circ_buf_t *circ_buf, const uint8_t *data, uint32_t size)
{
    uint32_t cnt;
    uint32_t i;

    cnt = circ_buf_count_free(circ_buf);
    cnt = MIN(size, cnt);
    for (i = 0; i < cnt; i++) {
        circ_buf_push(circ_buf, data[i]);
    }

    return cnt;
}

const uint8_t* circ_buf_peek(circ_buf_t *circ_buf, uint32_t* size)
{
    uint32_t cnt;
    uint8_t* ret;


    if (circ_buf->tail >= circ_buf->head) {
        cnt = circ_buf->tail - circ_buf->head;
    } else {
        // We can't peek all the bytes in the circular buffer in this case.
        cnt = circ_buf->size - circ_buf->head;
    }
    ret = circ_buf->buf + circ_buf->head;


    if (size) {
        *size = cnt;
    }
    return ret;
}

void circ_buf_pop_n(circ_buf_t *circ_buf, uint32_t n)
{

    if (circ_buf->tail >= circ_buf->head) {
        util_assert(circ_buf->tail - circ_buf->head >= n);
        circ_buf->head += n;
    } else {
        util_assert(circ_buf->tail + circ_buf->size - circ_buf->head >= n);
        circ_buf->head += n;
        if (circ_buf->head >= circ_buf->size) {
            circ_buf->head -= circ_buf->size;
        }
    }

}
