/* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Refactor notes:
 * 1) Device node path is passed via argv (no hard-coded /dev/test_scmi)
 * 2) Domain/name strings for perf/power are passed via argv (no hard-coded "perf"/"power")
 * 3) main() provides three sub-commands: perf | power | reset
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <uapi/misc/qcom_uscmi.h>

/* ---- struct for optional args ---- */
typedef struct {
	bool use_hold;
	bool use_threads;
	bool use_amount;
	unsigned int hold_val;
	unsigned int threads_val;
	unsigned int amount_val;
} optional_args_t;

/* ---- Thread context for threaded mode ---- */
typedef enum {
	OP_PERF,
	OP_POWER,
} op_type_t;

typedef struct {
	/* inputs */
	const char *node;
	op_type_t op_type;
	const char *domain;
	/* perf-specific */
	unsigned int level;
	scmi_prf_oper_t prf_op;
	/* power-specific */
	scmi_pwr_oper_t pwr_op;
	/* hold coordination */
	bool use_hold;
	pthread_barrier_t *barrier; /* signals: all ops done, check hold */
	/* output */
	int fd;
	int result;
} thread_ctx_t;

/* ---- helper: usage ---- */
static void print_usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s <device_node> perf  <domain_name> set <level> [hold <val>] [threads <val> | amount <val>]\n"
		"  %s <device_node> power <domain_name> on|off      [hold <val>] [threads <val> | amount <val>]\n"
		"  %s <device_node> reset <reset_id>    reset|assert|deassert\n"
		"\n"
		"Examples:\n"
		"  %s /dev/scmi_video  perf perf_video_bw set 2\n"
		"  %s /dev/scmi_video  perf perf_video_bw set 2 hold 60\n"
		"  %s /dev/scmi_video  perf perf_video_bw set 2 threads 4\n"
		"  %s /dev/scmi_video  perf perf_video_bw set 2 hold 60 threads 4\n"
		"  %s /dev/scmi_usb2   power usb_transfer on\n"
		"  %s /dev/scmi_usb2   power usb_transfer on hold 60 threads 4\n"
		"  %s /dev/scmi_usb2   power usb_transfer on hold 60 amount 4\n"
		"  %s /dev/scmi_usb2   power usb_transfer on amount 4\n"
		"  %s /dev/test_scmi   reset core reset\n"
		"\n"
		"Notes:\n"
		"  - <domain_name> must match DT power-domain-names (e.g., perf_video_clk, perf_video_bw)\n"
		"  - <reset_id> corresponds to DT reset-names (e.g., core)\n"
		"  - hold and threads are independent optional flags and may be used together\n",
		prog, prog, prog, prog, prog, prog, prog, prog, prog, prog,
		prog, prog);
}

/* ---- Validate and copy a domain / id name ------------------------------------------------ */
static int validate_and_copy_name(const char *src, char *dest, size_t dest_size,
				  const char *type)
{
	if (!dest) {
		fprintf(stderr,
			"Error: destination buffer for %s name is NULL.\n",
			type);
		return -EINVAL;
	}

	if (dest_size == 0) {
		fprintf(stderr,
			"Error: destination size for %s name is zero.\n", type);
		return -EINVAL;
	}

	if (!src || src[0] == '\0') {
		fprintf(stderr, "Error: empty %s name.\n", type);
		return -EINVAL;
	}

	size_t len = strlcpy(dest, src, dest_size);
	if (len >= dest_size) {
		fprintf(stderr,
			"Error: %s name \"%s\" too long (%zu bytes, maximum is %zu).\n",
			type, src, len, dest_size - 1);
		return -ENAMETOOLONG;
	}

	return 0;
}

/* ---- Parse and validate mode_val parameter ---- */
static int parse_mode_val(const char *mode_str, unsigned int *mode_val)
{
	if (!mode_str || !mode_val) {
		return -EINVAL;
	}

	char *hendp = NULL;
	errno = 0;
	unsigned long hul = strtoul(mode_str, &hendp, 0);

	if (*mode_str == '\0' || (hendp && *hendp != '\0')) {
		fprintf(stderr, "Invalid mode_val '%s': not a valid integer.\n",
			mode_str);
		return -EINVAL;
	}

	if (errno == ERANGE) {
		fprintf(stderr, "Invalid mode_val '%s': value out of range.\n",
			mode_str);
		return -ERANGE;
	}

#if ULONG_MAX > UINT_MAX
	if (hul > UINT_MAX) {
		fprintf(stderr, "Invalid mode_val '%s': must be ≤ %u.\n",
			mode_str, UINT_MAX);
		return -EOVERFLOW;
	}
#endif

	*mode_val = (unsigned int)hul;
	return 0;
}

