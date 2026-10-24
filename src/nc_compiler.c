/*

Copyright 2024-2025 Joachim Stolberg

Permission is hereby granted, free of charge, to any person obtaining a copy of this software
and associated documentation files (the “Software”), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>
#include <stdarg.h>
#include <setjmp.h>
#include "nc.h"
#include "nc_int.h"

#define MAX_XFUNC_PARAMS    8
#define MAX_CODE_PER_LINE   k_MAX_LINE_LEN // max. bytes per line (incl. string literals)
#define BLOCKEND(tok)       (tok == ELSE || tok == NEXT || tok == RBRACE) 

// Expression result types
typedef enum type_t {
    e_NONE = NB_NONE,
    e_NUM = NB_NUM,
    e_STR = NB_STR,
    e_REF = NB_REF,
    e_ANY = NB_ANY,
    e_CNST,
} type_t;

// Define external function
typedef struct {
    uint8_t num_params;
    uint8_t return_type;
    uint8_t type[MAX_XFUNC_PARAMS];
} xfunc_t;

typedef struct {
    uint8_t idx;
    uint16_t pos;
} fwdecl_t;

#define k_MAX_BREAKS    (16)    // 'break' statements in all currently open loops

typedef struct {
    void    *file_ptr;
    fwdecl_t a_forward_decl[cfg_MAX_FW_DECL];
    uint8_t  num_fw_decls;
    uint8_t *p_code;
    uint16_t *p_trace;
    uint16_t pc;
    uint16_t linenum;
    uint16_t err_count;
    uint16_t sym_idx;
    char     a_line[k_MAX_LINE_LEN];
    char     a_buff[k_MAX_LINE_LEN];
    uint32_t a_data[cfg_MAX_NUM_DATA];
    uint8_t  data_idx;
    char    *p_pos;
    char    *p_next;
    uint32_t value;
    uint8_t  next_tok;
    bool     in_func;                // True if compiling inside a function
    uint8_t  local_var_count;        // Number of local variables in current function
    uint16_t local_var_start_idx;    // Symbol table index where local vars start
    uint8_t  func_return_type;       // Return type of current function (0=void, NC_INT32, STR8)
    uint8_t  loop_type;              // Innermost loop: 0 (none), FOR or WHILE
    uint8_t  for_depth;              // Open for-loops in the current function
    uint8_t  num_breaks;             // Pending 'break' jumps, patched at the end of their loop
    uint16_t a_break_pos[k_MAX_BREAKS];
    jmp_buf  jmp_buf;
} comp_inst_t;

static xfunc_t a_XFuncs[cfg_MAX_NUM_XFUNC] = {0};
static uint8_t NumXFuncs = 0;
static sym_t a_Symbol[cfg_MAX_NUM_SYM] = {0};
static uint8_t CurrVarIdx = 0;
static uint16_t StartOfVars = 0;
static comp_inst_t *pCi = NULL;
#ifdef cfg_DEBUG_SUPPORT
static nc_dbg_line_t a_DbgLines[cfg_DBG_MAX_LINES];
static uint16_t NumDbgLines = 0;
static nc_dbg_local_t a_DbgLocals[cfg_DBG_MAX_LOCALS];
static uint16_t NumDbgLocals = 0;
static void dbg_add_line(uint16_t pc, uint16_t line);
static void dbg_drop_lines_from(uint16_t pc);
#endif

static bool get_line(void);
static uint8_t next_token(void);
static uint8_t lookahead(void);
static uint8_t next(void);
static void match(uint8_t expected);
#ifndef cfg_LINE_NUMBERS
static void label(void);
#endif
static void compile_line(void);
static void compile_stmts(void);
static void compile_stmt(void);
static void compile_for(void);
static void compile_if(void);
static void compile_func(void);
static void compile_return(void);
static void compile_var(uint8_t type);
static void compile_func_call(uint16_t idx, bool want_value);
static void compile_str8_var(void);
static void compile_int32_decl(void);
static void compile_arr_access(void);
static void compile_printf(void);
static void compile_end(void);
static type_t compile_xfunc(uint8_t type);
static void compile_break(void);
static void compile_breakpoint(void);
static void compile_reti(void);
static void compile_for_leave(void);
#ifdef cfg_DATA_ACCESS
static void compile_copy(void);
#ifdef cfg_STRING_SUPPORT
static void compile_str2arr(void);
#endif
static void compile_get(uint8_t tok, uint8_t instr, uint8_t instr_s);
static void compile_u8_stmt(void);
static void compile_u16_stmt(void);
static void compile_u32_stmt(void);
#endif
static void compile_const(void);
static void compile_while(void);
static void patch_breaks(uint8_t first_break);
static void compile_dispatch(void);
static void compile_free(void);
static uint16_t sym_add(char *id, uint32_t val, uint8_t type);
static uint16_t sym_add_local(char *id, uint8_t type);
static uint16_t sym_get(char *id);
static void trace_print(void);
static void error(char *err, char *id);
static uint8_t get_num_vars(void);
static void add_default_params(uint8_t num);
static void forward_declaration(uint16_t idx, uint16_t pos);
static void resolve_forward_declarations(void);
static void append_data_to_code(t_VM *vm);
static type_t compile_expression(type_t type);
static type_t compile_and_expr(void);
static type_t compile_not_expr(void);
static type_t compile_comp_expr(void);
static type_t compile_bin_and_expr(void);
static type_t compile_bin_or_expr(void);
static type_t compile_bin_xor_expr(void);
static type_t compile_shift_expr(void);
static type_t compile_add_expr(void);
static type_t compile_term(void);
static type_t compile_neg_factor(void);
static type_t compile_factor(void);

/*************************************************************************************************
** API functions
*************************************************************************************************/
void nc_init(void) {
    // A second nc_init() (configuration replayed without restart) must start from scratch.
    NumXFuncs = 0;
    CurrVarIdx = 0;
    StartOfVars = 0;
    memset(a_Symbol, 0, sizeof(a_Symbol));
    // Add keywords - C-style types
    sym_add("int32", 0, NC_INT32);
    sym_add("str8", 0, STR8);
    sym_add("func", 0, FUNC);
    sym_add("return", 0, RET);
    sym_add("for", 0, FOR);
    sym_add("to", 0, TO);
    sym_add("step", 0, STEP);
    sym_add("if", 0, IF);
    sym_add("else", 0, ELSE);
    sym_add("end", 0, END);
    sym_add("while", 0, WHILE);
    sym_add("and", 0, AND);
    sym_add("or", 0, OR);
    sym_add("not", 0, NOT);
    sym_add("mod", 0, MOD);
    sym_add("break", 0, BREAK);
    sym_add("breakpoint", 0, BREAKPOINT);
    sym_add("reti", 0, RETI);
#ifdef cfg_DATA_ACCESS
    sym_add("copy", 0, COPY);
    // C-style byte access functions
    sym_add("u8", 0, U8);
    sym_add("u16", 0, U16);
    sym_add("u32", 0, U32);
#endif
#ifdef cfg_STRING_SUPPORT    
    sym_add("left$", 0, LEFTS);
    sym_add("right$", 0, RIGHTS);
    sym_add("mid$", 0, MIDS);
    sym_add("len", 0, LEN);
    sym_add("val", 0, VAL);
    sym_add("str$", 0, STRS);
    sym_add("hex$", 0, HEXS);
    sym_add("NULL", 0, NIL);
    sym_add("string$", 0, STRINGS);
    // Same functions without the BASIC '$' suffix
    sym_add("left", 0, LEFTS);
    sym_add("right", 0, RIGHTS);
    sym_add("mid", 0, MIDS);
    sym_add("str", 0, STRS);
    sym_add("hex", 0, HEXS);
    sym_add("string", 0, STRINGS);
#ifdef cfg_DATA_ACCESS
    sym_add("str2arr", 0, STR2ARR);
    sym_add("arr2str$", 0, ARR2STRS);
    sym_add("arr2str", 0, ARR2STRS);
#endif
#endif
    sym_add("const", 0, NC_CONST);
    sym_add("instr", 0, INSTR);
    sym_add("free", 0, FREE);
    sym_add("rnd", 0, RND);
    sym_add("abs", 0, ABS);
    sym_add("printf", 0, PRINTF);
    sym_add("dispatch", 0, DISPATCH);
    StartOfVars = CurrVarIdx;
}

uint8_t nc_define_external_function(char *name, uint8_t num_params, uint8_t *types, uint8_t return_type) {
    if(NumXFuncs >= cfg_MAX_NUM_XFUNC) {
        nc_print("Error: too many external functions\n");
        return 0;
    }
    if(num_params > MAX_XFUNC_PARAMS) {
        nc_print("Error: too many parameters\n");
        return 0;
    }
    sym_add(name, NumXFuncs, XFUNC);
    a_XFuncs[NumXFuncs].num_params = num_params;
    a_XFuncs[NumXFuncs].return_type = return_type;
    for(uint8_t i = 0; i < num_params; i++) {
        a_XFuncs[NumXFuncs].type[i] = types[i];
    }
    StartOfVars = CurrVarIdx;
    return NB_XFUNC + NumXFuncs++;
}

void *nc_create(void) {
    t_VM *vm = malloc(sizeof(t_VM));
    if(vm != NULL) {
        memset(vm, 0, sizeof(t_VM));
        nc_mem_init(vm);
        vm->pc = 1;
        //srand(time(NULL));
    }
    return vm;
}

uint16_t nc_compile(void *pv_vm, void *fp) {
    t_VM *vm = pv_vm;
    uint16_t err_count = 0;

    pCi = malloc(sizeof(comp_inst_t));
    if(pCi == NULL) {
        printf("Error: out of memory\n");
        return 1;
    }
    memset(pCi, 0, sizeof(comp_inst_t));
    for(int i = StartOfVars; i < cfg_MAX_NUM_SYM; i++) {
        a_Symbol[i].name[0] = '\0';
        a_Symbol[i].type = 0;
        a_Symbol[i].value = 0;
    }

    pCi->p_code = vm->code;
#ifdef cfg_TRACE_SUPPORT
    pCi->p_trace = vm->trace;
#endif
#ifdef cfg_DEBUG_SUPPORT
    NumDbgLines = 0;
    NumDbgLocals = 0;
#endif
    CurrVarIdx = 0;
    pCi->pc = 0;
    pCi->file_ptr = fp;
    pCi->linenum = 0;
    pCi->err_count = 0;
    pCi->p_code[pCi->pc++] = 0; // The first byte is reserved (invalid label address)

    setjmp(pCi->jmp_buf);
    while(get_line()) {
        compile_line();
    }

    if(pCi->err_count > 0) {
        vm->code_size = 0;
        err_count = pCi->err_count;
        free(pCi);
        pCi = NULL;
        return err_count;
    }

    compile_end();
    append_data_to_code(vm);
    resolve_forward_declarations();

    vm->code_size = pCi->pc;
    vm->num_vars = get_num_vars();
    err_count = pCi->err_count;
    free(pCi);
    pCi = NULL;
    return err_count;
}

