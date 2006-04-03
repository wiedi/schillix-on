/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/sdt_impl.h>

static dtrace_pattr_t vtrace_attr = {
{ DTRACE_STABILITY_UNSTABLE, DTRACE_STABILITY_UNSTABLE, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_UNSTABLE, DTRACE_STABILITY_UNSTABLE, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t info_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t fpu_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_CPU },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t fsinfo_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t stab_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
};

static dtrace_pattr_t sdt_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
};

sdt_provider_t sdt_providers[] = {
	{ "vtrace", "__vtrace_", &vtrace_attr, 0 },
	{ "sysinfo", "__cpu_sysinfo_", &info_attr, 0 },
	{ "vminfo", "__cpu_vminfo_", &info_attr, 0 },
	{ "fpuinfo", "__fpuinfo_", &fpu_attr, 0 },
	{ "sched", "__sched_", &stab_attr, 0 },
	{ "proc", "__proc_", &stab_attr, 0 },
	{ "io", "__io_", &stab_attr, 0 },
	{ "mib", "__mib_", &stab_attr, 0 },
	{ "fsinfo", "__fsinfo_", &fsinfo_attr, 0 },
	{ "sdt", NULL, &sdt_attr, 0 },
	{ NULL }
};

sdt_argdesc_t sdt_args[] = {
	{ "sched", "wakeup", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "wakeup", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "dequeue", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "dequeue", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "dequeue", 2, 1, "disp_t *", "cpuinfo_t *" },
	{ "sched", "enqueue", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "enqueue", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "enqueue", 2, 1, "disp_t *", "cpuinfo_t *" },
	{ "sched", "enqueue", 3, 2, "int" },
	{ "sched", "off-cpu", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "off-cpu", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "tick", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "tick", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "change-pri", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "change-pri", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "change-pri", 2, 1, "pri_t" },
	{ "sched", "schedctl-nopreempt", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "schedctl-nopreempt", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "schedctl-nopreempt", 2, 1, "int" },
	{ "sched", "schedctl-preempt", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "schedctl-preempt", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "sched", "schedctl-yield", 0, 0, "int" },
	{ "sched", "surrender", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "sched", "surrender", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "proc", "create", 0, 0, "proc_t *", "psinfo_t *" },
	{ "proc", "exec", 0, 0, "string" },
	{ "proc", "exec-failure", 0, 0, "int" },
	{ "proc", "exit", 0, 0, "int" },
	{ "proc", "fault", 0, 0, "int" },
	{ "proc", "fault", 1, 1, "siginfo_t *" },
	{ "proc", "lwp-create", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "proc", "lwp-create", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "proc", "signal-clear", 0, 0, "int" },
	{ "proc", "signal-clear", 1, 1, "siginfo_t *" },
	{ "proc", "signal-discard", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "proc", "signal-discard", 1, 1, "proc_t *", "psinfo_t *" },
	{ "proc", "signal-discard", 2, 2, "int" },
	{ "proc", "signal-handle", 0, 0, "int" },
	{ "proc", "signal-handle", 1, 1, "siginfo_t *" },
	{ "proc", "signal-handle", 2, 2, "void (*)(void)" },
	{ "proc", "signal-send", 0, 0, "kthread_t *", "lwpsinfo_t *" },
	{ "proc", "signal-send", 1, 0, "kthread_t *", "psinfo_t *" },
	{ "proc", "signal-send", 2, 1, "int" },
	{ "io", "start", 0, 0, "buf_t *", "bufinfo_t *" },
	{ "io", "start", 1, 0, "buf_t *", "devinfo_t *" },
	{ "io", "start", 2, 0, "buf_t *", "fileinfo_t *" },
	{ "io", "done", 0, 0, "buf_t *", "bufinfo_t *" },
	{ "io", "done", 1, 0, "buf_t *", "devinfo_t *" },
	{ "io", "done", 2, 0, "buf_t *", "fileinfo_t *" },
	{ "io", "wait-start", 0, 0, "buf_t *", "bufinfo_t *" },
	{ "io", "wait-start", 1, 0, "buf_t *", "devinfo_t *" },
	{ "io", "wait-start", 2, 0, "buf_t *", "fileinfo_t *" },
	{ "io", "wait-done", 0, 0, "buf_t *", "bufinfo_t *" },
	{ "io", "wait-done", 1, 0, "buf_t *", "devinfo_t *" },
	{ "io", "wait-done", 2, 0, "buf_t *", "fileinfo_t *" },
	{ "mib", NULL, 0, 0, "int" },
	{ "fsinfo", NULL, 0, 0, "vnode_t *", "fileinfo_t *" },
	{ "fsinfo", NULL, 1, 1, "int", "int" },
	{ NULL }
};

/*ARGSUSED*/
void
sdt_getargdesc(void *arg, dtrace_id_t id, void *parg, dtrace_argdesc_t *desc)
{
	sdt_probe_t *sdp = parg;
	int i;

	desc->dtargd_native[0] = '\0';
	desc->dtargd_xlate[0] = '\0';

	for (i = 0; sdt_args[i].sda_provider != NULL; i++) {
		sdt_argdesc_t *a = &sdt_args[i];

		if (strcmp(sdp->sdp_provider->sdtp_name, a->sda_provider) != 0)
			continue;

		if (a->sda_name != NULL &&
		    strcmp(sdp->sdp_name, a->sda_name) != 0)
			continue;

		if (desc->dtargd_ndx != a->sda_ndx)
			continue;

		if (a->sda_native != NULL)
			(void) strcpy(desc->dtargd_native, a->sda_native);

		if (a->sda_xlate != NULL)
			(void) strcpy(desc->dtargd_xlate, a->sda_xlate);

		desc->dtargd_mapping = a->sda_mapping;
		return;
	}

	desc->dtargd_ndx = DTRACE_ARGNONE;
}