/* ---- Parse and validate optional parameters ---- */
static int parse_optional_args(int argc, char *argv[], int start,
			       optional_args_t *out)
{
	memset(out, 0, sizeof(*out));
	for (int i = start; i + 1 < argc; i += 2) {
		if (strcmp(argv[i], "hold") == 0) {
			int r = parse_mode_val(argv[i + 1], &out->hold_val);
			if (r < 0)
				return r;
			out->use_hold = true;
		} else if (strcmp(argv[i], "threads") == 0) {
			if (out->use_amount) {
				fprintf(stderr,
					"Error: 'threads' and 'amount' are mutually exclusive.\n");
				return -EINVAL;
			}
			int r = parse_mode_val(argv[i + 1], &out->threads_val);
			if (r < 0)
				return r;
			out->use_threads = true;
		} else if (strcmp(argv[i], "amount") == 0) {
			if (out->use_threads) {
				fprintf(stderr,
					"Error: 'amount' and 'threads' are mutually exclusive.\n");
				return -EINVAL;
			}
			int r = parse_mode_val(argv[i + 1], &out->amount_val);
			if (r < 0)
				return r;
			out->use_amount = true;
		} else {
			fprintf(stderr, "Unknown option '%s'.\n", argv[i]);
			return -EINVAL;
		}
	}
	return 0;
}

/* ---- Get file descriptor to node ---- */
static int get_fd(const char *node)
{
	int fd = open(node, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Failed to open node '%s': %s\n", node,
			strerror(errno));
		return -errno;
	}
	return fd;
}

/* ---- sleep for hold_val ---- */
static void maybe_hold(bool use_hold, unsigned int hold_val)
{
	if (use_hold) {
		printf("Holding for %u seconds...\n", hold_val);
		fflush(stdout);
		sleep(hold_val);
	}
}

/* ---- request_perf_operation ------------------------------------------------ */
int request_perf_operation(int fd, scmi_oper_ioctl_t *req, const char *domain,
			   unsigned int level, scmi_prf_oper_t op)
{
	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_PERFORMANCE;
	req->oper = op;
	req->level = level;

	int ret = validate_and_copy_name(domain, req->name, sizeof(req->name),
					 "performance");
	if (ret)
		return ret;

	ret = ioctl(fd, SCMI_IOCTL_PRF, req);
	if (ret == -1) {
		int err = errno;
		return -err;
	}

	return 0;
}

/* ---- request_power_operation ------------------------------------------------ */
int request_power_operation(int fd, scmi_oper_ioctl_t *req, const char *domain,
			    scmi_pwr_oper_t op)
{
	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_POWER;
	req->oper = op;

	int ret = validate_and_copy_name(domain, req->name, sizeof(req->name),
					 "power");
	if (ret)
		return ret;

	ret = ioctl(fd, SCMI_IOCTL_PWR, req);
	if (ret == -1) {
		int err = errno;
		return -err;
	}

	return 0;
}

/* ---- request_reset_operation ------------------------------------------------ */
int request_reset_operation(int fd, scmi_oper_ioctl_t *req, const char *id,
			    scmi_rst_oper_t op)
{
	memset(req, 0, sizeof(*req));
	req->proto = SCMI_PROTO_RESET;
	req->oper = op;

	int ret = validate_and_copy_name(id, req->name, sizeof(req->name),
					 "reset");
	if (ret)
		return ret;

	ret = ioctl(fd, SCMI_IOCTL_RST, req);
	if (ret == -1) {
		int err = errno;
		return -err;
	}

	return 0;
}

