#include "ruby.h"
#include "../../internal.h"
#include "ruby/st.h"
#include "./utils.h"

#include <assert.h>
#include <limits.h>

static VALUE rb_cSTBench;
static st_table *stb_table = NULL;
#define SCALE_BASE 100000

/* XXX: rb_cRandom is valid randgen ?? */
#define N_OF_CHARS ('z' - 'A' + 1)
#define RAND_UPTO(max) ((int) rb_random_ulong_limited((rb_cRandom), (max) -1))
#define GET_RAND_CHAR() ('A' + RAND_UPTO(N_OF_CHARS))

static int *int_array = NULL;
static char **str_array = NULL;

static void
fill_str_array_random(int array_size, int str_len) {
    for (int i = 0; i < array_size; i++) {
	for (int j = 0; j < str_len; j++)
	    str_array[i][j] = GET_RAND_CHAR();
	str_array[i][str_len] = '\0';
    }
}

static void
fill_int_array_random(int array_size) {
    for (int i = 0; i < array_size; i++)
	int_array[i] = RAND_UPTO(array_size);
}

static void
fill_int_array_diff(int array_size) {
    /* Fisher-Yates shuffle */
    for (int i = 0; i < array_size; i++)
	int_array[i] = i;
    for (int i = 0; i < array_size; i++) {
	int j = RAND_UPTO(array_size - i) + i;
	int tmp = int_array[j];
	int_array[j] = int_array[i];
	int_array[i] = tmp;
    }
}


static long rss_before, rss_after;
#define REPORT_RSS() elog("before: %10ld, after: %10ld, diff%10ld\n", (rss_before), (rss_after), (rss_after) - (rss_before));

typedef enum stbench_scenario_tag
{
    STBenchNotSet,

    STBenchInit,
    STBenchInsert,
    STBenchSearch,
    STBenchDelete,
} STBenchScenario;
static STBenchScenario running_scenario = STBenchNotSet;

typedef enum stbench_key_type_tag
{
    KeyTypeNum = 1,
    KeyTypeStr,
} STBenchKeyType;

static STBenchKeyType
parse_key_type(const char str[])
{
    if (strcmp(str, "num") == 0)
	return KeyTypeNum;
    else if (strcmp(str, "str") == 0)
	return KeyTypeStr;
    else
	rb_raise(rb_eRuntimeError, "Unexpected key type is specified");
}

typedef enum st_pattern_tag
{
    PatternSame = 1,
    PatternDifferent,
    PatternRandom,
} STBenchPattern;

static STBenchPattern
parse_pattern(const char str[])
{
    if (strcmp(str, "same") == 0)
	return PatternSame;
    else if (strcmp(str, "diff") == 0)
	return PatternDifferent;
    if (strcmp(str, "rand") == 0)
	return PatternRandom;
    else
	rb_raise(rb_eRuntimeError, "Unexpected pattern is specified");
}

/* For extension. Do nothing here, for now */
static void stbench_dmark(void *stbench) { (void) stbench; }
static void stbench_dfree(void *stbench) { (void) stbench; }
static size_t stbench_dsize(const void *stbench) { (void) stbench; return 0; }
const rb_data_type_t stbench_type = {
    "aoeusnth", {stbench_dmark, stbench_dfree, stbench_dsize, {0}}, 0, 0, 0};
#define value2stbench(stbench, value) \
    TypedData_Get_Struct((value), struct stbench_tag, &stbench_type, (stbench))
#define stbench2value(stbench) \
    TypedData_Wrap_Struct(rb_cSTBench, &stbench_type, (stbench))
#define value2stbench_checked(value) \
    ((stbench) rb_check_typeddata((value), &stbench_type))
static VALUE stbench_initialize(int argc, VALUE *argv, VALUE self) { return self; }
static VALUE stbench_alloc(VALUE klass) { return stbench2value(0); }

/* Init Benchmark */
typedef struct params_init_tag
{
    STBenchKeyType type;
    long n_trial;
    long ht_init_size;
} ParamsInit;
static ParamsInit params_init;

static void
stbench_init_validate_params(void)
{
    assert(params_init.type == KeyTypeNum || params_init.type == KeyTypeStr);
    assert(params_init.n_trial > 0);
    assert(params_init.ht_init_size >= 0);
}

