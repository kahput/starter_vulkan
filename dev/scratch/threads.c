#include "core/logger.h"
#include "core/strings.h"

#include <pthread.h>

static String8 strings_to_print[256];
static volatile uint32_t entry_count = 0, next_to_print = 0;

void *thread_function(void *arg) {
	uint32_t *thread_id = arg;

	while (true) {
		if (next_to_print < entry_count) {
			uint32_t entry_index = __atomic_fetch_add(&next_to_print, 1, __ATOMIC_SEQ_CST);

			LOG_INFO("THREAD[%u]: %.*s", *thread_id, sarg(strings_to_print[entry_index]));
		}
	}
	pthread_exit(0);
}
int main(void) {
	pthread_t thread_id;
	uint32_t thread_ids[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	pthread_create(&thread_id, 0, thread_function, &thread_ids[0]);
	pthread_create(&thread_id, 0, thread_function, &thread_ids[1]);
	pthread_create(&thread_id, 0, thread_function, &thread_ids[2]);
	pthread_create(&thread_id, 0, thread_function, &thread_ids[3]);
	pthread_create(&thread_id, 0, thread_function, &thread_ids[4]);
	pthread_create(&thread_id, 0, thread_function, &thread_ids[5]);
	pthread_create(&thread_id, 0, thread_function, &thread_ids[6]);
	pthread_create(&thread_id, 0, thread_function, &thread_ids[7]);
	pthread_create(&thread_id, 0, thread_function, &thread_ids[8]);
	pthread_create(&thread_id, 0, thread_function, &thread_ids[9]);

	strings_to_print[entry_count++] = s("Print 1");
	strings_to_print[entry_count++] = s("Print 2");
	strings_to_print[entry_count++] = s("Print 3");
	strings_to_print[entry_count++] = s("Print 4");
	strings_to_print[entry_count++] = s("Print 5");
	strings_to_print[entry_count++] = s("Print 6");
	strings_to_print[entry_count++] = s("Print 7");
	strings_to_print[entry_count++] = s("Print 8");
	strings_to_print[entry_count++] = s("Print 9");
	strings_to_print[entry_count++] = s("Print 10");
	strings_to_print[entry_count++] = s("Print 11");
	strings_to_print[entry_count++] = s("Print 12");
	strings_to_print[entry_count++] = s("Print 13");
	strings_to_print[entry_count++] = s("Print 14");
}