/* ---- worker thread ------------------------------------------------ */
static void *thread_worker(void *arg)
{
	thread_ctx_t *ctx = (thread_ctx_t *)arg;
	scmi_oper_ioctl_t req;

	ctx->fd = get_fd(ctx->node);
	if (ctx->fd < 0) {
		ctx->result = ctx->fd;
		pthread_barrier_wait(ctx->barrier);
		return NULL;
	}

	ctx->result = (ctx->op_type == OP_PERF) ?
			      request_perf_operation(ctx->fd, &req, ctx->domain,
						     ctx->level, ctx->prf_op) :
			      request_power_operation(ctx->fd, &req,
						      ctx->domain, ctx->pwr_op);

	if (ctx->result < 0)
		fprintf(stderr, "[thread] %s op failed on '%s': %s\n",
			ctx->op_type == OP_PERF ? "perf" : "power", ctx->domain,
			strerror(-ctx->result));
	else
		printf("[thread] %s op successful on domain '%s'.\n",
		       ctx->op_type == OP_PERF ? "perf" : "power", ctx->domain);

	/*
     * Reach the barrier — once every thread (plus the collector) arrives,
     * the hold decision is made in the main thread.  If hold is active the
     * fd must stay open until the main thread releases us; we therefore
     * block here until the main thread closes the fd itself (non-hold path)
     * or until the hold sleep finishes and the main thread closes it.
     *
     * Implementation: use a second barrier so threads wait while the main
     * thread sleeps for hold_val, then all close together.
     */
	pthread_barrier_wait(ctx->barrier);

	/*
     * If hold is active the main thread will close our fd after the sleep;
     * otherwise we close it ourselves after the barrier.
     */
	if (!ctx->use_hold) {
		close(ctx->fd);
		ctx->fd = -1;
	}
	/* If use_hold, fd is closed by the main thread after the hold sleep. */

	return NULL;
}

/* ---- run threaded operations ------------------------------------------------ */
static int run_threaded(const char *node, unsigned int threads_val,
			bool use_hold, unsigned int hold_val, op_type_t op_type,
			const char *domain, unsigned int level,
			scmi_prf_oper_t prf_op, scmi_pwr_oper_t pwr_op)
{
	unsigned int created_threads = 0;
	pthread_barrier_t barrier;

	pthread_barrier_init(&barrier, NULL, threads_val + 1);
	thread_ctx_t *ctxs = calloc(threads_val, sizeof(thread_ctx_t));
	pthread_t *tids = calloc(threads_val, sizeof(pthread_t));
	if (!ctxs || !tids) {
		fprintf(stderr, "Out of memory allocating thread contexts.\n");
		free(ctxs);
		free(tids);
		pthread_barrier_destroy(&barrier);
		return -ENOMEM;
	}

	for (unsigned int t = 0; t < threads_val; t++) {
		ctxs[t].node = node;
		ctxs[t].op_type = op_type;
		ctxs[t].domain = domain;
		ctxs[t].level = level;
		ctxs[t].prf_op = prf_op;
		ctxs[t].pwr_op = pwr_op;
		ctxs[t].use_hold = use_hold;
		ctxs[t].barrier = &barrier;
		ctxs[t].fd = -1;
		ctxs[t].result = 0;
		int rc =
			pthread_create(&tids[t], NULL, thread_worker, &ctxs[t]);
		if (rc != 0) {
			fprintf(stderr, "Failed to create thread %u: %s\n", t,
				strerror(rc));
			for (unsigned int j = 0; j < t; j++) {
				pthread_join(tids[j], NULL);
			}
			free(ctxs);
			free(tids);
			pthread_barrier_destroy(&barrier);
			return -rc;
		}
		created_threads++;
	}

	if (created_threads == 0) {
		free(ctxs);
		free(tids);
		return -EAGAIN;
	}

	pthread_barrier_wait(&barrier);

	maybe_hold(use_hold, hold_val);
	if (use_hold) {
		for (unsigned int t = 0; t < threads_val; t++) {
			if (ctxs[t].fd >= 0) {
				close(ctxs[t].fd);
				ctxs[t].fd = -1;
			}
		}
	}

	int ret = 0;
	for (unsigned int t = 0; t < threads_val; t++) {
		pthread_join(tids[t], NULL);
		if (ctxs[t].result < 0)
			ret = ctxs[t].result;
	}

	free(ctxs);
	free(tids);
	pthread_barrier_destroy(&barrier);
	return ret;
}