static VALUE
stbench_init_setup(int argc, VALUE argv[], VALUE self)
{
    VALUE arg_type, arg_ht_init_size, arg_scale;

    assert(running_scenario == STBenchNotSet);
    running_scenario = STBenchInit;

    rb_scan_args(argc, argv, "30", &arg_type, &arg_ht_init_size, &arg_scale);

    Check_Type(arg_type, T_STRING);

    params_init = (ParamsInit) {
	.type = parse_key_type(rb_string_value_cstr(&arg_type)),
	.ht_init_size = FIX2INT(arg_ht_init_size),
	.n_trial = FIX2INT(arg_scale) * SCALE_BASE,
    };

    stbench_init_validate_params();

    rss_before = get_rss();

    return self;
}

static VALUE
stbench_init_run(VALUE self) {
    assert(running_scenario == STBenchInit);
    assert(!stb_table);

    switch (params_init.type) {
	case KeyTypeNum:
	    if (params_init.ht_init_size > 0)
		for (int i = 0; i < params_init.n_trial; i++)
		{
		    stb_table = st_init_numtable_with_size(params_init.ht_init_size);
		    st_free_table(stb_table);
		}
	    else
		for (int i = 0; i < params_init.n_trial; i++)
		{
		    stb_table = st_init_numtable();
		    st_free_table(stb_table);
		}
	    break;
	case KeyTypeStr:
	    if (params_init.ht_init_size > 0)
		for (int i = 0; i < params_init.n_trial; i++)
		{
		    stb_table = st_init_strtable_with_size(params_init.ht_init_size);
		    st_free_table(stb_table);
		}
	    else
		for (int i = 0; i < params_init.n_trial; i++)
		{
		    stb_table = st_init_strtable();
		    st_free_table(stb_table);
		}
	    break;
	default:
	    rb_raise(rb_eArgError, "%s: unexpected arguments", __func__);
    }

    return self;
}

static VALUE
stbench_init_cleanup(VALUE self)
{
    rss_after = get_rss();
    REPORT_RSS();
    assert(running_scenario == STBenchInit);
    running_scenario = STBenchNotSet;
    return self;
}

/* Insert Benchmark */
typedef struct params_insert_tag
{
    STBenchKeyType type;
    STBenchPattern pattern;
    int n_trial;
    int ht_init_size;
    int key_len;
} ParamsInsert;
static ParamsInsert params_insert;

static void
stbench_insert_validate_params(void)
{
    int acc = 1;
    assert(params_insert.type == KeyTypeNum || params_insert.type == KeyTypeStr);
    assert(params_insert.pattern == PatternSame ||
	    params_insert.pattern == PatternDifferent ||
	    params_insert.pattern == PatternRandom);
    assert(params_insert.n_trial > 0);
    assert(params_insert.ht_init_size >= 0);
    assert(params_insert.key_len > 0);
    for (int i = 0; i < params_insert.key_len; i++)
    {
	if (acc > INT_MAX / N_OF_CHARS)
	    return;
	acc*=N_OF_CHARS;
    }
    if (params_insert.n_trial*2 > acc) /* Safety for Different pattern */
	rb_raise(rb_eArgError, "key_len is too short");
}

