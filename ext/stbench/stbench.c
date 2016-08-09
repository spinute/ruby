#include "ruby.h"
#include "ruby/st.h"
#include <stdio.h>

static VALUE rb_cSTBench;

static void
usage(void)
{
    printf("bench = STBench(type[, size, scale, senario])");
    printf("type = 'num' | 'str'");
    printf("bench ");
}

static void stbench_dmark(void *stbench) { (void) stbench; }
static void stbench_dfree(void *stbench) { (void) stbench; }
static size_t stbench_dsize(const void *stbench) { (void) stbench; return 0; }
const rb_data_type_t stbench_type = {
    "cstbench", {stbench_dmark, stbench_dfree, stbench_dsize, {0}}, 0, 0, 0};
#define value2stbench(stbench, value) \
	TypedData_Get_Struct((value), struct stbench_tag, &stbench_type, (stbench))
#define stbench2value(stbench) TypedData_Wrap_Struct(rb_cSTBench, &stbench_type, (stbench))
#define value2stbench_checked(value) \
	((stbench) rb_check_typeddata((value), &stbench_type))
static VALUE stbench_alloc(VALUE klass) { return stbench2value(0); }

static st_table *stb_table = NULL;
typedef enum {KeyNum, KeyStr, Unset} st_k_type;
st_k_type key_type = Unset;
#define SCALE_BASE 100000
static int n_trial = 1 * SCALE_BASE;

static VALUE
stbench_init(int argc, VALUE *argv, VALUE self)
{
    VALUE arg_type, arg_ht_size, arg_scale;
    char *vtype;

    if (stb_table)
	st_free_table(stb_table);

    rb_scan_args(argc, argv, "12", &arg_type, &arg_ht_size, &arg_scale);

    Check_Type(arg_type, T_STRING);

    vtype = rb_string_value_cstr(&arg_type);

    if (arg_scale != Qnil)
    {
	Check_Type(arg_scale, T_FIXNUM);
	n_trial = FIX2INT(arg_scale) * SCALE_BASE;
    }

    if (arg_ht_size != Qnil)
    {
	int ht_size;
	Check_Type(arg_ht_size, T_FIXNUM);
	ht_size = FIX2INT(arg_ht_size);

	if (strcmp(vtype, "num") == 0)
	{
	    key_type = KeyNum;
	    stb_table = st_init_numtable_with_size(ht_size);
	}
	else if (strcmp(vtype, "str") == 0)
	{
	    key_type = KeyStr;
	    stb_table = st_init_strtable_with_size(FIX2INT(arg_ht_size));
	}
	else
	{
	    usage();
	    rb_raise(rb_eArgError, "%s: unexpected arguments", __func__);
	}
    }
    else
    {
	if (strcmp(vtype, "num") == 0)
	{
	    key_type = KeyNum;
	    stb_table = st_init_numtable();
	}
	else if (strcmp(vtype, "str") == 0)
	{
	    key_type = KeyStr;
	    stb_table = st_init_strtable();
	}
	else
	{
	    usage();
	    rb_raise(rb_eArgError, "%s: unexpected arguments", __func__);
	}
    }



    return self;
}

static VALUE
stbench_init_only(VALUE self)
{
    printf("initonly called");
    return self;
}

static VALUE
stbench_insert(VALUE self)
{
    switch (key_type)
    {
    case KeyNum:
	st_insert(stb_table, 123, 456);
	break;
    case KeyStr:
	st_insert(stb_table, (st_data_t)"aoeusnth", (st_data_t)"htnsueoa");
	break;
    default:
	rb_raise(rb_eRuntimeError, "%s: unexpected key type", __func__);
    }

    return self;
}
static VALUE
stbench_delete(VALUE self)
{
    printf("delete called");
    return self;
}
static VALUE
stbench_search(VALUE self)
{
    printf("search called");
    return self;
}

void
Init_STBench(void)
{
#undef rb_intern
#define rb_inetrn(rope) rb_intern_const(rope)
    rb_cSTBench = rb_define_class("STBench", rb_cData);
    rb_define_alloc_func(rb_cSTBench, stbench_alloc);
    rb_define_private_method(rb_cSTBench, "initialize", stbench_init, -1);

    rb_define_method(rb_cSTBench, "call_init", stbench_init_only, -1);
    rb_define_method(rb_cSTBench, "insert", stbench_insert, 0);
    rb_define_method(rb_cSTBench, "delete", stbench_delete, 0);
    rb_define_method(rb_cSTBench, "search", stbench_search, 0);
}
