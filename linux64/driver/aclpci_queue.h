/* 
 * Copyright (c) 2017, Intel Corporation.
 * Intel, the Intel logo, Intel, MegaCore, NIOS II, Quartus and TalkBack 
 * words and logos are trademarks of Intel Corporation or its subsidiaries 
 * in the U.S. and/or other countries. Other marks and brands may be 
 * claimed as the property of others.   See Trademarks on intel.com for 
 * full list of Intel trademarks or the Trademarks & Brands Names Database 
 * (if Intel) or See www.Intel.com/legal (if Altera).
 * All rights reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * BSD 3-Clause license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 *      - Neither Intel nor the names of its contributors may be 
 *        used to endorse or promote products derived from this 
 *        software without specific prior written permission.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ACLPCI_QUEUE_H
#define ACLPCI_QUEUE_H

/* FIFO for a fixed number of elements. Interface is the same as for
 * C++ STL queue<> adaptor.
 *
 * Implemented as a circular buffer in an array.
 * Could've used kfifo but its interface changes between kernel 
 * versions. So don't want to bother porting source code just for a fifo. */
 
struct queue {
  void *buffer;       /* Buffer to hold the data. Size is >= size * elem_size */
  unsigned int size;  /* number of elements */
  unsigned int elem_size; /* size of single element */
  unsigned int count;   /* number of valid entries */
  unsigned int out;     /* First valid entry */
};

void queue_init (struct queue *q, unsigned int elem_size, unsigned int size);
void queue_fini (struct queue *q);

unsigned int queue_size (struct queue *q);
int queue_empty(struct queue *q);

void queue_push (struct queue *q, void *e);
void queue_pop (struct queue *q);
void *queue_front (struct queue *q);
void *queue_back (struct queue *q);

#endif