static VALUE
stbench_insert_setup(int argc, VALUE argv[], VALUE self)
{
    VALUE arg_type, arg_ht_init_size, arg_scale, arg_pattern, arg_key_len;

    assert(running_scenario == STBenchNotSet);
    running_scenario = STBenchInsert;

    rb_scan_args(argc, argv, "50", &arg_type, &arg_ht_init_size, &arg_scale, &arg_pattern, &arg_key_len);

    Check_Type(arg_type, T_STRING);
    Check_Type(arg_pattern, T_STRING);

    params_insert = (ParamsInsert) {
	.type = parse_key_type(rb_string_value_cstr(&arg_type)),
	.ht_init_size = FIX2INT(arg_ht_init_size),
	.n_trial = FIX2INT(arg_scale) * SCALE_BASE,
	.pattern = parse_pattern(rb_string_value_cstr(&arg_pattern)),
	.key_len = FIX2INT(arg_key_len),
    };

    stbench_insert_validate_params();

    assert(!stb_table);

    switch (params_insert.type) {
	case KeyTypeNum:
	    if (params_insert.ht_init_size > 0)
		stb_table = st_init_numtable_with_size(params_insert.ht_init_size);
	    else
		stb_table = st_init_numtable();
	    break;
	case KeyTypeStr:
	    if (params_insert.ht_init_size > 0)
		stb_table = st_init_strtable_with_size(params_insert.ht_init_size);
	    else
		stb_table = st_init_strtable();
	    break;
	default:
	    rb_raise(rb_eArgError, "%s: unexpected arguments", __func__);
    }

    switch (params_insert.type) {
	case KeyTypeNum:
	    assert(!int_array);
	    int_array = xmalloc(sizeof(int) * params_insert.n_trial);
	    if (params_insert.pattern == PatternSame)
		fill_int_array_diff(params_insert.n_trial); /* Dummy for memory usage comparison */
	    else if (params_insert.pattern == PatternDifferent)
		fill_int_array_diff(params_insert.n_trial);
	    else if (params_insert.pattern == PatternRandom)
		fill_int_array_random(params_insert.n_trial);
	    break;
	case KeyTypeStr:
	    assert(!str_array);
	    str_array = xmalloc(sizeof(char *) * params_insert.n_trial);
	    for (int i = 0; i < params_insert.n_trial; i++)
		str_array[i] = xmalloc(sizeof(char) * (params_insert.key_len + 1)); /* +1 for NUL */
	    if (params_insert.pattern == PatternSame)
		fill_str_array_random(params_insert.n_trial, params_insert.key_len); /* Dummy for memory usage comparison */
	    else if (params_insert.pattern == PatternDifferent)
		rb_raise(rb_eRuntimeError, "%s: not implemented", __func__);
	    else if (params_insert.pattern == PatternRandom)
		fill_str_array_random(params_insert.n_trial, params_insert.key_len);
	    break;
	default:
	    rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);
	    break;
    }

    rss_before = get_rss();

    return self;
}

static VALUE
stbench_insert_run(VALUE self) {
    switch (params_insert.type) {
	case KeyTypeNum:
	    if (params_insert.pattern == PatternSame)
		for (int i = 0; i < params_insert.n_trial; i++)
		    st_insert(stb_table, 123, 456);
	    else if (params_insert.pattern == PatternDifferent)
		for (int i = 0; i < params_insert.n_trial; i++)
		    st_insert(stb_table, int_array[i], 456);
	    else if (params_insert.pattern == PatternRandom)
		for (int i = 0; i < params_insert.n_trial; i++)
		    st_insert(stb_table, int_array[i], 456);
	    break;
	case KeyTypeStr:
	    if (params_insert.pattern == PatternSame)
		for (int i = 0; i < params_insert.n_trial; i++)
		    st_insert(stb_table, (st_data_t) str_array[123], 456);
	    else if (params_insert.pattern == PatternDifferent)
		rb_raise(rb_eRuntimeError, "%s: not implemented", __func__);
	    else if (params_insert.pattern == PatternRandom)
		for (int i = 0; i < params_insert.n_trial; i++)
		    st_insert(stb_table, (st_data_t) str_array[i], 456);
	    break;
	default:
	    rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);
	    break;
    }

    return self;
}

static VALUE
stbench_insert_cleanup(VALUE self)
{
    rss_after = get_rss();
    REPORT_RSS();

    assert(running_scenario == STBenchInsert);
    running_scenario = STBenchNotSet;

    if (params_insert.type == KeyTypeNum)
    {
	assert(int_array);
	xfree(int_array);
	int_array = NULL;
    }
    else if (params_insert.type == KeyTypeStr)
    {
	assert(str_array);
	xfree(str_array);
	str_array = NULL;
    }
    else
	rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);

    assert(stb_table);
    st_free_table(stb_table);
    stb_table = NULL;

    return self;
}

/* Search Benchmark */
typedef struct params_search_tag
{
    STBenchKeyType type;
    STBenchPattern pattern;
    int n_trial;
    int ht_init_size;
    int key_len;
} ParamsSearch;
static ParamsSearch params_search;

static void
stbench_search_validate_params(void)
{
    int acc = 1;
    assert(params_search.type == KeyTypeNum || params_search.type == KeyTypeStr);
    assert(params_search.pattern == PatternSame ||
	    params_search.pattern == PatternDifferent ||
	    params_search.pattern == PatternRandom);
    assert(params_search.n_trial > 0);
    assert(params_search.ht_init_size >= 0);
    for (int i = 0; i < params_search.key_len; i++)
    {
	if (acc > INT_MAX/N_OF_CHARS)
	    return;
	acc*=N_OF_CHARS;
    }
    if (params_search.n_trial*2 > acc) /* Safety for Different pattern */
	rb_raise(rb_eArgError, "key_len is too short");
}

