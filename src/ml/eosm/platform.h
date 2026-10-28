#ifndef SOUP_PLATFORM_H
#define SOUP_PLATFORM_H

#ifdef SOUP_HOST_TEST
#include "tests/platform.h"
#else
#include <module.h>
#include <dryos.h>
#include <menu.h>
#include <config.h>
#include <lens.h>
#include <propvalues.h>
#endif

#include "se.h"
#include "stock.h"

/* Shared by the worker and the streaming decoder; no camera addresses. */
int soup_cancelled(void);
void soup_yield(void);
extern const char *soup_error_detail;
se_err soup_develop(const char *cr2, const char *tmp,
                    const se_stock *stock, int scale);

#endif