/* ---- run N amount operations ------------------------------------------------ */
static int run_amount(const char *node, unsigned int amount_val, bool use_hold,
		      unsigned int hold_val, op_type_t op_type,
		      const char *domain, unsigned int level,
		      scmi_prf_oper_t prf_op, scmi_pwr_oper_t pwr_op)
{
	int ret = 0;
	int fd = -1;
	scmi_oper_ioctl_t req;

	for (unsigned int i = 0; i < amount_val; i++) {
		fd = get_fd(node);
		if (fd < 0) {
			ret = fd;
			goto out;
		}

		ret = (op_type == OP_PERF) ?
			      request_perf_operation(fd, &req, domain, level,
						     prf_op) :
			      request_power_operation(fd, &req, domain, pwr_op);

		if (ret < 0) {
			fprintf(stderr,
				"Failed %s op (iteration %u) on domain '%s': %s\n",
				op_type == OP_PERF ? "perf" : "power", i,
				domain, strerror(-ret));
			goto out;
		}

		printf("%s op successful on domain '%s' (iteration %u/%u).\n",
		       op_type == OP_PERF ? "perf" : "power", domain, i + 1,
		       amount_val);

		if (!use_hold) {
			close(fd);
			fd = -1;
		}
	}
	maybe_hold(use_hold, hold_val);
out:
	if (fd >= 0) {
		close(fd);
		fd = -1;
	}
	return ret;
}