static VALUE
stbench_search_setup(int argc, VALUE argv[], VALUE self) {
    VALUE arg_type, arg_ht_init_size, arg_scale, arg_pattern, arg_key_len;

    assert(running_scenario == STBenchNotSet);
    running_scenario = STBenchSearch;

    rb_scan_args(argc, argv, "50", &arg_type, &arg_ht_init_size, &arg_scale, &arg_pattern, &arg_key_len);

    Check_Type(arg_type, T_STRING);
    Check_Type(arg_pattern, T_STRING);

    params_search = (ParamsSearch) {
	.type = parse_key_type(rb_string_value_cstr(&arg_type)),
	.ht_init_size = FIX2INT(arg_ht_init_size),
	.n_trial = FIX2INT(arg_scale) * SCALE_BASE,
	.pattern = parse_pattern(rb_string_value_cstr(&arg_pattern)),
	.key_len = FIX2INT(arg_key_len),
    };

    stbench_search_validate_params();

    switch (params_search.type) {
	case KeyTypeNum:
	    if (params_search.ht_init_size > 0)
		stb_table = st_init_numtable_with_size(params_search.ht_init_size);
	    else
		stb_table = st_init_numtable();
	    break;
	case KeyTypeStr:
	    if (params_search.ht_init_size > 0)
		stb_table = st_init_strtable_with_size(params_search.ht_init_size);
	    else
		stb_table = st_init_strtable();
	    break;
	default:
	    rb_raise(rb_eArgError, "%s: unexpected arguments", __func__);
    }

    switch (params_search.type) {
	case KeyTypeNum:
	    assert(!int_array);
	    int_array = xmalloc(sizeof(int) * params_search.n_trial * 2);
	    if (params_search.pattern == PatternSame) {
		fill_int_array_diff(params_search.n_trial);
		for (int i = 0; i < params_search.n_trial; i++)
		    st_insert(stb_table, int_array[i], 456);
		fill_int_array_diff(params_search.n_trial);
	    }
	    else if (params_search.pattern == PatternDifferent) {
		fill_int_array_diff(params_search.n_trial * 2);
		for (int i = 0; i < params_search.n_trial; i++)
		    st_insert(stb_table, int_array[params_search.n_trial - 1 - i], 456);
	    }
	    else if (params_search.pattern == PatternRandom) {
		fill_int_array_diff(params_search.n_trial * 2);
		for (int i = 0; i < params_search.n_trial; i++)
		    st_insert(stb_table, int_array[i], 456);
		fill_int_array_diff(params_search.n_trial * 2);
	    }
	    break;
	case KeyTypeStr:
	    assert(!str_array);
	    str_array = xmalloc(sizeof(char *) * params_search.n_trial * 2);
	    for (int i = 0; i < params_search.n_trial * 2; i++)
		str_array[i] = xmalloc(sizeof(char) * (params_search.key_len + 1)); /* +1 for NUL */

	    /* TODO: fake implementation, have to implement different */
	    fprintf(stderr, "XXX: fake implementation, have to implement fill diff.\n");
	    if (params_search.pattern == PatternSame) {
		fill_str_array_random(params_search.n_trial, params_search.key_len);
		for (int i = 0; i < params_search.n_trial; i++)
		    st_insert(stb_table, (st_data_t)str_array[i], 456);
		//fill_str_array_random(params_search.n_trial, params_search.key_len);
	    }
	    else if (params_search.pattern == PatternDifferent) {
		/* TODO: fake implementation, have to implement different */
		fill_str_array_random(params_search.n_trial * 2, params_search.key_len);
		for (int i = 0; i < params_search.n_trial; i++)
		    st_insert(stb_table, (st_data_t)str_array[params_search.n_trial - 1 - i], 456);
	    }
	    else if (params_search.pattern == PatternRandom) {
		fill_str_array_random(params_search.n_trial * 2, params_search.key_len);
		for (int i = 0; i < params_search.n_trial; i++)
		    st_insert(stb_table, (st_data_t)str_array[i], 456);
		fill_str_array_random(params_search.n_trial * 2, params_search.key_len);
	    }
	    break;
	default:
	    rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);
	    break;
    }

    rss_before = get_rss();

    return self;
}
static VALUE
stbench_search_run(VALUE self) {
    st_data_t ret_value;
    switch (params_search.type) {
	case KeyTypeNum:
	    for (int i = 0; i < params_search.n_trial; i++)
		st_lookup(stb_table, int_array[i], &ret_value);
	    break;
	case KeyTypeStr:
	    for (int i = 0; i < params_search.n_trial; i++)
		st_lookup(stb_table, (st_data_t)str_array[i], &ret_value);
	    break;
	default:
	    rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);
	    break;
    }

    return self;
}
static VALUE
stbench_search_cleanup(VALUE self) {
    rss_after = get_rss();
    REPORT_RSS();

    assert(running_scenario == STBenchSearch);
    running_scenario = STBenchNotSet;

    if (params_search.type == KeyTypeNum)
    {
	assert(int_array);
	xfree(int_array);
	int_array = NULL;
    }
    else if (params_search.type == KeyTypeStr)
    {
	assert(str_array);
	xfree(str_array);
	str_array = NULL;
    }
    else
	rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);

    assert(stb_table);
    st_free_table(stb_table);
    stb_table = NULL;

    return self;
}