void nc_dump_code(void *pv_vm) {
    t_VM *vm = pv_vm;
    for(uint16_t i = 0; i < vm->code_size; i++) {
        printf("%02X ", vm->code[i]);
        if((i % 32) == 31) {
            printf("\n");
        } 
    }
    printf("\n");
}

void nc_output_symbol_table(void *pv_vm) {
    (void)pv_vm;
    uint8_t idx = 0;

    nc_print("#### Symbol table ####\n");
    nc_print("Variables:\n");
    for(uint16_t i = StartOfVars; i < cfg_MAX_NUM_SYM; i++) {
        if(a_Symbol[i].name[0] != '\0' && a_Symbol[i].type != LABEL)
        {
            nc_print("%2u: %-8s  %s\n", idx++, 
                (a_Symbol[i].type == ID) ? "(number)" : (a_Symbol[i].type == STRID) ? "(string)" : 
                        (a_Symbol[i].type == e_CNST) ? "(const)": "(array)",
                a_Symbol[i].name);
        }
    }
#ifndef cfg_LINE_NUMBERS    
    nc_print("Labels:\n");
    for(uint16_t i = StartOfVars; i < cfg_MAX_NUM_SYM; i++) {
        if(a_Symbol[i].name[0] != '\0' && a_Symbol[i].type == LABEL)
        {
            nc_print("%16s: %u\n", a_Symbol[i].name, a_Symbol[i].value);
        }
    }
#endif
}

// return 0 if not found
uint16_t nc_get_label_address(void *pv_vm, char *name) {
    (void)pv_vm;
    char str[k_MAX_SYM_LEN];
    // Copy symbol name (case-sensitive)
    for(uint16_t i = 0; i < k_MAX_SYM_LEN; i++) {
        str[i] = name[i];
        if(name[i] == '\0') {
            break;
        }
    }

    for(uint16_t i = StartOfVars; i < cfg_MAX_NUM_SYM; i++) {
        if(a_Symbol[i].name[0] != '\0' && a_Symbol[i].type == LABEL && strcmp(a_Symbol[i].name, str) == 0)
        {
            return a_Symbol[i].value;
        }
    }
    return 0;
}

sym_t *nc_get_symbol_table(uint16_t *p_start_idx) {
    *p_start_idx = StartOfVars;
    return a_Symbol;
}

#ifdef cfg_DEBUG_SUPPORT
const nc_dbg_line_t *nc_dbg_get_lines(uint16_t *p_num) {
    *p_num = NumDbgLines;
    return a_DbgLines;
}

const nc_dbg_local_t *nc_dbg_get_locals(uint16_t *p_num) {
    *p_num = NumDbgLocals;
    return a_DbgLocals;
}
#endif

/*************************************************************************************************
** Static functions
*************************************************************************************************/
static bool get_line(void) {
    while(nc_get_code_line(pCi->file_ptr, pCi->a_line, k_MAX_LINE_LEN) != NULL) {
        if(strlen(pCi->a_line) > (k_MAX_LINE_LEN - 2)) {
            error("line too long", NULL);
        }

        // Line-number directive "//#line N": reset the line counter so that
        // error messages refer to the user's own source line numbering.
        // Treated as a plain comment by older firmware (forward compatible).
        {
            char *p = pCi->a_line;
            while(*p == ' ' || *p == '\t') { p++; }
            if(p[0] == '/' && p[1] == '/' && p[2] == '#' &&
               p[3] == 'l' && p[4] == 'i' && p[5] == 'n' && p[6] == 'e' &&
               p[7] == ' ') {
                pCi->linenum = (uint16_t)(atoi(p + 8) - 1);
                continue; // skip the directive line, don't compile it
            }
        }

        pCi->p_pos = pCi->p_next = pCi->a_line;
        pCi->next_tok = next_token();
        while(pCi->next_tok == ':') {
            pCi->p_pos = pCi->p_next;
            pCi->next_tok = next_token();
        }

#ifndef cfg_LINE_NUMBERS        
        pCi->linenum++;
#else
        uint8_t tok = lookahead();
        if(tok == NUM) {
            match(NUM);
            if(pCi->value > 0 && pCi->value < 65536) {
                if(pCi->value > pCi->linenum) {
                    pCi->linenum = pCi->value;
                    pCi->sym_idx = sym_add(pCi->a_buff, pCi->pc, LABEL);
                } else {
                    error("line number out of order", NULL);
                }
            } else {
                error("line number out of range", NULL);
            }
        }
#endif
        trace_print();
        return true;
    }
    return false;
}

static uint8_t next_token(void) {
    if(pCi->p_pos == NULL || *pCi->p_pos == '\0') {
        return 0; // End of line
    }
    pCi->p_next = nc_scanner(pCi->p_pos, pCi->a_buff);
    if(pCi->a_buff[0] == '\0') {
       return 0; // End of line
    }
    // Check for semicolon
    if(pCi->a_buff[0] == ';') {
        return SEMICOLON;
    }
    if(pCi->a_buff[0] == '\"') {
        return STR;
    }
    if(isdigit((int8_t)pCi->a_buff[0])) {
        // Hexadecimal or decimal number
        if (pCi->a_buff[0] == '0' && (pCi->a_buff[1] == 'x' || pCi->a_buff[1] == 'X')) {
            pCi->value = (uint32_t)strtol(pCi->a_buff, NULL, 16);
        } else {
            pCi->value = atoi(pCi->a_buff);
        }
        return NUM;
    }
    if(isalpha((int8_t)pCi->a_buff[0]) || pCi->a_buff[0] == '_') {
        uint16_t len = strlen(pCi->a_buff);
        uint8_t type = pCi->a_buff[len - 1] == '$' ? STRID : ID;

        pCi->sym_idx = sym_add(pCi->a_buff, CurrVarIdx, type);
        return a_Symbol[pCi->sym_idx].type;
    }
    if(pCi->a_buff[0] == '{') {
        return LBRACE;
    }
    if(pCi->a_buff[0] == '}') {
        return RBRACE;
    }
    if(pCi->a_buff[0] == '[') {
        return LBRACKET;
    }
    if(pCi->a_buff[0] == ']') {
        return RBRACKET;
    }
    if(pCi->a_buff[0] == '=') {
        if(pCi->a_buff[1] == '=') {
            return EQ;  // == (C-style equals)
        }
        return EQ;
    }
    if(pCi->a_buff[0] == '!') {
        if(pCi->a_buff[1] == '=') {
            return NQ;  // != (C-style not equal)
        }
        return NOT;  // ! as NOT
    }
    if(pCi->a_buff[0] == '&') {
        if(pCi->a_buff[1] == '&') {
            return AND;  // && (C-style AND)
        }
        return '&';  // bitwise AND
    }
    if(pCi->a_buff[0] == '|') {
        if(pCi->a_buff[1] == '|') {
            return OR;  // || (C-style OR)
        }
        return '|';  // bitwise OR
    }
    if(pCi->a_buff[0] == '<') {
        // parse '<<', '<=', '<>', and '<'
        if (pCi->a_buff[1] == '<') {
            return SHL;
        }
        if (pCi->a_buff[1] == '=') {
            return LQ;
        }
        if (pCi->a_buff[1] == '>') {
            return NQ;
        }
        return LE;
    }
    if(pCi->a_buff[0] == '>') {
        // parse '>>', '>=' or '>'
        if (pCi->a_buff[1] == '>') {
            return SHR;
        }
        if (pCi->a_buff[1] == '=') {
            return GQ;
        }
        return GR;
    }
    if(pCi->a_buff[0] == '+') {
        if(pCi->a_buff[1] == '+') {
            return INC;  // ++ (C-style increment)
        }
        return '+';
    }
    if(pCi->a_buff[0] == '-') {
        if(pCi->a_buff[1] == '-') {
            return DEC;  // -- (C-style decrement)
        }
        return '-';
    }
    if(strlen(pCi->a_buff) == 1) {
        return pCi->a_buff[0]; // Single character
    }
    error("unknown character", pCi->a_buff);
    return 0;
}

static uint8_t lookahead(void) {
    if(pCi->p_pos == pCi->p_next) {
        pCi->next_tok = next_token();
    }
    //nc_print("lookahead: %s\n", pCi->a_buff);
    return pCi->next_tok;
}

#ifndef cfg_LINE_NUMBERS
static uint8_t lookfurther(void) {
    return pCi->p_next[0];
}
#endif

static uint8_t next(void) {
    if(pCi->p_pos == pCi->p_next) {
       pCi->next_tok = next_token();
    }
    pCi->p_pos = pCi->p_next;
    return pCi->next_tok;
}

static void match(uint8_t expected) {
    uint8_t tok = next();
    if (tok == expected) {
    } else {
        error("syntax error", pCi->a_buff);
    }
}

#ifndef cfg_LINE_NUMBERS
static void label(void) {
  uint8_t tok = lookahead();
  if(tok == ID) { // Token recognized as variable?
    // Convert to label
    a_Symbol[pCi->sym_idx].type = LABEL;
    pCi->next_tok = LABEL;
    CurrVarIdx--;
  } else if(tok == LABEL) {
    // Already a label
  } else {
    error("label expected", pCi->a_buff);
  }
  match(LABEL);
}
#endif

static void compile_line(void) {
#ifndef cfg_LINE_NUMBERS    
    uint8_t tok = lookahead();
    if(tok == ID || tok == LABEL) {
        uint16_t idx = pCi->sym_idx;
        if(lookfurther() == ':') {
            label();
            match(':');
            a_Symbol[idx].value = pCi->pc;
        }
    }
#endif
    compile_stmts();
}