/* ---- main: three cases: perf / power / reset ---- */
int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1;
	scmi_oper_ioctl_t request;

	if (argc < 3) {
		print_usage(argv[0]);
		ret = -EINVAL;
		goto out;
	}

	const char *node = argv[1];
	const char *op = argv[2];

	if (!node || node[0] == '\0' || !op || op[0] == '\0') {
		fprintf(stderr, "Error: empty device node or operation.\n");
		print_usage(argv[0]);
		ret = -EINVAL;
		goto out;
	}

	if (strncmp(node, "/dev/", 5) != 0) {
		fprintf(stderr,
			"Error: device node path must start with \"/dev/\".\n");
		print_usage(argv[0]);
		ret = -EINVAL;
		goto out;
	}

	/* ---- perf: <node> perf <domain> set <level> [hold <val>] [amount|threads <val>] ---- */
	if (strcmp(op, "perf") == 0) {
		/* minimum: node perf domain set level  => argc 6
         * +hold <val>                          => argc 8
         * +threads <val>                       => argc 8
         * +hold <val> + amount|threads <val>           => argc 10 */
		if (argc != 6 && argc != 8 && argc != 10) {
			print_usage(argv[0]);
			ret = -EINVAL;
			goto out;
		}
		const char *domain = argv[3];
		const char *action = argv[4];
		const char *level_str = argv[5];

		optional_args_t opts;
		ret = parse_optional_args(argc, argv, 6, &opts);
		if (ret < 0) {
			print_usage(argv[0]);
			goto out;
		}

		if (strcmp(action, "set") != 0) {
			fprintf(stderr,
				"Unsupported perf action '%s'. Only 'set' is supported.\n",
				action);
			ret = -EINVAL;
			goto out;
		}
		char *endp = NULL;
		errno = 0;
		unsigned long ul = strtoul(level_str, &endp, 0);

		if (*level_str == '\0' || (endp && *endp != '\0')) {
			fprintf(stderr,
				"Invalid perf level '%s': not a valid integer.\n",
				level_str);
			ret = -EINVAL;
			goto out;
		}

		if (errno == ERANGE) {
			fprintf(stderr,
				"Invalid perf level '%s': value out of range.\n",
				level_str);
			ret = -ERANGE;
			goto out;
		}

#if ULONG_MAX > UINT_MAX
		if (ul > UINT_MAX) {
			fprintf(stderr,
				"Invalid perf level '%s': must be ≤ %u.\n",
				level_str, UINT_MAX);
			ret = -EOVERFLOW;
			goto out;
		}
#endif
		unsigned int ulevel = (unsigned int)ul;

		if (opts.use_threads) {
			ret = run_threaded(node, opts.threads_val,
					   opts.use_hold, opts.hold_val,
					   OP_PERF, domain, ulevel,
					   SCMI_PRF_LVL_SET, 0);
			if (ret < 0)
				goto out;
		} else if (opts.use_amount) {
			ret = run_amount(node, opts.amount_val, opts.use_hold,
					 opts.hold_val, OP_PERF, domain, ulevel,
					 SCMI_PRF_LVL_SET, 0);
			if (ret < 0)
				goto out;
		} else {
			fd = get_fd(node);
			if (fd < 0)
				goto out;
			ret = request_perf_operation(fd, &request, domain,
						     ulevel, SCMI_PRF_LVL_SET);
			if (ret < 0) {
				fprintf(stderr,
					"Failed to set perf level %u for domain '%s': %s\n",
					ulevel, domain, strerror(-ret));
				goto out;
			}
			printf("Perf level set to %u successfully on domain '%s'.\n",
			       ulevel, domain);

			/* hold mode — keep the FD open to retain the driver client */
			maybe_hold(opts.use_hold, opts.hold_val);
		}
	}
	/* ---- power: <node> power <domain> on|off [hold <val>] [amount|threads <val>] ---- */
	else if (strcmp(op, "power") == 0) {
		/* minimum: node power domain on|off  => argc 5
         * +hold <val>                        => argc 7
         * +amount|threads <val>                     => argc 7
         * +hold <val> + amount|threads <val>         => argc 9 */
		if (argc != 5 && argc != 7 && argc != 9) {
			print_usage(argv[0]);
			ret = -EINVAL;
			goto out;
		}
		const char *domain = argv[3];
		const char *action = argv[4];

		/* Parse optional key/value pairs starting at argv[5] */
		optional_args_t opts;
		ret = parse_optional_args(argc, argv, 5, &opts);
		if (ret < 0) {
			print_usage(argv[0]);
			goto out;
		}

		scmi_pwr_oper_t pwr_op;
		if (strcmp(action, "on") == 0) {
			pwr_op = SCMI_PWR_ON;
		} else if (strcmp(action, "off") == 0) {
			pwr_op = SCMI_PWR_OFF;
		} else {
			fprintf(stderr,
				"Unsupported power action '%s'. Use 'on' or 'off'.\n",
				action);
			ret = -EINVAL;
			goto out;
		}

		if (opts.use_threads) {
			ret = run_threaded(node, opts.threads_val,
					   opts.use_hold, opts.hold_val,
					   OP_POWER, domain, 0, 0, pwr_op);
			if (ret < 0)
				goto out;
		} else if (opts.use_amount) {
			ret = run_amount(node, opts.amount_val, opts.use_hold,
					 opts.hold_val, OP_POWER, domain, 0, 0,
					 pwr_op);
			if (ret < 0)
				goto out;
		} else {
			fd = get_fd(node);
			if (fd < 0)
				goto out;
			ret = request_power_operation(fd, &request, domain,
						      pwr_op);
			if (ret < 0) {
				fprintf(stderr,
					"Failed to power %s domain '%s': %s\n",
					action, domain, strerror(-ret));
				goto out;
			}
			printf("Power %s successful on domain '%s'.\n", action,
			       domain);

			/* hold mode — keep the FD open to retain the driver client */
			maybe_hold(opts.use_hold, opts.hold_val);
		}
	}
	/* ---- reset: <node> reset <id> reset|assert|deassert ---- */
	else if (strcmp(op, "reset") == 0) {
		if (argc != 5) {
			print_usage(argv[0]);
			ret = -EINVAL;
			goto out;
		}
		const char *id = argv[3];
		const char *action = argv[4];

		scmi_rst_oper_t rst_op;
		if (strcmp(action, "reset") == 0) {
			rst_op = SCMI_RST_RESET;
		} else if (strcmp(action, "assert") == 0) {
			rst_op = SCMI_RST_ASSERT;
		} else if (strcmp(action, "deassert") == 0) {
			rst_op = SCMI_RST_DEASSERT;
		} else {
			fprintf(stderr,
				"Unsupported reset action '%s'. Use 'reset'|'assert'|'deassert'.\n",
				action);
			ret = -EINVAL;
			goto out;
		}

		fd = get_fd(node);
		if (fd < 0)
			goto out;
		ret = request_reset_operation(fd, &request, id, rst_op);
		if (ret < 0) {
			fprintf(stderr,
				"Failed to perform reset action '%s' on id '%s': %s\n",
				action, id, strerror(-ret));
			goto out;
		}

		printf("Reset action '%s' successful on id '%s'.\n", action,
		       id);
	} else {
		fprintf(stderr, "Unknown operation '%s'.\n", op);
		print_usage(argv[0]);
		ret = -EINVAL;
		goto out;
	}

out:
	if (fd >= 0)
		close(fd);
	return ret;
}
