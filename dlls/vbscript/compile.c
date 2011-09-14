/*
 * Copyright 2011 Jacek Caban for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <assert.h>

#include "vbscript.h"
#include "parse.h"
#include "parser.tab.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(vbscript);
WINE_DECLARE_DEBUG_CHANNEL(vbscript_disas);

typedef struct {
    parser_ctx_t parser;

    unsigned instr_cnt;
    unsigned instr_size;
    vbscode_t *code;

    unsigned *labels;
    unsigned labels_size;
    unsigned labels_cnt;

    unsigned sub_end_label;

    dim_decl_t *dim_decls;
    dynamic_var_t *global_vars;

    function_t *func;
    function_t *funcs;
    function_decl_t *func_decls;
} compile_ctx_t;

static HRESULT compile_expression(compile_ctx_t*,expression_t*);
static HRESULT compile_statement(compile_ctx_t*,statement_t*);

static const struct {
    const char *op_str;
    instr_arg_type_t arg1_type;
    instr_arg_type_t arg2_type;
} instr_info[] = {
#define X(n,a,b,c) {#n,b,c},
OP_LIST
#undef X
};

static void dump_instr_arg(instr_arg_type_t type, instr_arg_t *arg)
{
    switch(type) {
    case ARG_STR:
    case ARG_BSTR:
        TRACE_(vbscript_disas)("\t%s", debugstr_w(arg->str));
        break;
    case ARG_INT:
        TRACE_(vbscript_disas)("\t%d", arg->uint);
        break;
    case ARG_UINT:
    case ARG_ADDR:
        TRACE_(vbscript_disas)("\t%u", arg->uint);
        break;
    case ARG_DOUBLE:
        TRACE_(vbscript_disas)("\t%lf", *arg->dbl);
        break;
    case ARG_NONE:
        break;
    default:
        assert(0);
    }
}

static void dump_code(compile_ctx_t *ctx)
{
    instr_t *instr;

    for(instr = ctx->code->instrs; instr < ctx->code->instrs+ctx->instr_cnt; instr++) {
        TRACE_(vbscript_disas)("%d:\t%s", instr-ctx->code->instrs, instr_info[instr->op].op_str);
        dump_instr_arg(instr_info[instr->op].arg1_type, &instr->arg1);
        dump_instr_arg(instr_info[instr->op].arg2_type, &instr->arg2);
        TRACE_(vbscript_disas)("\n");
    }
}

static inline void *compiler_alloc(vbscode_t *vbscode, size_t size)
{
    return vbsheap_alloc(&vbscode->heap, size);
}

static WCHAR *compiler_alloc_string(vbscode_t *vbscode, const WCHAR *str)
{
    size_t size;
    WCHAR *ret;

    size = (strlenW(str)+1)*sizeof(WCHAR);
    ret = compiler_alloc(vbscode, size);
    if(ret)
        memcpy(ret, str, size);
    return ret;
}

static inline instr_t *instr_ptr(compile_ctx_t *ctx, unsigned id)
{
    assert(id < ctx->instr_cnt);
    return ctx->code->instrs + id;
}

static unsigned push_instr(compile_ctx_t *ctx, vbsop_t op)
{
    assert(ctx->instr_size && ctx->instr_size >= ctx->instr_cnt);

    if(ctx->instr_size == ctx->instr_cnt) {
        instr_t *new_instr;

        new_instr = heap_realloc(ctx->code->instrs, ctx->instr_size*2*sizeof(instr_t));
        if(!new_instr)
            return -1;

        ctx->code->instrs = new_instr;
        ctx->instr_size *= 2;
    }

    ctx->code->instrs[ctx->instr_cnt].op = op;
    return ctx->instr_cnt++;
}

static HRESULT push_instr_int(compile_ctx_t *ctx, vbsop_t op, LONG arg)
{
    unsigned ret;

    ret = push_instr(ctx, op);
    if(ret == -1)
        return E_OUTOFMEMORY;

    instr_ptr(ctx, ret)->arg1.lng = arg;
    return S_OK;
}

static HRESULT push_instr_addr(compile_ctx_t *ctx, vbsop_t op, unsigned arg)
{
    unsigned ret;

    ret = push_instr(ctx, op);
    if(ret == -1)
        return E_OUTOFMEMORY;

    instr_ptr(ctx, ret)->arg1.uint = arg;
    return S_OK;
}

static HRESULT push_instr_str(compile_ctx_t *ctx, vbsop_t op, const WCHAR *arg)
{
    unsigned instr;
    WCHAR *str;

    str = compiler_alloc_string(ctx->code, arg);
    if(!str)
        return E_OUTOFMEMORY;

    instr = push_instr(ctx, op);
    if(instr == -1)
        return E_OUTOFMEMORY;

    instr_ptr(ctx, instr)->arg1.str = str;
    return S_OK;
}

static HRESULT push_instr_double(compile_ctx_t *ctx, vbsop_t op, double arg)
{
    unsigned instr;
    double *d;

    d = compiler_alloc(ctx->code, sizeof(double));
    if(!d)
        return E_OUTOFMEMORY;

    instr = push_instr(ctx, op);
    if(instr == -1)
        return E_OUTOFMEMORY;

    *d = arg;
    instr_ptr(ctx, instr)->arg1.dbl = d;
    return S_OK;
}

static BSTR alloc_bstr_arg(compile_ctx_t *ctx, const WCHAR *str)
{
    if(!ctx->code->bstr_pool_size) {
        ctx->code->bstr_pool = heap_alloc(8 * sizeof(BSTR));
        if(!ctx->code->bstr_pool)
            return NULL;
        ctx->code->bstr_pool_size = 8;
    }else if(ctx->code->bstr_pool_size == ctx->code->bstr_cnt) {
       BSTR *new_pool;

        new_pool = heap_realloc(ctx->code->bstr_pool, ctx->code->bstr_pool_size*2*sizeof(BSTR));
        if(!new_pool)
            return NULL;

        ctx->code->bstr_pool = new_pool;
        ctx->code->bstr_pool_size *= 2;
    }

    ctx->code->bstr_pool[ctx->code->bstr_cnt] = SysAllocString(str);
    if(!ctx->code->bstr_pool[ctx->code->bstr_cnt])
        return NULL;

    return ctx->code->bstr_pool[ctx->code->bstr_cnt++];
}

static HRESULT push_instr_bstr(compile_ctx_t *ctx, vbsop_t op, const WCHAR *arg)
{
    unsigned instr;
    BSTR bstr;

    bstr = alloc_bstr_arg(ctx, arg);
    if(!bstr)
        return E_OUTOFMEMORY;

    instr = push_instr(ctx, op);
    if(instr == -1)
        return E_OUTOFMEMORY;

    instr_ptr(ctx, instr)->arg1.bstr = bstr;
    return S_OK;
}

static HRESULT push_instr_bstr_uint(compile_ctx_t *ctx, vbsop_t op, const WCHAR *arg1, unsigned arg2)
{
    unsigned instr;
    BSTR bstr;

    bstr = alloc_bstr_arg(ctx, arg1);
    if(!bstr)
        return E_OUTOFMEMORY;

    instr = push_instr(ctx, op);
    if(instr == -1)
        return E_OUTOFMEMORY;

    instr_ptr(ctx, instr)->arg1.bstr = bstr;
    instr_ptr(ctx, instr)->arg2.uint = arg2;
    return S_OK;
}

#define LABEL_FLAG 0x80000000

static unsigned alloc_label(compile_ctx_t *ctx)
{
    if(!ctx->labels_size) {
        ctx->labels = heap_alloc(8 * sizeof(*ctx->labels));
        if(!ctx->labels)
            return -1;
        ctx->labels_size = 8;
    }else if(ctx->labels_size == ctx->labels_cnt) {
        unsigned *new_labels;

        new_labels = heap_realloc(ctx->labels, 2*ctx->labels_size*sizeof(*ctx->labels));
        if(!new_labels)
            return -1;

        ctx->labels = new_labels;
        ctx->labels_size *= 2;
    }

    return ctx->labels_cnt++ | LABEL_FLAG;
}

static inline void label_set_addr(compile_ctx_t *ctx, unsigned label)
{
    assert(label & LABEL_FLAG);
    ctx->labels[label & ~LABEL_FLAG] = ctx->instr_cnt;
}

static HRESULT compile_args(compile_ctx_t *ctx, expression_t *args, unsigned *ret)
{
    unsigned arg_cnt = 0;
    HRESULT hres;

    while(args) {
        hres = compile_expression(ctx, args);
        if(FAILED(hres))
            return hres;

        arg_cnt++;
        args = args->next;
    }

    *ret = arg_cnt;
    return S_OK;
}

static HRESULT compile_member_expression(compile_ctx_t *ctx, member_expression_t *expr, BOOL ret_val)
{
    unsigned arg_cnt = 0;
    HRESULT hres;

    hres = compile_args(ctx, expr->args, &arg_cnt);
    if(FAILED(hres))
        return hres;

    if(expr->obj_expr) {
        FIXME("obj_expr not implemented\n");
        hres = E_NOTIMPL;
    }else {
        hres = push_instr_bstr_uint(ctx, ret_val ? OP_icall : OP_icallv, expr->identifier, arg_cnt);
    }

    return hres;
}

static HRESULT compile_unary_expression(compile_ctx_t *ctx, unary_expression_t *expr, vbsop_t op)
{
    HRESULT hres;

    hres = compile_expression(ctx, expr->subexpr);
    if(FAILED(hres))
        return hres;

    return push_instr(ctx, op) == -1 ? E_OUTOFMEMORY : S_OK;
}

static HRESULT compile_binary_expression(compile_ctx_t *ctx, binary_expression_t *expr, vbsop_t op)
{
    HRESULT hres;

    hres = compile_expression(ctx, expr->left);
    if(FAILED(hres))
        return hres;

    hres = compile_expression(ctx, expr->right);
    if(FAILED(hres))
        return hres;

    return push_instr(ctx, op) == -1 ? E_OUTOFMEMORY : S_OK;
}

static HRESULT compile_expression(compile_ctx_t *ctx, expression_t *expr)
{
    switch(expr->type) {
    case EXPR_ADD:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_add);
    case EXPR_BOOL:
        return push_instr_int(ctx, OP_bool, ((bool_expression_t*)expr)->value);
    case EXPR_CONCAT:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_concat);
    case EXPR_DIV:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_div);
    case EXPR_DOUBLE:
        return push_instr_double(ctx, OP_double, ((double_expression_t*)expr)->value);
    case EXPR_EMPTY:
        return push_instr(ctx, OP_empty) != -1 ? S_OK : E_OUTOFMEMORY;
    case EXPR_EQUAL:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_equal);
    case EXPR_EXP:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_exp);
    case EXPR_IDIV:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_idiv);
    case EXPR_MEMBER:
        return compile_member_expression(ctx, (member_expression_t*)expr, TRUE);
    case EXPR_MOD:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_mod);
    case EXPR_MUL:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_mul);
    case EXPR_NEG:
        return compile_unary_expression(ctx, (unary_expression_t*)expr, OP_neg);
    case EXPR_NEQUAL:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_nequal);
    case EXPR_NOT:
        return compile_unary_expression(ctx, (unary_expression_t*)expr, OP_not);
    case EXPR_NULL:
        return push_instr(ctx, OP_null) != -1 ? S_OK : E_OUTOFMEMORY;
    case EXPR_STRING:
        return push_instr_str(ctx, OP_string, ((string_expression_t*)expr)->value);
    case EXPR_SUB:
        return compile_binary_expression(ctx, (binary_expression_t*)expr, OP_sub);
    case EXPR_USHORT:
        return push_instr_int(ctx, OP_short, ((int_expression_t*)expr)->value);
    case EXPR_ULONG:
        return push_instr_int(ctx, OP_long, ((int_expression_t*)expr)->value);
    default:
        FIXME("Unimplemented expression type %d\n", expr->type);
        return E_NOTIMPL;
    }

    return S_OK;
}

static HRESULT compile_if_statement(compile_ctx_t *ctx, if_statement_t *stat)
{
    unsigned cnd_jmp, endif_label = -1;
    elseif_decl_t *elseif_decl;
    HRESULT hres;

    hres = compile_expression(ctx, stat->expr);
    if(FAILED(hres))
        return hres;

    cnd_jmp = push_instr(ctx, OP_jmp_false);
    if(cnd_jmp == -1)
        return E_OUTOFMEMORY;

    hres = compile_statement(ctx, stat->if_stat);
    if(FAILED(hres))
        return hres;

    if(stat->else_stat || stat->elseifs) {
        endif_label = alloc_label(ctx);
        if(endif_label == -1)
            return E_OUTOFMEMORY;

        hres = push_instr_addr(ctx, OP_jmp, endif_label);
        if(FAILED(hres))
            return hres;
    }

    for(elseif_decl = stat->elseifs; elseif_decl; elseif_decl = elseif_decl->next) {
        instr_ptr(ctx, cnd_jmp)->arg1.uint = ctx->instr_cnt;

        hres = compile_expression(ctx, elseif_decl->expr);
        if(FAILED(hres))
            return hres;

        cnd_jmp = push_instr(ctx, OP_jmp_false);
        if(cnd_jmp == -1)
            return E_OUTOFMEMORY;

        hres = compile_statement(ctx, elseif_decl->stat);
        if(FAILED(hres))
            return hres;

        hres = push_instr_addr(ctx, OP_jmp, endif_label);
        if(FAILED(hres))
            return hres;
    }

    instr_ptr(ctx, cnd_jmp)->arg1.uint = ctx->instr_cnt;

    if(stat->else_stat) {
        hres = compile_statement(ctx, stat->else_stat);
        if(FAILED(hres))
            return hres;
    }

    if(endif_label != -1)
        label_set_addr(ctx, endif_label);
    return S_OK;
}

static HRESULT compile_assign_statement(compile_ctx_t *ctx, assign_statement_t *stat)
{
    HRESULT hres;

    hres = compile_expression(ctx, stat->value_expr);
    if(FAILED(hres))
        return hres;

    if(stat->member_expr->args) {
        FIXME("arguments support not implemented\n");
        return E_NOTIMPL;
    }

    if(stat->member_expr->obj_expr) {
        hres = compile_expression(ctx, stat->member_expr->obj_expr);
        if(FAILED(hres))
            return hres;

        hres = push_instr_bstr(ctx, OP_assign_member, stat->member_expr->identifier);
    }else {
        hres = push_instr_bstr(ctx, OP_assign_ident, stat->member_expr->identifier);
    }

    return hres;
}

static BOOL lookup_dim_decls(compile_ctx_t *ctx, const WCHAR *name)
{
    dim_decl_t *dim_decl;

    for(dim_decl = ctx->dim_decls; dim_decl; dim_decl = dim_decl->next) {
        if(!strcmpiW(dim_decl->name, name))
            return TRUE;
    }

    return FALSE;
}

static BOOL lookup_args_name(compile_ctx_t *ctx, const WCHAR *name)
{
    unsigned i;

    for(i = 0; i < ctx->func->arg_cnt; i++) {
        if(!strcmpiW(ctx->func->args[i].name, name))
            return TRUE;
    }

    return FALSE;
}

static HRESULT compile_dim_statement(compile_ctx_t *ctx, dim_statement_t *stat)
{
    dim_decl_t *dim_decl = stat->dim_decls;

    while(1) {
        if(lookup_dim_decls(ctx, dim_decl->name) || lookup_args_name(ctx, dim_decl->name)) {
            FIXME("dim %s name redefined\n", debugstr_w(dim_decl->name));
            return E_FAIL;
        }

        if(!dim_decl->next)
            break;
        dim_decl = dim_decl->next;
    }

    dim_decl->next = ctx->dim_decls;
    ctx->dim_decls = stat->dim_decls;
    ctx->func->var_cnt++;
    return S_OK;
}

static HRESULT compile_function_statement(compile_ctx_t *ctx, function_statement_t *stat)
{
    if(ctx->func != &ctx->code->global_code) {
        FIXME("Function is not in the global code\n");
        return E_FAIL;
    }

    stat->func_decl->next = ctx->func_decls;
    ctx->func_decls = stat->func_decl;
    return S_OK;
}

static HRESULT compile_exitsub_statement(compile_ctx_t *ctx)
{
    if(ctx->sub_end_label == -1) {
        FIXME("Exit Sub outside Sub?\n");
        return E_FAIL;
    }

    return push_instr_addr(ctx, OP_jmp, ctx->sub_end_label);
}

static HRESULT compile_statement(compile_ctx_t *ctx, statement_t *stat)
{
    HRESULT hres;

    while(stat) {
        switch(stat->type) {
        case STAT_ASSIGN:
            hres = compile_assign_statement(ctx, (assign_statement_t*)stat);
            break;
        case STAT_CALL:
            hres = compile_member_expression(ctx, ((call_statement_t*)stat)->expr, FALSE);
            break;
        case STAT_DIM:
            hres = compile_dim_statement(ctx, (dim_statement_t*)stat);
            break;
        case STAT_EXITSUB:
            hres = compile_exitsub_statement(ctx);
            break;
        case STAT_FUNC:
            hres = compile_function_statement(ctx, (function_statement_t*)stat);
            break;
        case STAT_IF:
            hres = compile_if_statement(ctx, (if_statement_t*)stat);
            break;
        default:
            FIXME("Unimplemented statement type %d\n", stat->type);
            hres = E_NOTIMPL;
        }

        if(FAILED(hres))
            return hres;
        stat = stat->next;
    }

    return S_OK;
}

static void resolve_labels(compile_ctx_t *ctx, unsigned off)
{
    instr_t *instr;

    for(instr = ctx->code->instrs+off; instr < ctx->code->instrs+ctx->instr_cnt; instr++) {
        if(instr_info[instr->op].arg1_type == ARG_ADDR && (instr->arg1.uint & LABEL_FLAG)) {
            assert((instr->arg1.uint & ~LABEL_FLAG) < ctx->labels_cnt);
            instr->arg1.uint = ctx->labels[instr->arg1.uint & ~LABEL_FLAG];
        }
        assert(instr_info[instr->op].arg2_type != ARG_ADDR);
    }

    ctx->labels_cnt = 0;
}

static HRESULT compile_func(compile_ctx_t *ctx, statement_t *stat, function_t *func)
{
    HRESULT hres;

    func->code_off = ctx->instr_cnt;

    ctx->sub_end_label = -1;

    switch(func->type) {
    case FUNC_SUB:
        ctx->sub_end_label = alloc_label(ctx);
        if(ctx->sub_end_label == -1)
            return E_OUTOFMEMORY;
        break;
    case FUNC_GLOBAL:
        break;
    }

    ctx->func = func;
    ctx->dim_decls = NULL;
    hres = compile_statement(ctx, stat);
    ctx->func = NULL;
    if(FAILED(hres))
        return hres;

    if(ctx->sub_end_label != -1)
        label_set_addr(ctx, ctx->sub_end_label);

    if(push_instr(ctx, OP_ret) == -1)
        return E_OUTOFMEMORY;

    resolve_labels(ctx, func->code_off);

    if(func->var_cnt) {
        dim_decl_t *dim_decl;

        if(func->type == FUNC_GLOBAL) {
            dynamic_var_t *new_var;

            func->var_cnt = 0;

            for(dim_decl = ctx->dim_decls; dim_decl; dim_decl = dim_decl->next) {
                new_var = compiler_alloc(ctx->code, sizeof(*new_var));
                if(!new_var)
                    return E_OUTOFMEMORY;

                new_var->name = compiler_alloc_string(ctx->code, dim_decl->name);
                if(!new_var->name)
                    return E_OUTOFMEMORY;

                V_VT(&new_var->v) = VT_EMPTY;

                new_var->next = ctx->global_vars;
                ctx->global_vars = new_var;
            }
        }else {
            unsigned i;

            func->vars = compiler_alloc(ctx->code, func->var_cnt * sizeof(var_desc_t));
            if(!func->vars)
                return E_OUTOFMEMORY;

            for(dim_decl = ctx->dim_decls, i=0; dim_decl; dim_decl = dim_decl->next, i++) {
                func->vars[i].name = compiler_alloc_string(ctx->code, dim_decl->name);
                if(!func->vars[i].name)
                    return E_OUTOFMEMORY;
            }

            assert(i == func->var_cnt);
        }
    }

    return S_OK;
}

static BOOL lookup_funcs_name(compile_ctx_t *ctx, const WCHAR *name)
{
    function_t *iter;

    for(iter = ctx->funcs; iter; iter = iter->next) {
        if(!strcmpiW(iter->name, name))
            return TRUE;
    }

    return FALSE;
}

static HRESULT create_function(compile_ctx_t *ctx, function_decl_t *decl, function_t **ret)
{
    function_t *func;
    HRESULT hres;

    if(lookup_dim_decls(ctx, decl->name) || lookup_funcs_name(ctx, decl->name)) {
        FIXME("%s: redefinition\n", debugstr_w(decl->name));
        return E_FAIL;
    }

    func = compiler_alloc(ctx->code, sizeof(*func));
    if(!func)
        return E_OUTOFMEMORY;

    func->name = compiler_alloc_string(ctx->code, decl->name);
    if(!func->name)
        return E_OUTOFMEMORY;

    func->vars = NULL;
    func->var_cnt = 0;
    func->code_ctx = ctx->code;
    func->type = decl->type;

    func->arg_cnt = 0;
    if(decl->args) {
        arg_decl_t *arg;
        unsigned i;

        for(arg = decl->args; arg; arg = arg->next)
            func->arg_cnt++;

        func->args = compiler_alloc(ctx->code, func->arg_cnt * sizeof(arg_desc_t));
        if(!func->args)
            return E_OUTOFMEMORY;

        for(i = 0, arg = decl->args; arg; arg = arg->next, i++) {
            func->args[i].name = compiler_alloc_string(ctx->code, arg->name);
            if(!func->args[i].name)
                return E_OUTOFMEMORY;
            func->args[i].by_ref = arg->by_ref;
        }
    }else {
        func->args = NULL;
    }

    hres = compile_func(ctx, decl->body, func);
    if(FAILED(hres))
        return hres;

    *ret = func;
    return S_OK;
}

static BOOL lookup_script_identifier(script_ctx_t *script, const WCHAR *identifier)
{
    dynamic_var_t *var;
    function_t *func;

    for(var = script->global_vars; var; var = var->next) {
        if(!strcmpiW(var->name, identifier))
            return TRUE;
    }

    for(func = script->global_funcs; func; func = func->next) {
        if(!strcmpiW(func->name, identifier))
            return TRUE;
    }

    return FALSE;
}

static HRESULT check_script_collisions(compile_ctx_t *ctx, script_ctx_t *script)
{
    dynamic_var_t *var;
    function_t *func;

    for(var = ctx->global_vars; var; var = var->next) {
        if(lookup_script_identifier(script, var->name)) {
            FIXME("%s: redefined\n", debugstr_w(var->name));
            return E_FAIL;
        }
    }

    for(func = ctx->funcs; func; func = func->next) {
        if(lookup_script_identifier(script, func->name)) {
            FIXME("%s: redefined\n", debugstr_w(func->name));
            return E_FAIL;
        }
    }

    return S_OK;
}

void release_vbscode(vbscode_t *code)
{
    unsigned i;

    list_remove(&code->entry);

    for(i=0; i < code->bstr_cnt; i++)
        SysFreeString(code->bstr_pool[i]);

    vbsheap_free(&code->heap);

    heap_free(code->bstr_pool);
    heap_free(code->source);
    heap_free(code->instrs);
    heap_free(code);
}

static vbscode_t *alloc_vbscode(compile_ctx_t *ctx, const WCHAR *source)
{
    vbscode_t *ret;

    ret = heap_alloc(sizeof(*ret));
    if(!ret)
        return NULL;

    ret->source = heap_strdupW(source);
    if(!ret->source) {
        heap_free(ret);
        return NULL;
    }

    ret->instrs = heap_alloc(32*sizeof(instr_t));
    if(!ret->instrs) {
        release_vbscode(ret);
        return NULL;
    }

    ctx->instr_cnt = 0;
    ctx->instr_size = 32;
    vbsheap_init(&ret->heap);

    ret->option_explicit = ctx->parser.option_explicit;

    ret->bstr_pool = NULL;
    ret->bstr_pool_size = 0;
    ret->bstr_cnt = 0;

    ret->global_code.type = FUNC_GLOBAL;
    ret->global_code.name = NULL;
    ret->global_code.code_ctx = ret;
    ret->global_code.vars = NULL;
    ret->global_code.var_cnt = 0;
    ret->global_code.arg_cnt = 0;
    ret->global_code.args = NULL;

    list_init(&ret->entry);
    return ret;
}

static void release_compiler(compile_ctx_t *ctx)
{
    parser_release(&ctx->parser);
    heap_free(ctx->labels);
    if(ctx->code)
        release_vbscode(ctx->code);
}

HRESULT compile_script(script_ctx_t *script, const WCHAR *src, vbscode_t **ret)
{
    function_t *new_func;
    function_decl_t *func_decl;
    compile_ctx_t ctx;
    vbscode_t *code;
    HRESULT hres;

    hres = parse_script(&ctx.parser, src);
    if(FAILED(hres))
        return hres;

    code = ctx.code = alloc_vbscode(&ctx, src);
    if(!ctx.code)
        return E_OUTOFMEMORY;

    ctx.funcs = NULL;
    ctx.func_decls = NULL;
    ctx.global_vars = NULL;
    ctx.dim_decls = NULL;
    ctx.labels = NULL;
    ctx.labels_cnt = ctx.labels_size = 0;

    hres = compile_func(&ctx, ctx.parser.stats, &ctx.code->global_code);
    if(FAILED(hres)) {
        release_compiler(&ctx);
        return hres;
    }

    for(func_decl = ctx.func_decls; func_decl; func_decl = func_decl->next) {
        hres = create_function(&ctx, func_decl, &new_func);
        if(FAILED(hres)) {
            release_compiler(&ctx);
            return hres;
        }

        new_func->next = ctx.funcs;
        ctx.funcs = new_func;
    }

    hres = check_script_collisions(&ctx, script);
    if(FAILED(hres)) {
        release_compiler(&ctx);
        return hres;
    }

    if(ctx.global_vars) {
        dynamic_var_t *var;

        for(var = ctx.global_vars; var->next; var = var->next);

        var->next = script->global_vars;
        script->global_vars = ctx.global_vars;
    }

    if(ctx.funcs) {
        for(new_func = ctx.funcs; new_func->next; new_func = new_func->next);

        new_func->next = script->global_funcs;
        script->global_funcs = ctx.funcs;
    }

    if(TRACE_ON(vbscript_disas))
        dump_code(&ctx);

    ctx.code = NULL;
    release_compiler(&ctx);

    list_add_tail(&script->code_list, &code->entry);
    *ret = code;
    return S_OK;
}
