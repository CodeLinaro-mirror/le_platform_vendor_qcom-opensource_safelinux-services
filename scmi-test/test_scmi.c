/* Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Refactor notes:
 * 1) Device node path is passed via argv (no hard-coded /dev/test_scmi)
 * 2) Domain/name strings for perf/power are passed via argv (no hard-coded "perf"/"power")
 * 3) main() provides three sub-commands: perf | power | reset
 */

#include <stdatomic.h>
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
#include <dirent.h>
#include <time.h>
#include <uapi/misc/qcom_uscmi.h>

#define OPP_DEBUGFS_ROOT "/sys/kernel/debug/opp/"
#define SCMI_PWR_OFF_SETTLE_SEC 1

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
	/* perf-specific: randomly assigned OPP level for this thread */
	unsigned int level;
	/* power-specific: randomly assigned vote count (0-10); 0 means "off" */
	unsigned int votes;
	/* hold coordination */
	bool use_hold;
	pthread_barrier_t *barrier;
	atomic_bool* abort_flag;
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
		"  - hold and threads are independent optional flags and may be used together\n"
		"  - in threads mode, perf ignores <level> and instead assigns each thread a random OPP level read from /sys/kernel/debug/opp/<matching-dir>/opp:<freq>/level for the device node\n"
		"  - in threads mode, power ignores on|off and instead has each thread cast 0-10 random power-on votes (0 votes = off)\n",
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

	if (op == SCMI_PWR_OFF) {
		req->oper = SCMI_PWR_ON;
		ret = ioctl(fd, SCMI_IOCTL_PWR, req);
		if (ret == -1)
			return -errno;

		sleep(SCMI_PWR_OFF_SETTLE_SEC);

		req->oper = SCMI_PWR_OFF;
	}

	ret = ioctl(fd, SCMI_IOCTL_PWR, req);
	if (ret == -1)
		return -errno;

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

