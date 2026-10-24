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

#define k_MEM_BLOCK_SIZE    (8)     // Must be a multiple of 4 (real size is MIN_BLOCK_SIZE - 1)
#define k_MEM_FREE_TAG      (0)     // Also used for number of blocks
#define k_MAX_SYM_LEN       (16)    // Max. length of a symbol name incl. '\0'
#define k_MAX_LINE_LEN      (128)   // Max. length of a line/string
#define k_DATA_STR_TAG      (0x80000000) // To distinguish between strings and numbers in the data section


#define ACS8(x)   *(uint8_t*)&(x)
#define ACS16(x)  *(uint16_t*)&(x)
#define ACS32(x)  *(uint32_t*)&(x)
#define MIN(a,b)  ((a) < (b) ? (a) : (b))
#define MAX(a,b)  ((a) > (b) ? (a) : (b))

// Opcode definitions
enum {
    k_END,                // End of programm
    k_PUSH_STR_Nx,        // nn S T R I N G 00 (push string address (16 bit))
    k_PUSH_NUM_N5,        // (push 4 byte const value)
    k_PUSH_NUM_N2,        // (push 1 byte const value)     
    k_PUSH_VAR_N2,        // (push variable)
    k_POP_VAR_N2,         // (pop variable)
    k_POP_STR_N2,         // (pop variable)
    k_DIM_ARR_N2,         // (pop variable, pop size)
    k_BREAK_INSTR_N3,     // (break with line number)
    k_ADD_N1,             // (add two values from stack)
    k_SUB_N1,             // (sub two values from stack)
    k_MUL_N1,             // (mul two values from stack)
    k_DIV_N1,             // (div two values from stack)
    k_MOD_N1,             // (mod two values from stack)
    k_AND_N1,             // (pop two values from stack)
    k_OR_N1,              // (pop two values from stack)
    k_NOT_N1,             // (pop one value from stack)
    k_NEG_N1,             // (negate)
    k_BAND_N1,            // (bitwise and two values from stack)
    k_BOR_N1,             // (bitwise or two values from stack)
    k_BXOR_N1,            // (bitwise xor two values from stack)
    k_SHL_N1,             // (shift left two values from stack)
    k_SHR_N1,             // (shift right two values from stack)
    k_EQUAL_N1,           // (compare two values from stack)
    k_NOT_EQUAL_N1,       // (compare two values from stack)
    k_LESS_N1,            // (compare two values from stack)     
    k_LESS_EQU_N1,        // (compare two values from stack) 
    k_GREATER_N1,         // (compare two values from stack)      
    k_GREATER_EQU_N1,     // (compare two values from stack)
    k_GOTO_N3,            // (16 bit programm address)
    k_GOSUB_N3,           // (16 bit programm address)
    k_RETI_N1,            // (return from interrupt)
    k_FOR_N1,             // (check stack overflow)
    k_NEXT_N4,            // (16 bit programm address), (variable)
    k_IF_N3,              // (pop val, END address)
    k_SET_ARR_ELEM_N2,    // (set array element)
    k_GET_ARR_ELEM_N2,    // (get array element)
    k_SET_ARR_1BYTE_N2,   // (array: set one byte)
    k_GET_ARR_1BYTE_N2,   // (array: get one byte)
    k_SET_ARR_2BYTE_N2,   // (array: set one short)
    k_GET_ARR_2BYTE_N2,   // (array: get one short)
    k_SET_ARR_4BYTE_N2,   // (array: set one long)
    k_GET_ARR_4BYTE_N2,   // (array: get one long)
    k_COPY_N1,            // (copy)
    k_PARAM_N1,           // (pop and push value)
    k_PARAMS_N1,          // (pop and push string address)
    k_XFUNC_N2,           // (external function call)
    k_PUSH_PARAM_N1,      // (push value to parameter stack)
    k_FREE_N1,            // (free memory)
    k_RND_N1,             // (random number)
    k_ABS_N1,             // (absolute value)
    k_ADD_STR_N1,         // (add two strings from stack)
    k_STR_EQUAL_N1,       // (compare two values from stack)
    k_STR_NOT_EQU_N1,     // (compare two values from stack)
    k_STR_LESS_N1,        // (compare two values from stack)     
    k_STR_LESS_EQU_N1,    // (compare two values from stack) 
    k_STR_GREATER_N1,     // (compare two values from stack)      
    k_STR_GREATER_EQU_N1, // (compare two values from stack)
    k_LEFT_STR_N1,        // (left$)
    k_RIGHT_STR_N1,       // (right$)
    k_MID_STR_N1,         // (mid$)
    k_STR_LEN_N1,         // (len)
    k_STR_TO_VAL_N1,      // (val)
    k_VAL_TO_STR_N1,      // (str$)
    k_VAL_TO_HEX_N1,      // (hex$)
    k_INSTR_N1,           // (instr)
    k_ALLOC_STR_N1,       // (alloc string)
    k_PRINTF_Nx,          // (printf with format string)
    k_INC_VAR_N2,         // (increment variable)
    k_DEC_VAR_N2,         // (decrement variable)
    k_ENTER_N2,           // (enter function, reserve n local vars)
    k_LEAVE_N1,           // (leave function, restore frame)
    k_PUSH_RET_N1,        // (push return value register onto stack)
    k_PUSH_LOCAL_N2,      // (push local variable)
    k_POP_LOCAL_N2,       // (pop local variable)
    k_INC_LOCAL_N2,       // (increment local variable)
    k_DEC_LOCAL_N2,       // (decrement local variable)
    k_GET_ARR_ELEM_S_N1,  // stack-based: pop idx, pop addr, push arr[idx]
    k_SET_ARR_ELEM_S_N1,  // stack-based: pop val, pop idx, pop addr, arr[idx]=val
    k_GET_ARR_1BYTE_S_N1,  // stack-based: pop idx, pop addr, push u8
    k_SET_ARR_1BYTE_S_N1,  // stack-based: pop val, pop idx, pop addr, set u8
    k_GET_ARR_2BYTE_S_N1,  // stack-based: pop idx, pop addr, push u16
    k_SET_ARR_2BYTE_S_N1,  // stack-based: pop val, pop idx, pop addr, set u16
    k_GET_ARR_4BYTE_S_N1,  // stack-based: pop idx, pop addr, push u32
    k_SET_ARR_4BYTE_S_N1,  // stack-based: pop val, pop idx, pop addr, set u32
    k_DISPATCH_Nx,           // dispatch(expr) { func1 func2 ... }
    k_DISPATCH_JMP_Nx,       // dispatch(expr) { 0: code 1: code ... }
    k_NEXT_LOCAL_N4,         // (16 bit programm address), (local variable) - for-loop with local var
    k_DBG_TRAP_N1,           // debugger breakpoint, patched over the original opcode
    k_BREAK_FOR_N3,          // (16 bit programm address) - 'break' out of a for-loop
    k_FOR_LEAVE_N2,          // (number of for-loops) - 'return' from inside for-loops
    k_STR2ARR_N1,            // str2arr(arr, offs, str) - copy string incl. '\0' into array
    k_ARR2STR_N1,            // arr2str$(arr, offs) - read '\0'-terminated string from array
    k_FOR_CHECK_N4,          // skip empty for-loop: (target), (global var)
    k_FOR_CHECK_LOCAL_N4,    // skip empty for-loop: (target), (local var)
    k_POP_N1,                // discard one stack value (statement-level xfunc result)
};

