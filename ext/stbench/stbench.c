#include "ruby.h"
#include "ruby/st.h"
#include <stdio.h>

static VALUE rb_cSTBench;

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

    rb_define_private_method(rb_cSTBench, "initialize", stbench_init, 0);

    rb_define_method(rb_cSTBench, "call_init", stbench_init_only, 0);
    rb_define_method(rb_cSTBench, "insert", stbench_insert, 0);
    rb_define_method(rb_cSTBench, "delete", stbench_delete, 0);
    rb_define_method(rb_cSTBench, "search", stbench_search, 0);
}