/* Delete Benchmark */
typedef struct params_delete_tag
{
    STBenchKeyType type;
    STBenchPattern pattern;
    int n_trial;
    int ht_init_size;
    int key_len;
} ParamsDelete;
static ParamsDelete params_delete;

static void
stbench_delete_validate_params(void)
{
    int acc = 1;
    assert(params_delete.type == KeyTypeNum || params_delete.type == KeyTypeStr);
    assert(params_delete.pattern == PatternSame ||
	    params_delete.pattern == PatternDifferent ||
	    params_delete.pattern == PatternRandom);
    assert(params_delete.n_trial > 0);
    assert(params_delete.ht_init_size >= 0);
    for (int i = 0; i < params_delete.key_len; i++)
    {
	if (acc > INT_MAX/N_OF_CHARS)
	    return;
	acc*=N_OF_CHARS;
    }
    if (params_delete.n_trial*2 > acc) /* Safety for Different pattern */
	rb_raise(rb_eArgError, "key_len is too short");
}

static VALUE
stbench_delete_setup(int argc, VALUE argv[], VALUE self) {
    VALUE arg_type, arg_ht_init_size, arg_scale, arg_pattern, arg_key_len;

    assert(running_scenario == STBenchNotSet);
    running_scenario = STBenchDelete;

    rb_scan_args(argc, argv, "50", &arg_type, &arg_ht_init_size, &arg_scale, &arg_pattern, &arg_key_len);

    Check_Type(arg_type, T_STRING);
    Check_Type(arg_pattern, T_STRING);

    params_delete = (ParamsDelete) {
	.type = parse_key_type(rb_string_value_cstr(&arg_type)),
	.ht_init_size = FIX2INT(arg_ht_init_size),
	.n_trial = FIX2INT(arg_scale) * SCALE_BASE,
	.pattern = parse_pattern(rb_string_value_cstr(&arg_pattern)),
	.key_len = FIX2INT(arg_key_len),
    };

    stbench_delete_validate_params();

    switch (params_delete.type) {
	case KeyTypeNum:
	    if (params_delete.ht_init_size > 0)
		stb_table = st_init_numtable_with_size(params_delete.ht_init_size);
	    else
		stb_table = st_init_numtable();
	    break;
	case KeyTypeStr:
	    if (params_delete.ht_init_size > 0)
		stb_table = st_init_strtable_with_size(params_delete.ht_init_size);
	    else
		stb_table = st_init_strtable();
	    break;
	default:
	    rb_raise(rb_eArgError, "%s: unexpected arguments", __func__);
    }

    switch (params_delete.type) {
	case KeyTypeNum:
	    assert(!int_array);
	    int_array = xmalloc(sizeof(int) * params_delete.n_trial * 2);
	    if (params_delete.pattern == PatternSame) {
		fill_int_array_diff(params_delete.n_trial);
		for (int i = 0; i < params_delete.n_trial; i++)
		    st_insert(stb_table, int_array[i], 456);
		fill_int_array_diff(params_delete.n_trial);
	    }
	    else if (params_delete.pattern == PatternDifferent) {
		fill_int_array_diff(params_delete.n_trial * 2);
		for (int i = 0; i < params_delete.n_trial; i++)
		    st_insert(stb_table, int_array[params_delete.n_trial - 1 - i], 456);
	    }
	    else if (params_delete.pattern == PatternRandom) {
		fill_int_array_diff(params_delete.n_trial * 2);
		for (int i = 0; i < params_delete.n_trial; i++)
		    st_insert(stb_table, int_array[i], 456);
		fill_int_array_diff(params_delete.n_trial * 2);
	    }
	    break;
	case KeyTypeStr:
	    assert(!str_array);
	    str_array = xmalloc(sizeof(char *) * params_delete.n_trial * 2);
	    for (int i = 0; i < params_delete.n_trial * 2; i++)
		str_array[i] = xmalloc(sizeof(char) * (params_delete.key_len + 1)); /* +1 for NUL */

	    /* TODO: fake implementation, have to implement different */
	    fprintf(stderr, "XXX: fake implementation, have to implement fill diff.\n");
	    if (params_delete.pattern == PatternSame) {
		fill_str_array_random(params_delete.n_trial, params_delete.key_len);
		for (int i = 0; i < params_delete.n_trial; i++)
		    st_insert(stb_table, (st_data_t)str_array[i], 456);
		//fill_str_array_random(params_delete.n_trial, params_delete.key_len);
	    }
	    else if (params_delete.pattern == PatternDifferent) {
		/* TODO: fake implementation, have to implement different */
		fill_str_array_random(params_delete.n_trial * 2, params_delete.key_len);
		for (int i = 0; i < params_delete.n_trial; i++)
		    st_insert(stb_table, (st_data_t)str_array[params_delete.n_trial - 1 - i], 456);
	    }
	    else if (params_delete.pattern == PatternRandom) {
		fill_str_array_random(params_delete.n_trial * 2, params_delete.key_len);
		for (int i = 0; i < params_delete.n_trial; i++)
		    st_insert(stb_table, (st_data_t)str_array[i], 456);
		fill_str_array_random(params_delete.n_trial * 2, params_delete.key_len);
	    }
	    break;
	default:
	    rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);
	    break;
    }

    rss_before = get_rss();

    return self;
}
static VALUE
stbench_delete_run(VALUE self) {
    st_data_t ret_value;
    switch (params_delete.type) {
	case KeyTypeNum:
	    for (int i = 0; i < params_delete.n_trial; i++)
		st_lookup(stb_table, int_array[i], &ret_value);
	    break;
	case KeyTypeStr:
	    for (int i = 0; i < params_delete.n_trial; i++)
		st_lookup(stb_table, (st_data_t)str_array[i], &ret_value);
	    break;
	default:
	    rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);
	    break;
    }
    return self;
}
static VALUE
stbench_delete_cleanup(VALUE self) {
    rss_after = get_rss();
    REPORT_RSS();

    assert(running_scenario == STBenchDelete);
    running_scenario = STBenchNotSet;

    if (params_delete.type == KeyTypeNum)
    {
	assert(int_array);
	xfree(int_array);
	int_array = NULL;
    }
    else if (params_delete.type == KeyTypeStr)
    {
	assert(str_array);
	xfree(str_array);
	str_array = NULL;
    }
    else
	rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);

    assert(stb_table);
    st_free_table(stb_table);
    stb_table = NULL;

    return self;
}

