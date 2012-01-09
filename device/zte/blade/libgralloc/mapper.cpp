/*
 * Copyright (C) 2008 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <limits.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <cutils/log.h>
#include <cutils/atomic.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <linux/android_pmem.h>

#include "gralloc_priv.h"


// we need this for now because pmem cannot mmap at an offset
#define PMEM_HACK   1

/* desktop Linux needs a little help with gettid() */
#if defined(ARCH_X86) && !defined(HAVE_ANDROID_OS)
#define __KERNEL__
# include <linux/unistd.h>
pid_t gettid() { return syscall(__NR_gettid);}
#undef __KERNEL__
#endif

/*****************************************************************************/

static int gralloc_map(gralloc_module_t const* module,
        buffer_handle_t handle,
        void** vaddr)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        size_t size = hnd->size;
#if PMEM_HACK
        size += hnd->offset;
#endif
        void* mappedAddress = mmap(0, size,
                PROT_READ|PROT_WRITE, MAP_SHARED, hnd->fd, 0);
        if (mappedAddress == MAP_FAILED) {
            LOGE("Could not mmap handle %p, fd=%d (%s)",
                    handle, hnd->fd, strerror(errno));
            hnd->base = 0;
            return -errno;
        }
        hnd->base = intptr_t(mappedAddress) + hnd->offset;
        //LOGD("gralloc_map() succeeded fd=%d, off=%d, size=%d, vaddr=%p", 
        //        hnd->fd, hnd->offset, hnd->size, mappedAddress);
    }
    *vaddr = (void*)hnd->base;
    return 0;
}

static int gralloc_unmap(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        void* base = (void*)hnd->base;
        size_t size = hnd->size;
#if PMEM_HACK
        base = (void*)(intptr_t(base) - hnd->offset);
        size += hnd->offset;
#endif
        //LOGD("unmapping from %p, size=%d, flags=%08x", base, size, hnd->flags);
        if (munmap(base, size) < 0) {
            LOGE("Could not unmap %s", strerror(errno));
        }
    }
    hnd->base = 0;
    return 0;
}

/*****************************************************************************/

static pthread_mutex_t sMapLock = PTHREAD_MUTEX_INITIALIZER; 

/*****************************************************************************/

int gralloc_register_buffer(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    // if this handle was created in this process, then we keep it as is.
    int err = 0;
    private_handle_t* hnd = (private_handle_t*)handle;
    if (hnd->pid != getpid()) {
        hnd->base = NULL;
        if (!(hnd->flags & private_handle_t::PRIV_FLAGS_USES_GPU)) {
            void *vaddr;
            err = gralloc_map(module, handle, &vaddr);
        }
    }
    return err;
}

int gralloc_unregister_buffer(gralloc_module_t const* module,
        buffer_handle_t handle)
{
    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    // never unmap buffers that were created in this process
    private_handle_t* hnd = (private_handle_t*)handle;
    if (hnd->pid != getpid()) {
        if (hnd->base) {
            gralloc_unmap(module, handle);
        }
    }
    return 0;
}

int mapBuffer(gralloc_module_t const* module,
        private_handle_t* hnd)
{
    void* vaddr;
    return gralloc_map(module, hnd, &vaddr);
}

int terminateBuffer(gralloc_module_t const* module,
        private_handle_t* hnd)
{
    if (hnd->base) {
        // this buffer was mapped, unmap it now
        if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_PMEM) {
            if (hnd->pid != getpid()) {
                // ... unless it's a "master" pmem buffer, that is a buffer
                // mapped in the process it's been allocated.
                // (see gralloc_alloc_buffer())
                gralloc_unmap(module, hnd);
            }
        } else if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_GPU) {
            // XXX: for now do nothing here
        } else {
            gralloc_unmap(module, hnd);
        }
    }

    return 0;
}

int gralloc_lock(gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr)
{
    // this is called when a buffer is being locked for software
    // access. in thin implementation we have nothing to do since
    // not synchronization with the h/w is needed.
    // typically this is used to wait for the h/w to finish with
    // this buffer if relevant. the data cache may need to be
    // flushed or invalidated depending on the usage bits and the
    // hardware.

    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;

    private_handle_t* hnd = (private_handle_t*)handle;
    *vaddr = (void*)hnd->base;
    return 0;
}

int gralloc_unlock(gralloc_module_t const* module, 
        buffer_handle_t handle)
{
    // we're done with a software buffer. nothing to do in this
    // implementation. typically this is used to flush the data cache.

    if (private_handle_t::validate(handle) < 0)
        return -EINVAL;
    return 0;
}


/*****************************************************************************/

int gralloc_perform(struct gralloc_module_t const* module,
        int operation, ... )
{
    int res = -EINVAL;
    return res;
}