static void compile_stmts(void) {
    uint8_t tok = lookahead();
    while(tok && !BLOCKEND(tok)) {
        if(tok == SEMICOLON) {
            next();  // Skip semicolon
            tok = lookahead();
            continue;
        }
        compile_stmt();
        tok = lookahead();
        // Skip optional semicolon after statement
        if(tok == SEMICOLON) {
            next();
            tok = lookahead();
        }
        if(pCi->pc >= cfg_MAX_CODE_SIZE - MAX_CODE_PER_LINE) {
            error("code size exceeded", NULL);
            break;
        }
    }
}

static void compile_stmt(void) {
    uint8_t tok = next();
    switch(tok) {
    case FOR: compile_for(); break;
    case IF: compile_if(); break;
    case NC_INT32: compile_int32_decl(); break;
    case STR8: compile_str8_var(); break;
    case FUNC: compile_func(); break;
    case RET: compile_return(); break;
    case ID: compile_var(tok); break;
    case LABEL: compile_var(ID); break;  // Function call: func(args)
    case STRID: compile_var(tok); break;
    case ARR: compile_arr_access(); break;
    case PRINTF: compile_printf(); break;
    case NC_CONST: compile_const(); break;
    case WHILE: compile_while(); break;
    case END: compile_end(); break;
    case XFUNC: {
        // Statement-level xfunc call: a value-returning function leaves its
        // result on the stack, so discard it.
        uint16_t xidx = sym_get(pCi->a_buff);
        uint8_t rt = (xidx < NumXFuncs) ? a_XFuncs[xidx].return_type : NB_NONE;
        compile_xfunc(rt == NB_NONE ? e_NONE : e_ANY);
        if(rt != NB_NONE) {
            pCi->p_code[pCi->pc++] = k_POP_N1;
        }
        break;
    }
    case BREAK: compile_break(); break;
    case BREAKPOINT: compile_breakpoint(); break;
    case RETI: compile_reti(); break;
#ifdef cfg_DATA_ACCESS    
    case COPY: compile_copy(); break;
    case U8: compile_u8_stmt(); break;
    case U16: compile_u16_stmt(); break;
    case U32: compile_u32_stmt(); break;
#ifdef cfg_STRING_SUPPORT
    case STR2ARR: compile_str2arr(); break;
#endif
#endif
    case FREE: compile_free(); break;
    case DISPATCH: compile_dispatch(); break;
    case ':': break;
    default: error("syntax error", pCi->a_buff); break;
    }
}

/* FOR ID '=' <Expression1> TO <Expression2> [STEP <Expression3>]
**   <Statement>...
** NEXT [ID]
**
** <Expression2>  and <Expression3> are pushed on the data stack
*/
static void compile_for(void) {
    uint16_t pc;
    uint8_t tok;
    uint16_t idx;
    bool is_local;

    pCi->p_code[pCi->pc++] = k_FOR_N1;
    // FOR ID
    uint8_t num_vars = CurrVarIdx;
    match(ID);
    idx = pCi->sym_idx;
    // A loop variable first seen inside a function is local; as a global it would share
    // its index with the next global variable after the function's locals are released
    if(pCi->in_func && !a_Symbol[idx].is_local && CurrVarIdx != num_vars) {
        a_Symbol[idx].is_local = 1;
        a_Symbol[idx].value = pCi->local_var_count++;
    }
    is_local = a_Symbol[idx].is_local;
    match(EQ);
    compile_expression(e_NUM);
    pCi->p_code[pCi->pc++] = is_local ? k_POP_LOCAL_N2 : k_POP_VAR_N2;
    pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    match(TO);
    compile_expression(e_NUM);
    tok = lookahead();
    if(tok == STEP) {
        match(STEP);
        compile_expression(e_NUM);
    } else {
        pCi->p_code[pCi->pc++] = k_PUSH_NUM_N2;
        pCi->p_code[pCi->pc++] = 1;
    }

    // Skip the body when the range is empty (start > end for positive step,
    // start < end for negative step).
    uint16_t check_pos = pCi->pc;
    pCi->p_code[pCi->pc++] = is_local ? k_FOR_CHECK_LOCAL_N4 : k_FOR_CHECK_N4;
    pCi->p_code[pCi->pc++] = 0;
    pCi->p_code[pCi->pc++] = 0;
    pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    pc = pCi->pc;
    
    // C-style: for i = 1 to 10 { ... }
    uint8_t outer_loop = pCi->loop_type;
    uint8_t first_break = pCi->num_breaks;
    pCi->loop_type = FOR;
    pCi->for_depth++;
    match(LBRACE);
    compile_stmts();
    while(!BLOCKEND(lookahead())) {
        if(!get_line()) break;
        compile_line();
    }
    match(RBRACE);
    pCi->loop_type = outer_loop;
    pCi->for_depth--;

    pCi->p_code[pCi->pc++] = is_local ? k_NEXT_LOCAL_N4 : k_NEXT_N4;
    pCi->p_code[pCi->pc++] = pc & 0xFF;
    pCi->p_code[pCi->pc++] = (pc >> 8) & 0xFF;
    pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    ACS16(pCi->p_code[check_pos + 1]) = pCi->pc;
    patch_breaks(first_break);
}

/*
** while (expr) { stmts }
*/
static void compile_while(void) {
    uint16_t pos1, pos2;

    pos1 = pCi->pc; // start of loop
    compile_expression(e_NUM);
    pCi->p_code[pCi->pc++] = k_IF_N3;
    pos2 = pCi->pc; // end of loop
    pCi->pc += 2;
    uint8_t outer_loop = pCi->loop_type;
    uint8_t first_break = pCi->num_breaks;
    pCi->loop_type = WHILE;
    match(LBRACE);
    compile_stmts();
    while(!BLOCKEND(lookahead())) {
        if(!get_line()) break;
        compile_line();
    }
    match(RBRACE);
    pCi->loop_type = outer_loop;
    pCi->p_code[pCi->pc++] = k_GOTO_N3;
    pCi->p_code[pCi->pc++] = pos1 & 0xFF;
    pCi->p_code[pCi->pc++] = (pos1 >> 8) & 0xFF;
    ACS16(pCi->p_code[pos2]) = pCi->pc;
    patch_breaks(first_break);
}

/*
** Let the 'break' jumps of the loop just compiled point to the current position
*/
static void patch_breaks(uint8_t first_break) {
    for(uint8_t i = first_break; i < pCi->num_breaks; i++) {
        ACS16(pCi->p_code[pCi->a_break_pos[i]]) = pCi->pc;
    }
    pCi->num_breaks = first_break;
}

/*
** if (expr) { stmts } [else { stmts }]
*/
static void compile_if(void) {
    uint8_t tok;
    uint16_t pos;

    compile_expression(e_NUM);
    pCi->p_code[pCi->pc++] = k_IF_N3;
    pos = pCi->pc;
    pCi->pc += 2;
    match(LBRACE);
    compile_stmts();
    while(!BLOCKEND(lookahead())) {
        if(!get_line()) break;
        compile_line();
    }
    match(RBRACE);
    ACS16(pCi->p_code[pos]) = pCi->pc;
    tok = lookahead();
    if(tok == ELSE) {
        match(ELSE);
        pCi->p_code[pCi->pc++] = k_GOTO_N3;
        ACS16(pCi->p_code[pos]) = pCi->pc + 2;
        pos = pCi->pc;
        pCi->pc += 2;
        if(lookahead() == IF) {  // "else if": the nested if/else forms the else branch
            match(IF);
            compile_if();
        } else {
            match(LBRACE);
            compile_stmts();
            while(!BLOCKEND(lookahead())) {
                if(!get_line()) break;
                compile_line();
            }
            match(RBRACE);
        }
        ACS16(pCi->p_code[pos]) = pCi->pc;
    }
}

static void compile_func_call(uint16_t idx, bool want_value) {
    uint16_t addr = a_Symbol[idx].value;
    bool defined = (a_Symbol[idx].type == LABEL);
    uint8_t param_count = 0;
    uint8_t next_tok;

    match('(');
    next_tok = lookahead();
    while(next_tok != ')') {
        compile_expression(e_ANY);  // Accept any type (e_NUM, e_STR, e_REF)
        pCi->p_code[pCi->pc++] = k_PUSH_PARAM_N1;
        param_count++;
        next_tok = lookahead();
        if(next_tok == ',') {
            match(',');
            next_tok = lookahead();
        }
    }
    match(')');

    // Check parameter count
    if(a_Symbol[idx].param_count != 0xFF && param_count != a_Symbol[idx].param_count) {
        error("wrong number of arguments", a_Symbol[idx].name);
    }

    // Only genuinely forward calls need the (limited) patch list.
    // A not-yet-defined function has type ID and a variable index in value.
    if(!defined) {
        forward_declaration(idx, pCi->pc + 1);
        addr = 0;
    }
    pCi->p_code[pCi->pc++] = k_GOSUB_N3;
    pCi->p_code[pCi->pc++] = addr & 0xFF;
    pCi->p_code[pCi->pc++] = (addr >> 8) & 0xFF;
    if(want_value) {
        pCi->p_code[pCi->pc++] = k_PUSH_RET_N1;
    }
}

static void compile_var(uint8_t tok) {
    uint16_t idx = pCi->sym_idx;
    type_t type;
    uint8_t next_tok = lookahead();
    bool is_local = a_Symbol[idx].is_local;

    // Check for function call: identifier followed by '('
    if(next_tok == '(') {
        compile_func_call(idx, false);
        return;
    }

    // If we're in a function and this variable is not yet marked as local,
    // and it's a newly created variable (not pre-existing global), make it local
    if(pCi->in_func && !is_local) {
        // Check if this is a brand new variable (just added to symbol table)
        // A new variable has value == CurrVarIdx - 1 (the index just assigned)
        if(a_Symbol[idx].value == CurrVarIdx - 1) {
            // This is a new variable declared inside the function - make it local
            a_Symbol[idx].is_local = 1;
            a_Symbol[idx].value = pCi->local_var_count++;
            is_local = true;
        }
    }

    // Check for ++ or -- (post-increment/decrement)
    if(next_tok == INC) {
        match(INC);
        if(is_local) {
            pCi->p_code[pCi->pc++] = k_INC_LOCAL_N2;
        } else {
            pCi->p_code[pCi->pc++] = k_INC_VAR_N2;
        }
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
        return;
    }
    if(next_tok == DEC) {
        match(DEC);
        if(is_local) {
            pCi->p_code[pCi->pc++] = k_DEC_LOCAL_N2;
        } else {
            pCi->p_code[pCi->pc++] = k_DEC_VAR_N2;
        }
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
        return;
    }

    if(tok == STRID) { // str8 a = "string"
        if(is_local) {
            error("string parameters are read-only", a_Symbol[idx].name);
            return;
        }
        match(EQ);
        compile_expression(e_STR);
        pCi->p_code[pCi->pc++] = k_POP_STR_N2;  // Strings always global for now
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    } else if(tok == ID) { // int32 a = expression
        match(EQ);
        type = compile_expression(e_NUM);
        if(type == e_NUM) {
            if(is_local) {
                pCi->p_code[pCi->pc++] = k_POP_LOCAL_N2;
            } else {
                pCi->p_code[pCi->pc++] = k_POP_VAR_N2;
            }
            pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
        } else if(type == e_STR) {
            pCi->p_code[pCi->pc++] = k_POP_STR_N2;
            pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
        } else {
            error("type mismatch", pCi->a_buff);
        }
    } else {
        error("unknown variable type", pCi->a_buff);
    }
}