void
Init_STBench(void) {
#undef rb_intern
#define rb_inetrn(rope) rb_intern_const(rope)
    rb_cSTBench = rb_define_class("STBench", rb_cData);
    rb_define_alloc_func(rb_cSTBench, stbench_alloc);
    rb_define_private_method(rb_cSTBench, "initialize", stbench_initialize, -1);

    rb_define_method(rb_cSTBench, "init_setup", stbench_init_setup, -1);
    rb_define_method(rb_cSTBench, "init_run", stbench_init_run, 0);
    rb_define_method(rb_cSTBench, "init_cleanup", stbench_init_cleanup, 0);
    rb_define_method(rb_cSTBench, "insert_setup", stbench_insert_setup, -1);
    rb_define_method(rb_cSTBench, "insert_run", stbench_insert_run, 0);
    rb_define_method(rb_cSTBench, "insert_cleanup", stbench_insert_cleanup, 0);
    rb_define_method(rb_cSTBench, "search_setup", stbench_search_setup, -1);
    rb_define_method(rb_cSTBench, "search_run", stbench_search_run, 0);
    rb_define_method(rb_cSTBench, "search_cleanup", stbench_search_cleanup, 0);
    rb_define_method(rb_cSTBench, "delete_setup", stbench_delete_setup, -1);
    rb_define_method(rb_cSTBench, "delete_run", stbench_delete_run, 0);
    rb_define_method(rb_cSTBench, "delete_cleanup", stbench_delete_cleanup, 0);
}
