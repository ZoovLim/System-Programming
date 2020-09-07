#define UNW_LOCAL_ONLY
#include <stdlib.h>
#include <libunwind.h>

int get_callinfo(char *fname, size_t fnlen, unsigned long long *ofs)
{
	unw_context_t context;
	unw_cursor_t cursor;

	unw_getcontext(&context);
	unw_init_local(&cursor, &context);
	
	// 3 steps to 'main' on stack frame
	unw_step(&cursor);
	unw_step(&cursor);
	unw_step(&cursor);

	unw_get_proc_name(&cursor, fname, fnlen, (unw_word_t *) ofs);
	
	//callq offset
	*ofs -= 5;

	return 0;
}

