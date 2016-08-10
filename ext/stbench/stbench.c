#include "ruby.h"
#include "../../internal.h"
#include "ruby/st.h"
#include <stdio.h>
#include <sys/resource.h>

#include <stdarg.h>
#define elog(...) fprintf(stderr, __VA_ARGS__)

static void
report_rss(const char header[]) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("%s Max RSS = %ld\n", header, ru.ru_maxrss);
}

#include <execinfo.h>
static void
get_backtrace(void) {
    void *callstack[128];
    int frames = backtrace(callstack, 128);
    char **strs = backtrace_symbols(callstack, frames);
    for (int i = 0; i < frames; ++i) {
	fprintf(stdout, "%s\n", strs[i]);
    }
    free(strs);
}

static VALUE rb_cSTBench;

static void
usage(void) {
    elog("bench = STBench(type[, ht_init_size, scale, pattern])");
    elog("type = 'num' | 'str'");
    elog("bench ");
}

static void
stbench_dmark(void *stbench) {
    (void) stbench;
}
static void
stbench_dfree(void *stbench) {
    (void) stbench;
}
static size_t
stbench_dsize(const void *stbench) {
    (void) stbench;
    return 0;
}
const rb_data_type_t stbench_type = {
    "cstbench", {stbench_dmark, stbench_dfree, stbench_dsize, {0}}, 0, 0, 0};
#define value2stbench(stbench, value) \
    TypedData_Get_Struct((value), struct stbench_tag, &stbench_type, (stbench))
#define stbench2value(stbench) \
    TypedData_Wrap_Struct(rb_cSTBench, &stbench_type, (stbench))
#define value2stbench_checked(value) \
    ((stbench) rb_check_typeddata((value), &stbench_type))
static VALUE
stbench_alloc(VALUE klass) {
    return stbench2value(0);
}

static st_table *stb_table = NULL;
typedef enum { KeyNum, KeyStr, Unset } st_k_type;
st_k_type key_type = Unset;
typedef enum { Same, Different, Random } st_pattern;
st_pattern key_pattern = Same;
#define SCALE_BASE 100000
static int n_trial = 1 * SCALE_BASE;
static int key_len = 5;

static VALUE
stbench_init(int argc, VALUE *argv, VALUE self) {
    VALUE arg_type, arg_ht_size, arg_scale, arg_pattern, arg_keylen;
    int ht_size;
    char *vtype, *vpattern;

    if (stb_table)
	st_free_table(stb_table);

    rb_scan_args(argc, argv, "41", &arg_type, &arg_ht_size, &arg_scale,
	    &arg_pattern, &arg_keylen);

    ht_size = FIX2INT(arg_ht_size);
    n_trial = FIX2INT(arg_scale) * SCALE_BASE;
    if (arg_keylen != Qnil)
	key_len = FIX2INT(arg_keylen);

    Check_Type(arg_type, T_STRING);
    vtype = rb_string_value_cstr(&arg_type);
    Check_Type(arg_pattern, T_STRING);
    vpattern = rb_string_value_cstr(&arg_pattern);

    if (strcmp(vpattern, "same") == 0)
	key_pattern = Same;
    else if (strcmp(vpattern, "diff") == 0)
	key_pattern = Different;
    else if (strcmp(vpattern, "rand") == 0)
	key_pattern = Random;
    else {
	usage();
	rb_raise(rb_eArgError, "%s: unexpected pattern", __func__);
    }

    if (strcmp(vtype, "num") == 0)
	key_type = KeyNum;
    else if (strcmp(vtype, "str") == 0)
	key_type = KeyStr;
    else {
	usage();
	rb_raise(rb_eArgError, "%s: unexpected arguments", __func__);
    }

    switch (key_type) {
	case KeyNum:
	    if (ht_size > 0)
		stb_table = st_init_numtable_with_size(ht_size);
	    else
		stb_table = st_init_numtable();
	    break;
	case KeyStr:
	    if (ht_size > 0)
		stb_table = st_init_strtable_with_size(ht_size);
	    else
		stb_table = st_init_strtable();
	    break;
	default:
	    usage();
	    rb_raise(rb_eArgError, "%s: unexpected arguments", __func__);
    }

    return self;
}

static VALUE
stbench_init_only(VALUE self) {
    elog("initonly called");
    return self;
}

static int *int_array = NULL;
static char **char_array = NULL;

