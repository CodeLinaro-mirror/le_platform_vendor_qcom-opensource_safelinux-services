// Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <unistd.h>

#include "bootkpi/logging.h"


int main() {
    bootkpi_log_init();

    sleep(1);
    bootkpi_log_line("early service started CRITICAL TASK");
    sleep(1);
    bootkpi_log_line("early service finished CRITICAL TASK");
    return 0;
}
