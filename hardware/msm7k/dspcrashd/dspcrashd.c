/* dspcrashd.c
**
** Copyright 2010, The Android Open Source Project
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of Google Inc. nor the names of its contributors may
**       be used to endorse or promote products derived from this software
**       without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY Google Inc. ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
** MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
** EVENT SHALL Google Inc. BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
** PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
** OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
** WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
** OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
** ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/klog.h>

#include <cutils/properties.h>

#define KLOG_BUF_SHIFT	17	/* CONFIG_LOG_BUF_SHIFT from our kernel */
#define KLOG_BUF_LEN	(1 << KLOG_BUF_SHIFT)

char *props[] = {
    "ro.product.name",
    "ro.build.id",
    "ro.build.date",
    "ro.serialno",
    "ro.baseband",
    0,
};

char *dashes = "---- ---- ---- ---- ---- ---- ---- ---- ---- ----\n";

void dump_dmesg(int fd)
{
    char buffer[KLOG_BUF_LEN + 1];
    char *p = buffer;
    ssize_t ret;
    int n, op;

    n = klogctl(KLOG_READ_ALL, buffer, KLOG_BUF_LEN);
    if (n < 0)
        return;
    buffer[n] = '\0';

    while((ret = write(fd, p, n))) {
        if (ret < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        p += ret;
        n -= ret;
    }

    return 0;
}

void dump_info(int fd)
{
    char buf[4096];
    char val[PROPERTY_VALUE_MAX];
    char **p = props;

    write(fd, dashes, strlen(dashes));
    while (*p) {
        property_get(*p,val,"");
        sprintf(buf,"%s: %s\n", *p, val);
        write(fd, buf, strlen(buf));
        p++;
    }
    write(fd, dashes, strlen(dashes));
    dump_dmesg(fd);
    write(fd, dashes, strlen(dashes));    
}

void dump_dsp_state(int dsp)
{
    int fd, r;
    char name[128];
    char buf[128*1024];

    sprintf(name,"/sdcard/dsp.crash.%d.img", (int) time(0));

    fd = open(name, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0)
        return;

    for (;;) {
        r = read(dsp, buf, sizeof(buf));
        if (r == 0) break;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        write(fd, buf, r);
    }

    dump_info(fd);
    close(fd);

    fd = open("/dev/kmsg", O_WRONLY);
    if (fd >= 0) {
        sprintf(buf,"*** WROTE DSP RAMDUMP TO %s ***\n",name);
        write(fd, buf, strlen(buf));
        close(fd);
    }
}

int main(int argc, char **argv)
{
    int fd;
    fd = open("/dev/dsp_debug", O_RDWR);
    if (fd < 0)
        return -1;

    write(fd, "wait-for-crash", 14);

    dump_dsp_state(fd);

    sync();
    sync();
    sync();

    write(fd, "continue-crash", 14);
    
    close(fd);

    for (;;)
        sleep(100);

    return 0;
}

