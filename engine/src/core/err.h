#include "common.h"

bool _err_report(const char *func, const char *file, int line, const char *cond_str, const char *msg);
bool _err_report_index(const char *func, const char *file, int line, uint64_t index, uint64_t bound, const char *index_str, const char *bound_str, const char *msg);

#define ERR_COND_CHECK(cond) \
	((cond) ? (_err_print(FN_NAME, __FILE__, __LINE__, "Condition '" STRINGIFY(cond) "' is true."), false) : true)

#define ERR_NULL_CHECK(ptr) \
	((ptr) != 0 ? true : _err_print(FN_NAME, __FILE__, __LINE__, "'" STRINGIFY(ptr) "' is null."))

#define ERR_BOUND_CHECK(index, size) ((((int64_t)(index)) >= 0 && ((int64_t)(index)) < ((int64_t)(bound))) true : _err_report_index(FN_NAME, __FILE__, __LINE__, (index), (bound), _STR(index), _STR(bound)))