/*
** str8 name = "string"
*/
static void compile_str8_var(void) {
    uint8_t tok = next();
    if(tok != STRID && tok != ID) {
        error("string identifier expected", pCi->a_buff);
        return;
    }
    uint16_t idx = pCi->sym_idx;
    // Mark this variable as string type (the '$' suffix is optional)
    a_Symbol[idx].type = STRID;
    match(EQ);
    compile_expression(e_STR);
    pCi->p_code[pCi->pc++] = k_POP_STR_N2;
    pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
}

/*
** int32 x = 0      - variable declaration
** int32 arr[10]    - array declaration with size
*/
static void compile_int32_decl(void) {
    uint8_t tok = next();
    if(tok != ID) {
        error("identifier expected", pCi->a_buff);
        return;
    }
    uint16_t idx = pCi->sym_idx;
    
    // Check if this is an array declaration
    uint8_t next_tok = lookahead();
    if(next_tok == LBRACKET) {
        // Function-local arrays are not supported
        if(pCi->in_func) {
            error("function-local arrays are not supported", NULL);
            return;
        }
        // Array declaration: int32 arr[size]
        a_Symbol[idx].type = ARR;
        match(LBRACKET);
        compile_expression(e_NUM);
        match(RBRACKET);
        pCi->p_code[pCi->pc++] = k_DIM_ARR_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    } else {
        // Variable declaration: int32 x = value
        compile_var(tok);
    }
}

