#ifndef FASTRPC_TEST_H
#define FASTRPC_TEST_H

#include "AEEStdDef.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Method to test the calculator functionality on a specified domain.
 * @param[in] domain_id          Domain id to run test on
 * @param[in] is_unsignedpd_enabled  Run on the Signed/Unsigned PD
 * @returns                      0 on Success, error code on failure
 */
int calculator_test(int domain_id, bool is_unsignedpd_enabled);

#ifdef __cplusplus
}
#endif

#endif // FASTRPC_TEST_H
