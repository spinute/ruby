#include "ruby.h"
#include "ruby/st.h"
#include <stdio.h>

static VALUE rb_cSTBench;

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
#define stbench2value(stbench) TypedData_Wrap_Struct(rb_cSTBench, &stbench_type, (stbench))
#define value2stbench_checked(value) \
	((stbench) rb_check_typeddata((value), &stbench_type))

static VALUE
stbench_alloc(VALUE klass) {
	return stbench2value(0);
}

typedef struct stbench_tag
{
    struct st_table ht;
} *STBench;

static VALUE
stbench_init(VALUE self)
{
    printf("init called");
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
    printf("insert called");
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
    rb_define_private_method(rb_cSTBench, "initialize", stbench_init, 0);

    rb_define_method(rb_cSTBench, "call_init", stbench_init_only, 0);
    rb_define_method(rb_cSTBench, "insert", stbench_insert, 0);
    rb_define_method(rb_cSTBench, "delete", stbench_delete, 0);
    rb_define_method(rb_cSTBench, "search", stbench_search, 0);
}