/*
** arr[idx] = value  - array element assignment
** arr[idx]++        - increment array element (expansion: arr[idx] = arr[idx] + 1)
** arr[idx]--        - decrement array element (expansion: arr[idx] = arr[idx] - 1)
*/
static void compile_arr_access(void) {
    uint16_t idx = pCi->sym_idx;
    bool arr_is_local = a_Symbol[idx].is_local;
    
    // For local arrays, push address first (before index)
    if(arr_is_local) {
        pCi->p_code[pCi->pc++] = k_PUSH_LOCAL_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
    
    match(LBRACKET);
    compile_expression(e_NUM);
    match(RBRACKET);
    
    uint8_t tok = lookahead();
    if(tok == INC) {
        match(INC);
        error("a[i]++ not yet supported, use a[i] = a[i] + 1", NULL);
        return;
    }
    if(tok == DEC) {
        match(DEC);
        error("a[i]-- not yet supported, use a[i] = a[i] - 1", NULL);
        return;
    }
    
    match(EQ);
    compile_expression(e_NUM);
    
    if(arr_is_local) {
        // Stack: [addr, idx, val] -> k_SET_ARR_ELEM_S_N1
        pCi->p_code[pCi->pc++] = k_SET_ARR_ELEM_S_N1;
    } else {
        // Stack: [idx, val] -> k_SET_ARR_ELEM_N2 var
        pCi->p_code[pCi->pc++] = k_SET_ARR_ELEM_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
}

/*
** func [return_type] funcname(int32 param1, int32 param2, ...) { stmts }
** C-style function definition with LOCAL variables
** All parameters and variables declared inside are LOCAL (on stack)
** return_type is optional: omit for void, or use int32/str8 for return value
*/
static void compile_func(void) {
    uint8_t tok = next();
    uint8_t return_type = 0;  // 0 = void
    
    // Generate GOTO to skip over function body (will be patched later)
    uint16_t skip_pos = pCi->pc;
    pCi->p_code[pCi->pc++] = k_GOTO_N3;
    pCi->p_code[pCi->pc++] = 0;  // Will be patched
    pCi->p_code[pCi->pc++] = 0;
#ifdef cfg_DEBUG_SUPPORT
    // The skip GOTO is no statement; the header line is mapped to the body start below
    if(NumDbgLines > 0 && a_DbgLines[NumDbgLines - 1].pc == skip_pos) {
        NumDbgLines--;
    }
#endif
    
    // Check if first token is a return type (int32, str8) or function name (ID)
    if(tok == NC_INT32 || tok == STR8) {
        return_type = tok;
        tok = next();  // Now get the function name
    }
    
    if(tok != ID) {
        error("function name expected", pCi->a_buff);
        return;
    }
    
    // Register function name as label - points to AFTER the skip GOTO
    uint16_t func_idx = pCi->sym_idx;
    a_Symbol[func_idx].type = LABEL;
    a_Symbol[func_idx].value = pCi->pc;
    a_Symbol[func_idx].return_type = return_type;
    CurrVarIdx--;  // Don't count as variable
    
    // Enter function context
    pCi->in_func = true;
    pCi->local_var_count = 0;
    pCi->local_var_start_idx = CurrVarIdx;
    pCi->func_return_type = return_type;
    
    match('(');
    
    // Parse parameters - they become local variables
    uint8_t param_indices[8];
    //uint8_t param_types[8];  // Store parameter types (NC_INT32, STR8, ARR)
    uint8_t param_count = 0;
    
    tok = lookahead();
    while(tok != ')' && param_count < 8) {
        // Expect type (int32, str8, or int32[] for array ref)
        if(tok != NC_INT32 && tok != STR8) {
            error("type expected", pCi->a_buff);
            return;
        }
        uint8_t param_type = tok;
        uint8_t vars_before = CurrVarIdx;  // the parameter name is tokenized by the next lookahead
        match(tok);
        
        // Check for [] after int32 (array reference parameter)
        tok = lookahead();
        if(param_type == NC_INT32 && tok == LBRACKET) {
            match(LBRACKET);
            match(RBRACKET);
            param_type = ARR;  // Mark as array reference
            tok = lookahead();  // Get the parameter name token
        }
        
        // Expect parameter name (tok is already from lookahead)
        if(tok != ID && tok != STRID) {
            error("parameter name expected", pCi->a_buff);
            return;
        }
        match(tok);  // Consume the parameter name
        if(CurrVarIdx == vars_before && !a_Symbol[pCi->sym_idx].is_local) {
            // Name of an existing global: the parameter gets its own entry, the global stays intact
            pCi->sym_idx = sym_add_local(a_Symbol[pCi->sym_idx].name, a_Symbol[pCi->sym_idx].type);
        }
        // Mark as local variable with stack offset
        a_Symbol[pCi->sym_idx].is_local = 1;
        a_Symbol[pCi->sym_idx].value = pCi->local_var_count;
        // Set type: ARR for array refs, ID for int32, STRID for str8
        if(param_type == ARR) {
            a_Symbol[pCi->sym_idx].type = ARR;
        } else if(param_type == STR8) {
            a_Symbol[pCi->sym_idx].type = STRID;
        }
        param_indices[param_count] = pCi->sym_idx;
        //param_types[param_count] = param_type;
        param_count++;
        pCi->local_var_count++;
        
        // Check for comma or end
        tok = lookahead();
        if(tok == ',') {
            match(',');
            tok = lookahead();
        }
    }
    match(')');
    
    // Remember position for ENTER instruction (we patch it later)
    uint16_t enter_pos = pCi->pc;
    pCi->p_code[pCi->pc++] = k_ENTER_N2;
    pCi->p_code[pCi->pc++] = 0;  // Will be patched with final local_var_count
    
    // Generate code to pop parameters from param stack into local variables
    // Pop in reverse order because param stack is LIFO
    for(int8_t i = param_count - 1; i >= 0; i--) {
        pCi->p_code[pCi->pc++] = k_PARAM_N1;
        pCi->p_code[pCi->pc++] = k_POP_LOCAL_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[param_indices[i]].value;
    }
#ifdef cfg_DEBUG_SUPPORT
    dbg_add_line(pCi->pc, pCi->linenum);
#endif
    
    // Expect opening brace
    match(LBRACE);
    
    // Compile function body ('break' must not leave the function)
    uint8_t outer_loop = pCi->loop_type;
    uint8_t outer_for_depth = pCi->for_depth;
    pCi->loop_type = 0;
    pCi->for_depth = 0;
    compile_stmts();
    while(!BLOCKEND(lookahead())) {
        if(!get_line()) break;
        compile_line();
    }
    match(RBRACE);
    pCi->loop_type = outer_loop;
    pCi->for_depth = outer_for_depth;
    
    // Patch ENTER instruction with final local variable count
    pCi->p_code[enter_pos + 1] = pCi->local_var_count;
    
    // Generate implicit return at end of function
    if(return_type != 0) {
        // Function with return type - push default value (0) for implicit return
        pCi->p_code[pCi->pc++] = k_PUSH_NUM_N2;
        pCi->p_code[pCi->pc++] = 0;  // default return value
    }
    // LEAVE saves return value (if any) to ret_val register
    pCi->p_code[pCi->pc++] = k_LEAVE_N1;
    
    // Patch the skip GOTO to jump here (after function body)
    pCi->p_code[skip_pos + 1] = pCi->pc & 0xFF;
    pCi->p_code[skip_pos + 2] = (pCi->pc >> 8) & 0xFF;
    
    // Clear local variables from symbol table (they're out of scope)
    for(uint16_t i = StartOfVars; i < cfg_MAX_NUM_SYM; i++) {
        if(a_Symbol[i].is_local) {
#ifdef cfg_DEBUG_SUPPORT
            if(NumDbgLocals < cfg_DBG_MAX_LOCALS) {
                nc_dbg_local_t *p_loc = &a_DbgLocals[NumDbgLocals++];
                memcpy(p_loc->name, a_Symbol[i].name, k_MAX_SYM_LEN);
                p_loc->start_pc = a_Symbol[func_idx].value;
                p_loc->end_pc = pCi->pc;
                p_loc->offs = (uint8_t)a_Symbol[i].value;
                p_loc->type = a_Symbol[i].type;
            }
#endif
            a_Symbol[i].name[0] = '\0';
            a_Symbol[i].is_local = 0;
            CurrVarIdx--;
        }
    }

    // Store parameter count in function symbol for call-site checking
    a_Symbol[func_idx].param_count = param_count;
    
    // Exit function context
    pCi->in_func = false;
    pCi->local_var_count = 0;
    pCi->func_return_type = 0;
}

/*
** return [expression]
** Returns from current function, optionally with a value
** Value is saved to ret_val register by k_LEAVE_N1
*/
static void compile_return(void) {
    if(!pCi->in_func) {
        error("return outside function", NULL);
        return;
    }
    
    uint8_t tok = lookahead();
    if(tok != 0 && tok != RBRACE && tok != ':') {
        // There's an expression to return
        if(pCi->func_return_type == 0) {
            error("void function cannot return value", NULL);
            return;
        }
        type_t expected = (pCi->func_return_type == NC_INT32) ? e_NUM : e_STR;
        compile_expression(expected);
        // Value is on stack - LEAVE will save it to ret_val register
    } else {
        // No return value
        if(pCi->func_return_type != 0) {
            error("function must return a value", NULL);
            return;
        }
    }
    // Generate LEAVE (restores frame, saves return value if present)
    compile_for_leave();
    pCi->p_code[pCi->pc++] = k_LEAVE_N1;
}

/*
** Unwind the for-loops still open when leaving a function via return/reti
*/
static void compile_for_leave(void) {
    if(pCi->for_depth > 0) {
        pCi->p_code[pCi->pc++] = k_FOR_LEAVE_N2;
        pCi->p_code[pCi->pc++] = pCi->for_depth;
    }
}

/*
** printf("format %d %s\n", value, string$)
** Format specifiers: %d (decimal), %u (unsigned decimal), %h (hex), %s (string), %% (percent)
** Escape sequences (decoded by the scanner): \a \b \e \f \n \r \t \v \\ \' \"
** and \xHH (hex, Lua/Python style) and \ddd (decimal, Lua style). No C octal.
*/
static void compile_printf(void) {
    uint8_t tok;
    uint8_t num_args = 0;
    static char fmt_buf[k_MAX_LINE_LEN];
    
    match('(');
    tok = lookahead();
    if(tok != STR) {
        error("format string expected", pCi->a_buff);
        return;
    }
    
    // Save format string before match overwrites it
    match(STR);
    uint16_t len = strlen(pCi->a_buff);
    pCi->a_buff[len - 1] = '\0'; // remove closing quote
    strncpy(fmt_buf, pCi->a_buff + 1, sizeof(fmt_buf) - 1); // skip opening quote
    fmt_buf[sizeof(fmt_buf) - 1] = '\0';
    uint16_t fmt_len = strlen(fmt_buf) + 1; // include null terminator
    
    // Count format specifiers in format string (escapes already decoded)
    for(char *p = fmt_buf; *p; p++) {
        if(*p == '%' && *(p+1)) {
            p++; // skip to specifier
            if(*p != '%') {
                num_args++; // only count real format specifiers
            }
        }
    }
    
    // Compile arguments
    tok = lookahead();
    for(uint8_t i = 0; i < num_args; i++) {
        if(tok != ',') {
            error("missing argument for printf", NULL);
            return;
        }
        match(',');
        compile_expression(e_ANY);
        tok = lookahead();
    }
    match(')');
    
    // Emit opcode with format string
    if(pCi->pc + 3 + fmt_len > cfg_MAX_CODE_SIZE) {
        error("code size exceeded", NULL);
        return;
    }
    pCi->p_code[pCi->pc++] = k_PRINTF_Nx;
    pCi->p_code[pCi->pc++] = num_args;
    pCi->p_code[pCi->pc++] = fmt_len;
    memcpy(&pCi->p_code[pCi->pc], fmt_buf, fmt_len);
    pCi->pc += fmt_len;
}

static void compile_end(void) {
    pCi->p_code[pCi->pc++] = k_END;
}

static type_t compile_xfunc(uint8_t type) {
    uint8_t idx = sym_get(pCi->a_buff);
    uint8_t tok;
    if(idx >= NumXFuncs) {
        error("unknown external function", pCi->a_buff);
    }
    if(type != e_ANY && type != a_XFuncs[idx].return_type) {
        error("syntax error", pCi->a_buff);
    }
    match('(');
    for(uint8_t i = 0; i < a_XFuncs[idx].num_params; i++) {
        compile_expression(a_XFuncs[idx].type[i]);
        pCi->p_code[pCi->pc++] = k_PUSH_PARAM_N1;
        tok = lookahead();
        if(tok == ',') {
            match(',');
        } else if(tok == ')') {
            add_default_params(a_XFuncs[idx].num_params - i - 1);
            break;
        } else {
            error("syntax error", pCi->a_buff);
        }
    }
    pCi->p_code[pCi->pc++] = k_XFUNC_N2;
    pCi->p_code[pCi->pc++] = idx;
    match(')');
    return a_XFuncs[idx].return_type;
}

/*
** break: leave the innermost for/while loop
*/
static void compile_break(void) {
    if(pCi->loop_type == 0) {
        error("break outside loop", NULL);
        return;
    }
    if(pCi->num_breaks >= k_MAX_BREAKS) {
        error("too many break statements", NULL);
        return;
    }
    // A for-loop keeps end value and step on the stack, k_BREAK_FOR_N3 removes them
    pCi->p_code[pCi->pc++] = (pCi->loop_type == FOR) ? k_BREAK_FOR_N3 : k_GOTO_N3;
    pCi->a_break_pos[pCi->num_breaks++] = pCi->pc;
    pCi->pc += 2;
}

/*
** breakpoint(): returns NB_BREAK with the line number on the parameter stack
*/
static void compile_breakpoint(void) {
    match('(');
    match(')');
    pCi->p_code[pCi->pc++] = k_BREAK_INSTR_N3;
    ACS16(pCi->p_code[pCi->pc]) = pCi->linenum;
    pCi->pc += 2;
}

static void compile_reti(void) {
    compile_for_leave();
    pCi->p_code[pCi->pc++] = k_RETI_N1;
}

#ifdef cfg_DATA_ACCESS

// copy(arr, offs, arr, offs, bytes)
static void compile_copy(void) {
    match('(');
    compile_expression(e_REF);
    match(',');
    compile_expression(e_NUM);
    match(',');
    compile_expression(e_REF);
    match(',');
    compile_expression(e_NUM);
    match(',');
    compile_expression(e_NUM);
    match(')');
    pCi->p_code[pCi->pc++] = k_COPY_N1;
}

// str2arr(arr, offs, str)
#ifdef cfg_STRING_SUPPORT
static void compile_str2arr(void) {
    match('(');
    compile_expression(e_REF);
    match(',');
    compile_expression(e_NUM);
    match(',');
    compile_expression(e_STR);
    match(')');
    pCi->p_code[pCi->pc++] = k_STR2ARR_N1;
}
#endif

static void compile_get(uint8_t tok, uint8_t instr, uint8_t instr_s) {
    uint8_t idx;
    match(tok);
    match('(');
    match(ARR);
    idx = pCi->sym_idx;
    bool is_local = a_Symbol[idx].is_local;
    if(is_local) {
        pCi->p_code[pCi->pc++] = k_PUSH_LOCAL_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
    match(',');
    compile_expression(e_NUM);
    match(')');
    if(is_local) {
        pCi->p_code[pCi->pc++] = instr_s;
    } else {
        pCi->p_code[pCi->pc++] = instr;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
}

/*
** u8(arr, idx, val)  - SET: 3 params
** u8(arr, idx)       - GET: 2 params (handled in compile_factor)
*/
static void compile_u8_stmt(void) {
    uint8_t idx;
    match('(');
    match(ARR);
    idx = pCi->sym_idx;
    bool is_local = a_Symbol[idx].is_local;
    if(is_local) {
        pCi->p_code[pCi->pc++] = k_PUSH_LOCAL_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
    match(',');
    compile_expression(e_NUM);
    match(',');
    compile_expression(e_NUM);
    match(')');
    if(is_local) {
        pCi->p_code[pCi->pc++] = k_SET_ARR_1BYTE_S_N1;
    } else {
        pCi->p_code[pCi->pc++] = k_SET_ARR_1BYTE_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
}

static void compile_u16_stmt(void) {
    uint8_t idx;
    match('(');
    match(ARR);
    idx = pCi->sym_idx;
    bool is_local = a_Symbol[idx].is_local;
    if(is_local) {
        pCi->p_code[pCi->pc++] = k_PUSH_LOCAL_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
    match(',');
    compile_expression(e_NUM);
    match(',');
    compile_expression(e_NUM);
    match(')');
    if(is_local) {
        pCi->p_code[pCi->pc++] = k_SET_ARR_2BYTE_S_N1;
    } else {
        pCi->p_code[pCi->pc++] = k_SET_ARR_2BYTE_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
}

static void compile_u32_stmt(void) {
    uint8_t idx;
    match('(');
    match(ARR);
    idx = pCi->sym_idx;
    bool is_local = a_Symbol[idx].is_local;
    if(is_local) {
        pCi->p_code[pCi->pc++] = k_PUSH_LOCAL_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
    match(',');
    compile_expression(e_NUM);
    match(',');
    compile_expression(e_NUM);
    match(')');
    if(is_local) {
        pCi->p_code[pCi->pc++] = k_SET_ARR_4BYTE_S_N1;
    } else {
        pCi->p_code[pCi->pc++] = k_SET_ARR_4BYTE_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[idx].value;
    }
}
#endif

static void compile_const(void) {
    uint32_t factor = 1;
    next();
    uint16_t idx = pCi->sym_idx;
    match(EQ);
    uint8_t tok = lookahead();
    if(tok == '-') {
        match('-');
        factor = -1;
    }
    match(NUM);
    a_Symbol[idx].type = e_CNST;
    a_Symbol[idx].value = pCi->value * factor;
}

static void compile_free(void) {
    match('(');
    match(')');
    pCi->p_code[pCi->pc++] = k_FREE_N1;
}

/*
** dispatch(expr) {
**     func_a
**     func_b
**     func_c
** }
** Calls func_a() if expr==0, func_b() if expr==1, etc.
** If expr is out of range, the dispatch is skipped.
**
** dispatch(expr) {
**     0: stmt
**     1: { stmts }
** }
** Inline dispatch: executes code block based on index.
** Case values: numbers or constants 0..31, any order, gaps allowed.
** If expr is out of range or a gap, the dispatch is skipped.
*/
static void compile_dispatch(void) {
    match('(');
    compile_expression(e_NUM);
    match(')');
    match(LBRACE);

    // Skip empty lines to find first entry
    uint8_t tok = lookahead();
    while(tok == 0) {
        if(!get_line()) break;
        tok = lookahead();
    }

    if(tok == NUM || tok == e_CNST) {
        // Inline dispatch: the jump table is indexed by the case value
        uint16_t case_addrs[32] = {0};  // 0 = no case (address 0 is never code)
        uint16_t goto_patches[32];

        pCi->p_code[pCi->pc++] = k_DISPATCH_JMP_Nx;
        uint16_t count_pos = pCi->pc;
        pCi->p_code[pCi->pc++] = 0;  // count placeholder
        uint16_t table_ref_pos = pCi->pc;
        pCi->pc += 2;  // table address placeholder

        uint8_t num_cases = 0;
        uint8_t count = 0;  // highest case value + 1

        while(tok != RBRACE) {
            if(tok == 0) {
                if(!get_line()) break;
                tok = lookahead();
                continue;
            }
            tok = next();
            uint32_t value;
            if(tok == NUM) {
                value = pCi->value;
            } else if(tok == e_CNST) {
                value = a_Symbol[pCi->sym_idx].value;
            } else {
                error("case value expected", pCi->a_buff);
                return;
            }
            if(value >= 32) {
                error("case value out of range (0..31)", pCi->a_buff);
                return;
            }
            if(case_addrs[value] != 0) {
                error("duplicate case value", pCi->a_buff);
                return;
            }
            match(':');

            case_addrs[value] = pCi->pc;
            if(value >= count) {
                count = (uint8_t)(value + 1);
            }

            tok = lookahead();
            if(tok == LBRACE) {
                match(LBRACE);
                compile_stmts();
                while(!BLOCKEND(lookahead())) {
                    if(!get_line()) break;
                    compile_line();
                }
                match(RBRACE);
            } else if(tok != 0) {
                compile_stmts();
            }

            // Emit GOTO end (to be patched)
            pCi->p_code[pCi->pc++] = k_GOTO_N3;
            goto_patches[num_cases++] = pCi->pc;
            pCi->pc += 2;

            tok = lookahead();
        }
        match(RBRACE);

        // Emit jump table after all case bodies; gaps jump to the end like out-of-range values
        uint16_t table_addr = pCi->pc;
        uint16_t end_addr = table_addr + count * 2;
#ifdef cfg_DEBUG_SUPPORT
        dbg_drop_lines_from(table_addr);
#endif
        for(uint8_t i = 0; i < count; i++) {
            uint16_t addr = case_addrs[i] != 0 ? case_addrs[i] : end_addr;
            pCi->p_code[pCi->pc++] = addr & 0xFF;
            pCi->p_code[pCi->pc++] = (addr >> 8) & 0xFF;
        }

        // Patch header
        pCi->p_code[count_pos] = count;
        ACS16(pCi->p_code[table_ref_pos]) = table_addr;

        // Patch GOTOs to end
        for(uint8_t i = 0; i < num_cases; i++) {
            ACS16(pCi->p_code[goto_patches[i]]) = end_addr;
        }
    } else {
        // Function dispatch: func_a func_b ...
        pCi->p_code[pCi->pc++] = k_DISPATCH_Nx;
        uint16_t count_pos = pCi->pc;
        pCi->p_code[pCi->pc++] = 0; // count placeholder

        uint8_t count = 0;

        // Process dispatch entries (comma-separated, may span multiple lines)
        while(tok != RBRACE) {
            if(tok == 0) {
                // End of line - read next line
                if(!get_line()) break;
                tok = lookahead();
                continue;
            }
            tok = next();
            if(tok != LABEL && tok != ID) {
                error("function name expected", pCi->a_buff);
                return;
            }
            uint16_t idx = pCi->sym_idx;
            uint16_t addr = a_Symbol[idx].value;
            // Register for forward declaration patching
            forward_declaration(idx, pCi->pc);
            pCi->p_code[pCi->pc++] = addr & 0xFF;
            pCi->p_code[pCi->pc++] = (addr >> 8) & 0xFF;
            count++;
            tok = lookahead();
            // Skip empty lines after entry
            while(tok == 0) {
                if(!get_line()) break;
                tok = lookahead();
            }
            // Expect comma between entries
            if(tok == ',') {
                next();  // skip comma
                tok = lookahead();
            } else if(tok != RBRACE) {
                error("',' expected between dispatch entries", pCi->a_buff);
                return;
            }
        }
        match(RBRACE);

        // Patch count
        pCi->p_code[count_pos] = count;
#ifdef cfg_DEBUG_SUPPORT
        dbg_drop_lines_from(count_pos);
#endif
    }
}

/**************************************************************************************************
 * Symbol table and other helper functions
 *************************************************************************************************/

/*
** Add symbol to symbol table
** id = symbol name
** val = value (in case of variable the index to vm->variables)
** type = type of symbol (ID, STRID, ARR, LABEL)
*/
static uint16_t sym_add(char *id, uint32_t val, uint8_t type) {
    uint16_t start = 0;
    uint16_t first_free = 0xFFFF;
    char sym[k_MAX_SYM_LEN];

    // Copy symbol name (case-sensitive)
    for(uint16_t i = 0; i < k_MAX_SYM_LEN; i++) {
        sym[i] = id[i];
        if(sym[i] == '\0') {
            break;
        }
    }
    sym[k_MAX_SYM_LEN - 1] = '\0';

    // Search for existing symbol (search ALL entries, don't stop at holes).
    // Inside a function a local symbol takes precedence over a global one with the same name.
    uint16_t found = 0xFFFF;
    for(uint16_t i = 0; i < cfg_MAX_NUM_SYM; i++) {
        if(a_Symbol[i].name[0] == '\0') {
            if(first_free == 0xFFFF) {
                first_free = i;
            }
            continue;  // Skip holes, keep searching
        }
        if(strcmp(a_Symbol[i].name, sym) == 0) {
            if(a_Symbol[i].is_local || pCi == NULL || !pCi->in_func) {
                found = i;
                break;
            }
            if(found == 0xFFFF) {
                found = i;
            }
        }
    }
    if(found != 0xFFFF) {
        if(a_Symbol[found].value == 0 && a_Symbol[found].type == LABEL && val > 0) {
            a_Symbol[found].value = val;
        }
        return found;
    }

    // Not found - add new symbol at first free slot
    start = (first_free != 0xFFFF) ? first_free : 0;
    for(uint16_t i = start; i < cfg_MAX_NUM_SYM; i++) {
        if(a_Symbol[i].name[0] == '\0') {
            strncpy(a_Symbol[i].name, sym, k_MAX_SYM_LEN - 1);
            a_Symbol[i].name[k_MAX_SYM_LEN - 1] = '\0';
            a_Symbol[i].value = val;
            a_Symbol[i].type = type;
            a_Symbol[i].is_local = 0;     // Default: global
            a_Symbol[i].param_count = 0xFF; // Default: unknown
            if(type != LABEL) {
                CurrVarIdx++;
            }
            return i;
        }
    }
    error("symbol table full", NULL);
    return 0;
}

/*
** Add a local symbol even if a global symbol with the same name exists (parameter shadowing)
*/
static uint16_t sym_add_local(char *id, uint8_t type) {
    for(uint16_t i = StartOfVars; i < cfg_MAX_NUM_SYM; i++) {
        if(a_Symbol[i].name[0] == '\0') {
            memmove(a_Symbol[i].name, id, k_MAX_SYM_LEN);
            a_Symbol[i].name[k_MAX_SYM_LEN - 1] = '\0';
            a_Symbol[i].value = 0;
            a_Symbol[i].type = type;
            a_Symbol[i].is_local = 1;
            a_Symbol[i].param_count = 0xFF;
            CurrVarIdx++;
            return i;
        }
    }
    error("symbol table full", NULL);
    return 0;
}

static uint16_t sym_get(char *id) {
    char sym[k_MAX_SYM_LEN];

    // Copy symbol name (case-sensitive)
    for(uint16_t i = 0; i < k_MAX_SYM_LEN; i++) {
        sym[i] = id[i];
        if(sym[i] == '\0') {
            break;
        }
    }
    sym[k_MAX_SYM_LEN - 1] = '\0';

    // Search for existing symbol
    for(uint16_t i = 0; i < cfg_MAX_NUM_SYM; i++) {
        if(strcmp(a_Symbol[i].name, sym) == 0) {
            return a_Symbol[i].value;
        }
        if(a_Symbol[i].name[0] == '\0') {
            break;
        }
    }
    error("unknown symbol", id);
    return 0;
}

static void trace_print(void) {
#ifdef cfg_TRACE_SUPPORT
    pCi->p_trace[pCi->pc] = pCi->linenum;
#endif
#ifdef cfg_DEBUG_SUPPORT
    dbg_add_line(pCi->pc, pCi->linenum);
#endif
}

#ifdef cfg_DEBUG_SUPPORT
// Lines without code share the pc with the following line; the last one wins.
static void dbg_add_line(uint16_t pc, uint16_t line) {
    if(NumDbgLines > 0 && a_DbgLines[NumDbgLines - 1].pc == pc) {
        a_DbgLines[NumDbgLines - 1].line = line;
    } else if(NumDbgLines < cfg_DBG_MAX_LINES) {
        a_DbgLines[NumDbgLines].pc = pc;
        a_DbgLines[NumDbgLines].line = line;
        NumDbgLines++;
    }
}

// Lines read while emitting inline data (jump tables) must not become breakpoint positions
static void dbg_drop_lines_from(uint16_t pc) {
    while(NumDbgLines > 0 && a_DbgLines[NumDbgLines - 1].pc >= pc) {
        NumDbgLines--;
    }
}
#endif

static void error(char *err, char *id) {
    nc_print("Error in line %u: ", pCi->linenum);
    if(id != NULL && id[0] != '\0') {
        nc_print("%s '%s'\n", err, id);
    } else {
        nc_print("%s\n", err);
    }
    pCi->err_count++;
    pCi->p_pos = pCi->p_next;
    if(pCi->p_pos != NULL) {
        pCi->p_pos[0] = '\0';
    }
    longjmp(pCi->jmp_buf, 0);
}

static uint8_t get_num_vars(void) {
    uint8_t idx = 0;

    for(uint16_t i = StartOfVars; i < cfg_MAX_NUM_SYM; i++) {
        if(a_Symbol[i].name[0] != '\0' && a_Symbol[i].type != LABEL)
        {
            idx++;
        }
    }
    return idx;
}

static void add_default_params(uint8_t num) {
    for(uint8_t i = 0; i < num; i++) {
        pCi->p_code[pCi->pc++] = k_PUSH_NUM_N2;
        pCi->p_code[pCi->pc++] = 0;
        pCi->p_code[pCi->pc++] = k_PUSH_PARAM_N1;
    }
}

// idx = index of symbol (SmyIdx)
// pos = position in code array
static void forward_declaration(uint16_t idx, uint16_t pos) {
    if(pCi->num_fw_decls < cfg_MAX_FW_DECL) {
        pCi->a_forward_decl[pCi->num_fw_decls].idx = idx;
        pCi->a_forward_decl[pCi->num_fw_decls].pos = pos;
        pCi->num_fw_decls++;
    } else {
        error("too many forward declarations", NULL);
    }
}

static void resolve_forward_declarations(void) {
    uint16_t idx, pos, addr;
    for(uint8_t i = 0; i < pCi->num_fw_decls; i++) {
        idx = pCi->a_forward_decl[i].idx;
        pos = pCi->a_forward_decl[i].pos;
        if(a_Symbol[idx].type == LABEL) {
            addr = a_Symbol[idx].value;
            if(addr > 0) {
                pCi->p_code[pos + 0] = addr & 0xFF;
                pCi->p_code[pos + 1] = (addr >> 8) & 0xFF;
            } else {
#ifdef cfg_LINE_NUMBERS
                error("Line number not found", a_Symbol[idx].name);
#else
                error("Label not found", a_Symbol[idx].name);
#endif
            }
        } else {

            error("forward declaration not resolved", a_Symbol[idx].name);
        }
    }
    pCi->num_fw_decls = 0;
}

static void append_data_to_code(t_VM *vm) {
    pCi->p_code[pCi->pc++] = 0xFF;  // End tag before the data section starts
    vm->data_start_addr = pCi->pc;
    vm->data_read_offs = 0;
    
    if((pCi->pc + pCi->data_idx * 4) >= cfg_MAX_CODE_SIZE) {
        error("code size exceeded", NULL);
    }
    for(int i = 0; i < pCi->data_idx; i++) {
        ACS32(pCi->p_code[pCi->pc]) = pCi->a_data[i];
        pCi->pc += 4;
    }
}

/**************************************************************************************************
 * Expression compiler
 *************************************************************************************************/
static type_t compile_expression(type_t type) {
    type_t type1 = compile_and_expr();
    uint8_t op = lookahead();
    while(op == OR) {
        match(op);
        type_t type2 = compile_and_expr();
        if(type1 != e_NUM || type2 != e_NUM) {
            error("type mismatch", NULL);
        }
        pCi->p_code[pCi->pc++] = k_OR_N1;
        op = lookahead();
    }
    if(type != e_ANY && type1 != type) {
        error("type mismatch", pCi->a_buff);
    }
    return type1;
}

static type_t compile_and_expr(void) {
    type_t type1 = compile_not_expr();
    uint8_t op = lookahead();
    while(op == AND) {
        match(op);
        type_t type2 = compile_not_expr();
        if(type1 != e_NUM || type2 != e_NUM) {
            error("type mismatch", pCi->a_buff);
        }
        pCi->p_code[pCi->pc++] = k_AND_N1;
        op = lookahead();
    }
    return type1;
}

static type_t compile_not_expr(void) {
    type_t type;
    uint8_t op = lookahead();
    if(op == NOT) {
        match(op);
        type = compile_comp_expr();
        if(type != e_NUM) {
            error("type mismatch", pCi->a_buff);
        }
          pCi->p_code[pCi->pc++] = k_NOT_N1;
    } else {
        type = compile_comp_expr();
    }
    return type;
}

static type_t compile_comp_expr(void) {
    type_t type1 = compile_bin_or_expr();
    uint8_t op = lookahead();
    while(op == EQ || op == NQ || op == LE || op == LQ || op == GR || op == GQ) {
        match(op);
        type_t type2 = compile_bin_or_expr();
        if(type1 != type2) {
            error("type mismatch", pCi->a_buff);
        }
#ifdef cfg_STRING_SUPPORT        
        if(type1 == e_STR) {
            switch(op) {
            case EQ: pCi->p_code[pCi->pc++] = k_STR_EQUAL_N1; break;
            case NQ: pCi->p_code[pCi->pc++] = k_STR_NOT_EQU_N1; break;
            case LE: pCi->p_code[pCi->pc++] = k_STR_LESS_N1; break;
            case LQ: pCi->p_code[pCi->pc++] = k_STR_LESS_EQU_N1; break;
            case GR: pCi->p_code[pCi->pc++] = k_STR_GREATER_N1; break;
            case GQ: pCi->p_code[pCi->pc++] = k_STR_GREATER_EQU_N1; break;
            default: error("unknown operator", pCi->a_buff); break;
            }
        } else {
#else
        { 
#endif
            switch(op) {
            case EQ: pCi->p_code[pCi->pc++] = k_EQUAL_N1; break;
            case NQ: pCi->p_code[pCi->pc++] = k_NOT_EQUAL_N1; break;
            case LE: pCi->p_code[pCi->pc++] = k_LESS_N1; break;
            case LQ: pCi->p_code[pCi->pc++] = k_LESS_EQU_N1; break;
            case GR: pCi->p_code[pCi->pc++] = k_GREATER_N1; break;
            case GQ: pCi->p_code[pCi->pc++] = k_GREATER_EQU_N1; break;
            default: error("unknown operator", pCi->a_buff); break;
            }
        }
        type1 = e_NUM;  // a comparison yields 0/1, also for strings
        op = lookahead();
    }
    return type1;
}

static type_t compile_bin_or_expr(void) {
    type_t type1 = compile_bin_xor_expr();
    uint8_t op = lookahead();
    while(op == '|') {
        match(op);
        type_t type2 = compile_bin_xor_expr();
        if(type1 != e_NUM || type2 != e_NUM) {
            error("type mismatch", pCi->a_buff);
        }
        pCi->p_code[pCi->pc++] = k_BOR_N1;
        op = lookahead();
    }
    return type1;
}

static type_t compile_bin_xor_expr(void) {
    type_t type1 = compile_bin_and_expr();
    uint8_t op = lookahead();
    while(op == '^') {
        match(op);
        type_t type2 = compile_bin_and_expr();
        if(type1 != e_NUM || type2 != e_NUM) {
            error("type mismatch", pCi->a_buff);
        }
        pCi->p_code[pCi->pc++] = k_BXOR_N1;
        op = lookahead();
    }
    return type1;
}

static type_t compile_bin_and_expr(void) {
    type_t type1 = compile_shift_expr();
    uint8_t op = lookahead();
    while(op == '&') {
        match(op);
        type_t type2 = compile_shift_expr();
        if(type1 != e_NUM || type2 != e_NUM) {
            error("type mismatch", pCi->a_buff);
        }
        pCi->p_code[pCi->pc++] = k_BAND_N1;
        op = lookahead();
    }
    return type1;
}

static type_t compile_shift_expr(void) {
    type_t type1 = compile_add_expr();
    uint8_t op = lookahead();
    while(op == SHL || op == SHR) {
        match(op);
        type_t type2 = compile_add_expr();
        if(type1 != e_NUM || type2 != e_NUM) {
            error("type mismatch", pCi->a_buff);
        }
        pCi->p_code[pCi->pc++] = (op == SHL) ? k_SHL_N1 : k_SHR_N1;
        op = lookahead();
    }
    return type1;
}


static type_t compile_add_expr(void) {
    type_t type1 = compile_term();
    uint8_t op = lookahead();
    while(op == '+' || op == '-') {
        match(op);
        type_t type2 = compile_term();
        if(type1 != type2) {
            error("type mismatch", pCi->a_buff);
        }
        if(op == '+') {
            if(type1 == e_NUM) {
              pCi->p_code[pCi->pc++] = k_ADD_N1;
            } else {
#ifdef cfg_STRING_SUPPORT                
                pCi->p_code[pCi->pc++] = k_ADD_STR_N1;
#else
                error("type mismatch", pCi->a_buff);
#endif
            }
        } else {
            if(type1 == e_NUM) {
              pCi->p_code[pCi->pc++] = k_SUB_N1;
            } else {
              error("type mismatch", pCi->a_buff);
            }
        }
        op = lookahead();
    }
    return type1;
}

static type_t compile_term(void) {
    type_t type1 = compile_neg_factor();
    uint8_t op = lookahead();
    while(op == '*' || op == '/' || op == MOD || op == '%') {
        match(op);
        type_t type2 = compile_neg_factor();
        if(type1 != e_NUM || type2 != e_NUM) {
            error("type mismatch", pCi->a_buff);
        }
        if(op == '*') {
          pCi->p_code[pCi->pc++] = k_MUL_N1;
        } else if(op == MOD || op == '%') {
          pCi->p_code[pCi->pc++] = k_MOD_N1;
        } else {
          pCi->p_code[pCi->pc++] = k_DIV_N1;
        }
        op = lookahead();
    }
    return type1;
}

static type_t compile_neg_factor(void) {
    type_t type = 0;
    uint8_t tok = lookahead();
    if(tok == '-') {
        match('-');
        type = compile_factor();
        pCi->p_code[pCi->pc++] = k_NEG_N1;
    } else {
        type = compile_factor();
    }
    return type;
}

static type_t compile_factor(void) {
    type_t type = 0;
    uint8_t val;
    uint8_t tok = lookahead();
    switch(tok) {
    case '(':
        match('(');
        type = compile_expression(e_NUM);
        match(')');
        break;
    case e_CNST:
        pCi->value = a_Symbol[pCi->sym_idx].value;
        match(e_CNST);
        if(pCi->value < 256)
        {
          pCi->p_code[pCi->pc++] = k_PUSH_NUM_N2;
          pCi->p_code[pCi->pc++] = pCi->value;
        }
        else
        {
          pCi->p_code[pCi->pc++] = k_PUSH_NUM_N5;
          pCi->p_code[pCi->pc++] = pCi->value & 0xFF;
          pCi->p_code[pCi->pc++] = (pCi->value >> 8) & 0xFF;
          pCi->p_code[pCi->pc++] = (pCi->value >> 16) & 0xFF;
          pCi->p_code[pCi->pc++] = (pCi->value >> 24) & 0xFF;
        }
        type = e_NUM;
        break;
    case NUM: // number, like 1234
        match(NUM);
        if(pCi->value < 256)
        {
          pCi->p_code[pCi->pc++] = k_PUSH_NUM_N2;
          pCi->p_code[pCi->pc++] = pCi->value;
        }
        else
        {
          pCi->p_code[pCi->pc++] = k_PUSH_NUM_N5;
          pCi->p_code[pCi->pc++] = pCi->value & 0xFF;
          pCi->p_code[pCi->pc++] = (pCi->value >> 8) & 0xFF;
          pCi->p_code[pCi->pc++] = (pCi->value >> 16) & 0xFF;
          pCi->p_code[pCi->pc++] = (pCi->value >> 24) & 0xFF;
        }
        type = e_NUM;
        break;
    case ID: // variable or function call
        match(ID);
        if(lookahead() == '(') {
            uint16_t fidx = pCi->sym_idx;
            uint8_t rt = a_Symbol[fidx].return_type;
            if(rt == 0) {
                error("function without return value (define it before use)",
                    a_Symbol[fidx].name);
                break;
            }
            compile_func_call(fidx, true);
            type = (rt == NC_INT32) ? e_NUM : e_STR;
            break;
        }
        if(a_Symbol[pCi->sym_idx].is_local) {
            pCi->p_code[pCi->pc++] = k_PUSH_LOCAL_N2;
        } else {
            pCi->p_code[pCi->pc++] = k_PUSH_VAR_N2;
        }
        pCi->p_code[pCi->pc++] = a_Symbol[pCi->sym_idx].value;
        type = e_NUM;
        break;
    case ARR: // array access or reference
        val = a_Symbol[pCi->sym_idx].value;
        {
            bool arr_is_local = a_Symbol[pCi->sym_idx].is_local;
            match(ARR);
            tok = lookahead();
            if(tok == LBRACKET) {
                // arr[idx] - element access
                if(arr_is_local) {
                    // Local array: push addr from stack, then idx, then stack-based get
                    pCi->p_code[pCi->pc++] = k_PUSH_LOCAL_N2;
                    pCi->p_code[pCi->pc++] = val;
                    match(LBRACKET);
                    compile_expression(e_NUM);
                    match(RBRACKET);
                    pCi->p_code[pCi->pc++] = k_GET_ARR_ELEM_S_N1;
                } else {
                    // Global array: use variable-indexed opcode
                    match(LBRACKET);
                    compile_expression(e_NUM);
                    match(RBRACKET);
                    pCi->p_code[pCi->pc++] = k_GET_ARR_ELEM_N2;
                    pCi->p_code[pCi->pc++] = val;
                }
                type = e_NUM;
            } else {
                // arr without [] - return reference (like C)
                if(arr_is_local) {
                    pCi->p_code[pCi->pc++] = k_PUSH_LOCAL_N2;
                } else {
                    pCi->p_code[pCi->pc++] = k_PUSH_VAR_N2;
                }
                pCi->p_code[pCi->pc++] = val;
                type = e_REF;
            }
        }
        break;
#ifdef cfg_DATA_ACCESS
    case U8: // u8(arr, idx) - read byte
        compile_get(U8, k_GET_ARR_1BYTE_N2, k_GET_ARR_1BYTE_S_N1);
        type = e_NUM;
        break;
    case U16: // u16(arr, idx) - read word
        compile_get(U16, k_GET_ARR_2BYTE_N2, k_GET_ARR_2BYTE_S_N1);
        type = e_NUM;
        break;
    case U32: // u32(arr, idx) - read dword
        compile_get(U32, k_GET_ARR_4BYTE_N2, k_GET_ARR_4BYTE_S_N1);
        type = e_NUM;
        break;
    // REF is deprecated - array names without [] now work as references
#endif
    case PARAMS: // Move value from (external) parameter stack to the data stack
        match(PARAMS);
        match('(');
        match(')');
        pCi->p_code[pCi->pc++] = k_PARAMS_N1;
        type = e_STR;
        break;
    case PARAM: // Move value from (external) parameter stack to the data stack
        match(PARAM);
        match('(');
        match(')');
        pCi->p_code[pCi->pc++] = k_PARAM_N1;
        type = e_NUM;
        break;
    case STR: // string, like "Hello"
        match(STR);
        // push string address
        uint16_t len = strlen(pCi->a_buff);
        pCi->a_buff[len - 1] = '\0';
        if(pCi->pc + 2 + len - 1 > cfg_MAX_CODE_SIZE) {
            error("code size exceeded", NULL);
            break;
        }
        pCi->p_code[pCi->pc++] = k_PUSH_STR_Nx;
        pCi->p_code[pCi->pc++] = len - 1; // without quotes but with 0
        memcpy(&pCi->p_code[pCi->pc], pCi->a_buff + 1, len - 1);
        pCi->pc += len - 1;
        type = e_STR;
        break;
    case STRID: // string variable, like A$
        match(STRID);
        // String parameters live on the stack frame and hold the string address
        pCi->p_code[pCi->pc++] = a_Symbol[pCi->sym_idx].is_local ? k_PUSH_LOCAL_N2 : k_PUSH_VAR_N2;
        pCi->p_code[pCi->pc++] = a_Symbol[pCi->sym_idx].value;
        type = e_STR;
        break;
#ifdef cfg_STRING_SUPPORT        
    case LEFTS: // left function
        match(LEFTS);
        match('(');
        compile_expression(e_STR);
        match(',');
        compile_expression(e_NUM);
        match(')');
        pCi->p_code[pCi->pc++] = k_LEFT_STR_N1;
        type = e_STR;
        break;
    case RIGHTS: // right function
        match(RIGHTS);
        match('(');
        compile_expression(e_STR);
        match(',');
        compile_expression(e_NUM);
        match(')');
        pCi->p_code[pCi->pc++] = k_RIGHT_STR_N1;
        type = e_STR;
        break;
    case MIDS: // mid function
        match(MIDS);
        match('(');
        compile_expression(e_STR);
        match(',');
        compile_expression(e_NUM);
        match(',');
        type = compile_expression(e_NUM);
        match(')');
        pCi->p_code[pCi->pc++] = k_MID_STR_N1;
        type = e_STR;
        break;
    case LEN: // len function
        match(LEN);
        match('(');
        compile_expression(e_STR);
        match(')');
        pCi->p_code[pCi->pc++] = k_STR_LEN_N1;
        type = e_NUM;
        break;
    case VAL: // val function
        match(VAL);
        match('(');
        compile_expression(e_STR);
        match(')');
        pCi->p_code[pCi->pc++] = k_STR_TO_VAL_N1;
        type = e_NUM;
        break;
    case STRS: // str$ function
        match(STRS);
        match('(');
        compile_expression(e_NUM);
        match(')');
        pCi->p_code[pCi->pc++] = k_VAL_TO_STR_N1;
        type = e_STR;
        break;
    case HEXS: // hex function
        match(HEXS);
        match('(');
        compile_expression(e_NUM);
        match(')');
        pCi->p_code[pCi->pc++] = k_VAL_TO_HEX_N1;
        type = e_STR;
        break;
    case INSTR: // instr function
        match(INSTR);
        match('(');
        compile_expression(e_NUM);
        match(',');
        compile_expression(e_STR);
        match(',');
        compile_expression(e_STR);
        match(')');
        pCi->p_code[pCi->pc++] = k_INSTR_N1;
        type = e_NUM;
        break;
    case STRINGS: // string$ function
        match(STRINGS);
        match('(');
        compile_expression(e_NUM);
        match(',');
        tok = lookahead();
        compile_expression(e_STR);
        match(')');
        pCi->p_code[pCi->pc++] = k_ALLOC_STR_N1;
        type = e_STR;
        break;
#ifdef cfg_DATA_ACCESS
    case ARR2STRS: // arr2str$(arr, offs)
        match(ARR2STRS);
        match('(');
        compile_expression(e_REF);
        match(',');
        compile_expression(e_NUM);
        match(')');
        pCi->p_code[pCi->pc++] = k_ARR2STR_N1;
        type = e_STR;
        break;
#endif
#endif        
    case RND: // Random number
        match(RND);
        match('(');
        compile_expression(e_NUM);
        match(')');
        pCi->p_code[pCi->pc++] = k_RND_N1;
        type = e_NUM;
        break;
    case ABS: // Absolute value
        match(ABS);
        match('(');
        compile_expression(e_NUM);
        match(')');
        pCi->p_code[pCi->pc++] = k_ABS_N1;
        type = e_NUM;
        break;
    case LABEL: // Function call with return value: func(args)
        {
            match(LABEL);
            uint16_t func_sym = pCi->sym_idx;
            uint16_t addr = a_Symbol[func_sym].value;
            uint8_t call_param_count = 0;
            match('(');
            // Push arguments to parameter stack
            tok = lookahead();
            while(tok != ')') {
                compile_expression(e_ANY);  // Accept any type (e_NUM, e_STR, e_REF)
                pCi->p_code[pCi->pc++] = k_PUSH_PARAM_N1;
                call_param_count++;
                tok = lookahead();
                if(tok == ',') {
                    match(',');
                    tok = lookahead();
                }
            }
            match(')');
            // Check parameter count
            if(a_Symbol[func_sym].param_count != 0xFF && call_param_count != a_Symbol[func_sym].param_count) {
                error("wrong number of arguments", a_Symbol[func_sym].name);
            }
            // Generate GOSUB to function
            pCi->p_code[pCi->pc++] = k_GOSUB_N3;
            pCi->p_code[pCi->pc++] = addr & 0xFF;
            pCi->p_code[pCi->pc++] = (addr >> 8) & 0xFF;
            // Push return value from ret_val register onto stack
            pCi->p_code[pCi->pc++] = k_PUSH_RET_N1;
            type = e_NUM;
        }
        break;
    case XFUNC:
        match(XFUNC);
        type = compile_xfunc(e_ANY);
        pCi->p_code[pCi->pc++] = k_PARAM_N1;
        break;
    case NIL:
        match(NIL);
        pCi->p_code[pCi->pc++] = k_PUSH_NUM_N2;
        pCi->p_code[pCi->pc++] = 0;
        type = e_REF;
        break;
    default:
        error("syntax error", pCi->a_buff);
        break;
    }
    return type;
}