// Token types
enum {
    NC_INT32 = 128, DIM, FOR, TO, // 128 - 131 (NC_INT32 replaces LET)
    STEP, NEXT, IF, THEN,       // 132 - 135
    PRINT, GOTO, GOSUB, RETURN, // 136 - 139
    END, REM, AND, OR,          // 140 - 143
    NOT, MOD, NUM, STR,         // 144 - 147
    ID, STRID, EQ, NQ,          // 148 - 151
    LE, LQ, GR, GQ,             // 152 - 155
    XFUNC, ARR, BREAK, LABEL,   // 156 - 159
    SET1, SET2, SET4, GET1,     // 160 - 163    
    GET2, GET4, LEFTS, RIGHTS,  // 164 - 167
    MIDS, LEN, VAL, STRS,       // 168 - 171
    SPC, PARAM, COPY, NC_CONST, // 172 - 175
    ERASE, ELSE, HEXS, NIL,     // 176 - 179
    INSTR, ON, TRON, TROFF,     // 180 - 183
    FREE, RND, PARAMS, STRINGS, // 184 - 187
    WHILE, LOOP, ENDIF, DATA,   // 188 - 191
    READ, RESTORE, REF, RETI,   // 192 - 195
    ELSEIF, SEMICOLON,           // 196 - 197
    LBRACE, RBRACE,              // 198 - 199 (C-style blocks)
    PRINTF, STR8,                // 200 - 201 (C-style printf, string type)
    LBRACKET, RBRACKET,          // 202 - 203 (C-style array brackets)
    NC_UINT32,                   // 204 (unsigned 32-bit array type)
    NC_VOID,                     // 205 (void function type)
    U8, U16, U32,                // 206 - 208 (C-style byte access)
    INC, DEC,                    // 209 - 210 (C-style ++ and --)
    LOCAL,                       // 211 (local variable marker)
    FUNC,                        // 212 (function definition keyword)
    RET,                         // 213 (return statement)
    DISPATCH,                    // 214 (indexed dispatch)
    SHL, SHR,                    // 215 - 216 (bit shift operators)
    ABS,                         // 217 (absolute value)
    BREAKPOINT,                  // 218 (breakpoint() statement)
    STR2ARR, ARR2STRS,           // 219 - 220 (string <-> array)
};