#define RAND_UPTO(max) (int) rb_random_ulong_limited((rb_cRandom), (max) -1)
/* XXX: rb_cRandom is valid randgen ?? */
static void
fill_carray_with_random_values(void) {
    if (!char_array)
	char_array = xmalloc(sizeof(char *) * n_trial);

    for (int i = 0; i < n_trial; i++) {
	char_array[i] = xmalloc(sizeof(char) * key_len);
	for (int j = 0; j < key_len; j++)
	    char_array[i][j] = 'A' + RAND_UPTO('z' - 'A' + 1);
    }
}

static void
fill_array_with_random_values(void) {
    if (!int_array)
	int_array = xmalloc(sizeof(int) * n_trial);

    for (int i = 0; i < n_trial; i++)
	int_array[i] = RAND_UPTO(n_trial);
}

static void
fill_array_with_different_values(void) {
    if (!int_array)
	int_array = xmalloc(sizeof(int) * n_trial);

    for (int i = 0; i < n_trial; i++)
	int_array[i] = i;
    for (int i = 0; i < n_trial; i++) {
	int j = RAND_UPTO(n_trial - i) + i;
	int tmp = int_array[j];
	int_array[j] = int_array[i];
	int_array[i] = tmp;
    }
}

static VALUE
stbench_setup(VALUE self)
{
    switch (key_type) {
	case KeyNum:
	    if (key_pattern == Same)
		fill_array_with_different_values();
	    else if (key_pattern == Different)
		fill_array_with_different_values();
	    else if (key_pattern == Random)
		fill_array_with_random_values();
	    break;
	case KeyStr:
	    if (key_pattern == Same)
		fill_carray_with_random_values();
	    else if (key_pattern == Different)
		rb_raise(rb_eRuntimeError, "%s: not implemented", __func__);
	    else if (key_pattern == Random)
		fill_carray_with_random_values();
	    break;
	default:
	    rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);
	    break;
    }
    report_rss("After setup");

    return self;
}

static VALUE
stbench_insert(VALUE self) {
    switch (key_type) {
	case KeyNum:
	    if (key_pattern == Same)
		for (int i = 0; i < n_trial; i++)
		    st_insert(stb_table, 123, 456);
	    else if (key_pattern == Different)
		for (int i = 0; i < n_trial; i++)
		    st_insert(stb_table, int_array[i], 456);
	    else if (key_pattern == Random)
		for (int i = 0; i < n_trial; i++)
		    st_insert(stb_table, int_array[i], 456);
	    break;
	case KeyStr:
	    if (key_pattern == Same)
		for (int i = 0; i < n_trial; i++)
		    st_insert(stb_table, (st_data_t) char_array[123], 456);
	    else if (key_pattern == Different)
		rb_raise(rb_eRuntimeError, "%s: not implemented", __func__);
	    else if (key_pattern == Random)
		for (int i = 0; i < n_trial; i++)
		    st_insert(stb_table, (st_data_t) char_array[i], 456);
	    break;
	default:
	    rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);
	    break;
    }

    report_rss("After benchmark");

    return self;
}

static VALUE
stbench_cleanup(VALUE self)
{
    if (key_type == KeyNum)
    {
	xfree(int_array);
	int_array = NULL;
    }
    else if (key_type == KeyStr)
    {
	xfree(char_array);
	char_array = NULL;
    }
    else
	rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);

    return self;
}

static VALUE
stbench_delete(VALUE self) {
    printf("delete called\n");
    return self;
}
static VALUE
stbench_search(VALUE self) {
    printf("search called\n");
    return self;
}

void
Init_STBench(void) {
#undef rb_intern
#define rb_inetrn(rope) rb_intern_const(rope)
    rb_cSTBench = rb_define_class("STBench", rb_cData);
    rb_define_alloc_func(rb_cSTBench, stbench_alloc);
    rb_define_private_method(rb_cSTBench, "initialize", stbench_init, -1);

    rb_define_method(rb_cSTBench, "call_init", stbench_init_only, -1);
    rb_define_method(rb_cSTBench, "setup", stbench_setup, 0);
    rb_define_method(rb_cSTBench, "cleanup", stbench_cleanup, 0);
    rb_define_method(rb_cSTBench, "insert", stbench_insert, 0);
    rb_define_method(rb_cSTBench, "delete", stbench_delete, 0);
    rb_define_method(rb_cSTBench, "search", stbench_search, 0);
}