/* ---- helper: does str end with suffix? ------------------------------------------------ */
static bool str_ends_with(const char *str, const char *suffix)
{
	size_t str_len = strlen(str);
	size_t suffix_len = strlen(suffix);

	if (suffix_len > str_len)
		return false;

	return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

/* ---- discover available OPP levels for a device node via debugfs ------------------------------------------------ */
static int discover_opp_levels(const char *node, unsigned int **levels_out,
			       size_t *count_out)
{
	const char *base = strrchr(node, '/');
	base = base ? base + 1 : node;

	char colon_suffix[NAME_MAX];
	char dash_suffix[NAME_MAX];
	snprintf(colon_suffix, sizeof(colon_suffix), ":%s", base);
	snprintf(dash_suffix, sizeof(dash_suffix), "-%s", base);

	DIR *root = opendir(OPP_DEBUGFS_ROOT);
	if (!root) {
		fprintf(stderr, "Failed to open '%s': %s\n", OPP_DEBUGFS_ROOT,
			strerror(errno));
		return -errno;
	}

	char opp_dir_path[PATH_MAX];
	bool matched = false;
	struct dirent *entry;

	while ((entry = readdir(root)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		if (strcmp(entry->d_name, base) != 0 &&
		    !str_ends_with(entry->d_name, colon_suffix) &&
		    !str_ends_with(entry->d_name, dash_suffix))
			continue;

		snprintf(opp_dir_path, sizeof(opp_dir_path), "%s%s",
			 OPP_DEBUGFS_ROOT, entry->d_name);
		matched = true;
		break;
	}
	closedir(root);

	if (!matched) {
		fprintf(stderr,
			"No OPP debugfs entry found for device node '%s' under '%s'.\n",
			node, OPP_DEBUGFS_ROOT);
		return -ENOENT;
	}

	DIR *opp_dir = opendir(opp_dir_path);
	if (!opp_dir) {
		fprintf(stderr, "Failed to open '%s': %s\n", opp_dir_path,
			strerror(errno));
		return -errno;
	}

	unsigned int *levels = NULL;
	size_t count = 0;
	size_t capacity = 0;

	while ((entry = readdir(opp_dir)) != NULL) {
		if (strncmp(entry->d_name, "opp:", 4) != 0)
			continue;

		char level_path[PATH_MAX];
		snprintf(level_path, sizeof(level_path), "%s/%s/level",
			 opp_dir_path, entry->d_name);

		FILE *lf = fopen(level_path, "r");
		if (!lf)
			continue;

		unsigned long level;
		int scanned = fscanf(lf, "%lu", &level);
		fclose(lf);
		if (scanned != 1)
			continue;

#if ULONG_MAX > UINT_MAX
		if (level > UINT_MAX) {
			fprintf(stderr, "OPP level value %lu exceeds UINT_MAX, skipping.\n", level);
			continue;
		}
#endif

		if (count == capacity) {
			size_t new_capacity = capacity ? capacity * 2 : 8;
			unsigned int *grown = realloc(
				levels, new_capacity * sizeof(*levels));
			if (!grown) {
				free(levels);
				closedir(opp_dir);
				return -ENOMEM;
			}
			levels = grown;
			capacity = new_capacity;
		}
		levels[count++] = (unsigned int)level;
	}
	closedir(opp_dir);

	if (count == 0) {
		fprintf(stderr, "No OPP levels found under '%s'.\n",
			opp_dir_path);
		free(levels);
		return -ENOENT;
	}

	*levels_out = levels;
	*count_out = count;
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
		/* Skip barrier entirely if main thread aborted */
		if (!atomic_load(ctx->abort_flag))
			pthread_barrier_wait(ctx->barrier);
		return NULL;
	}

	if (ctx->op_type == OP_PERF) {
		ctx->result = request_perf_operation(ctx->fd, &req,
						     ctx->domain, ctx->level,
						     SCMI_PRF_LVL_SET);
		if (ctx->result == 0)
			printf("[thread] perf level set to %u on domain '%s'.\n",
			       ctx->level, ctx->domain);
	} else {
		if (ctx->votes == 0) {
			ctx->result = request_power_operation(
				ctx->fd, &req, ctx->domain, SCMI_PWR_OFF);
			if (ctx->result == 0)
				printf("[thread] power vote: off on domain '%s'.\n",
				       ctx->domain);
		} else {
			ctx->result = 0;
			for (unsigned int v = 0; v < ctx->votes; v++) {
				ctx->result = request_power_operation(
					ctx->fd, &req, ctx->domain,
					SCMI_PWR_ON);
				if (ctx->result < 0)
					break;
			}
			if (ctx->result == 0)
				printf("[thread] power vote: on x%u on domain '%s'.\n",
				       ctx->votes, ctx->domain);
		}
	}

	if (ctx->result < 0)
		fprintf(stderr, "[thread] %s op failed on '%s': %s\n",
			ctx->op_type == OP_PERF ? "perf" : "power", ctx->domain,
			strerror(-ctx->result));

	/*
	 * Reach the barrier — once every thread (plus the collector) arrives,
	 * the hold decision is made in the main thread.  If hold is active the
	 * fd must stay open until the main thread releases us; we therefore
	 * block here until the main thread closes the fd itself (non-hold path)
	 * or until the hold sleep finishes and the main thread closes it.
	 *
	 * If the main thread aborted (pthread_create failed mid-loop), skip
	 * the barrier entirely and clean up the fd ourselves.
	 */
	if (atomic_load(ctx->abort_flag)) {
		close(ctx->fd);
		ctx->fd = -1;
		return NULL;
	}

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
			const char *domain)
{
	unsigned int created_threads = 0;
	pthread_barrier_t barrier;
	atomic_bool abort_flag;
	unsigned int *opp_levels = NULL;
	size_t opp_level_count = 0;

	if (threads_val == 0) {
		fprintf(stderr, "Error: threads_val must be > 0.\n");
		return -EINVAL;
	}

	if (op_type == OP_PERF) {
		int ret = discover_opp_levels(node, &opp_levels,
					      &opp_level_count);
		if (ret < 0)
			return ret;
	}

	atomic_init(&abort_flag, false);
	pthread_barrier_init(&barrier, NULL, threads_val + 1);

	thread_ctx_t *ctxs = calloc(threads_val, sizeof(thread_ctx_t));
	pthread_t *tids = calloc(threads_val, sizeof(pthread_t));
	if (!ctxs || !tids) {
		fprintf(stderr, "Out of memory allocating thread contexts.\n");
		free(ctxs);
		free(tids);
		free(opp_levels);
		pthread_barrier_destroy(&barrier);
		return -ENOMEM;
	}

	for (unsigned int t = 0; t < threads_val; t++) {
		ctxs[t].node = node;
		ctxs[t].op_type = op_type;
		ctxs[t].domain = domain;
		if (op_type == OP_PERF)
			ctxs[t].level =
				opp_levels[rand() % opp_level_count];
		else
			ctxs[t].votes = rand() % 11;
		ctxs[t].use_hold = use_hold;
		ctxs[t].barrier = &barrier;
		ctxs[t].abort_flag = &abort_flag;
		ctxs[t].fd = -1;
		ctxs[t].result = 0;

		int rc = pthread_create(&tids[t], NULL, thread_worker, &ctxs[t]);
		if (rc != 0) {
			fprintf(stderr, "Failed to create thread %u: %s\n", t,
				strerror(rc));

			/*
			 * Signal all already-running threads to skip the barrier
			 * and exit cleanly, then wait for them.
			 */
			atomic_store(&abort_flag, true);
			for (unsigned int j = 0; j < t; j++)
				pthread_join(tids[j], NULL);

			free(ctxs);
			free(tids);
			free(opp_levels);
			pthread_barrier_destroy(&barrier);
			return -rc;
		}
		created_threads++;
	}

	if (created_threads == 0) {
		free(ctxs);
		free(tids);
		free(opp_levels);
		pthread_barrier_destroy(&barrier);
		return -EAGAIN;
	}

	/* All threads created successfully — release them from the barrier */
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
	free(opp_levels);
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
	int *fds = NULL;
	scmi_oper_ioctl_t req;

	if (use_hold) {
		fds = malloc(amount_val * sizeof(*fds));
		if (!fds) {
			fprintf(stderr, "Failed to allocate fd array\n");
			return -ENOMEM;
		}
		/* Initialize all to -1 so cleanup is safe on partial failure */
		for (unsigned int i = 0; i < amount_val; i++)
			fds[i] = -1;
	}

	for (unsigned int i = 0; i < amount_val; i++) {
		int fd = get_fd(node);
		if (fd < 0) {
			ret = fd;
			goto out;
		}

		/* Track or immediately handle the fd */
		if (use_hold)
			fds[i] = fd;

		ret = (op_type == OP_PERF) ?
			      request_perf_operation(fd, &req, domain, level,
						     prf_op) :
			      request_power_operation(fd, &req, domain, pwr_op);

		if (ret < 0) {
			fprintf(stderr,
				"Failed %s op (iteration %u) on domain '%s': %s\n",
				op_type == OP_PERF ? "perf" : "power", i,
				domain, strerror(-ret));
			if (!use_hold) {
				close(fd);
			}
			goto out;
		}

		printf("%s op successful on domain '%s' (iteration %u/%u).\n",
		       op_type == OP_PERF ? "perf" : "power", domain, i + 1,
		       amount_val);

		if (!use_hold)
			close(fd);
	}

	maybe_hold(use_hold, hold_val);

out:
	if (use_hold && fds) {
		for (unsigned int i = 0; i < amount_val; i++) {
			if (fds[i] >= 0) {
				close(fds[i]);
				fds[i] = -1;
			}
		}
		free(fds);
	}
	return ret;
}

/* ---- main: three cases: perf / power / reset ---- */
int main(int argc, char *argv[])
{
	int ret = 0;
	int fd = -1;
	scmi_oper_ioctl_t request;

	srand((unsigned int)(time(NULL) ^ getpid()));

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
					   OP_PERF, domain);
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
					   OP_POWER, domain);
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