// Symbol table
typedef struct {
    char name[k_MAX_SYM_LEN];
    uint8_t  type;        // Token type
    uint8_t  is_local;    // 1 if local variable in void function
    uint8_t  param_count; // Number of parameters for LABEL (func), 0xFF = unknown
    uint8_t  return_type; // LABEL (func): 0 = void, NC_INT32 or STR8
    uint32_t value;       // Variable index (0..n) or label address, for locals: stack offset
} sym_t;

// Virtual machine
typedef struct {
    uint16_t code_size; // size of the compiled byte code
    uint16_t num_vars;  // number of used variables
    uint16_t pc;        // Programm counter
    uint16_t sp;        // Stack pointer
    uint16_t fp;        // Frame pointer for local variables
    int32_t  ret_val;   // Return value register
    uint8_t  psp;       // Parameter stack pointer
    uint8_t  nested_loop_idx;
    int32_t  stack[cfg_STACK_SIZE];
    int32_t  paramstack[cfg_PARAMSTACK_SIZE];
    uint32_t variables[cfg_NUM_VARS];
    uint8_t  code[cfg_MAX_CODE_SIZE];
#ifdef cfg_TRACE_SUPPORT
    uint16_t trace[cfg_MAX_CODE_SIZE];
#endif
    bool     trace_on;
    uint16_t mem_start_addr;    // Search start address for a free memory block
    uint16_t data_start_addr;   // Data section start address
    uint16_t data_read_offs;    // Data section read offset
    uint8_t  heap[cfg_MEM_HEAP_SIZE];
#ifdef cfg_STRING_SUPPORT    
    char     strbuf1[k_MAX_LINE_LEN]; // temporary buffer for string operations
    char     strbuf2[k_MAX_LINE_LEN]; // temporary buffer for string operations
    bool     strbuf1_used;            // flag to indicate which buffer is used
#endif
} t_VM;

char *nc_scanner(char *p_in, char *p_out);
sym_t *nc_get_symbol_table(uint16_t *p_start_idx);
int32_t nc_get_number(void *pv_vm, uint8_t var);
char *nc_get_string(void *pv_vm, uint8_t var);
int32_t nc_get_arr_elem(void *pv_vm, uint8_t var, uint16_t idx);
void nc_mem_init(t_VM *p_vm);
uint16_t nc_mem_alloc(t_VM *p_vm, uint16_t bytes);
void nc_mem_free(t_VM *p_vm, uint16_t addr);
uint16_t nc_mem_realloc(t_VM *p_vm, uint16_t addr, uint16_t bytes);
uint16_t nc_mem_get_blocksize(t_VM *p_vm, uint16_t addr);
uint16_t nc_mem_get_free(t_VM *p_vm);

#ifdef cfg_DEBUG_SUPPORT
// Line table entry, sorted by pc (one entry per source line that generates code)
typedef struct {
    uint16_t pc;
    uint16_t line;
} nc_dbg_line_t;

// Local variable/parameter of a function, valid for start_pc <= pc < end_pc
typedef struct {
    char     name[k_MAX_SYM_LEN];
    uint16_t start_pc;
    uint16_t end_pc;
    uint8_t  offs;      // stack offset relative to fp
    uint8_t  type;      // ID, STRID or ARR
} nc_dbg_local_t;

const nc_dbg_line_t *nc_dbg_get_lines(uint16_t *p_num);
const nc_dbg_local_t *nc_dbg_get_locals(uint16_t *p_num);
char *nc_dbg_get_string(void *pv_vm, uint16_t addr);
#endif
