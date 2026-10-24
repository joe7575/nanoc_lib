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
#include <time.h>
#include "nc.h"
#include "nc_int.h"

// Aggressive optimizations for nc_run
#pragma GCC push_options
#pragma GCC optimize ("O3")
#pragma GCC optimize ("unroll-loops")
#pragma GCC optimize ("inline-functions")
#pragma GCC optimize ("omit-frame-pointer")

#define STRBUF1  0x7FF1 // temporary string buffers
#define STRBUF2  0x7FF2

#define PUSH(x) vm->stack[(uint16_t)(vm->sp++) % cfg_STACK_SIZE] = (x)
#define POP()   vm->stack[(uint16_t)(--vm->sp) % cfg_STACK_SIZE]
#define TOP()   vm->stack[(uint16_t)(vm->sp - 1) % cfg_STACK_SIZE]
#define PEEK(x) vm->stack[(uint16_t)(vm->sp + (x)) % cfg_STACK_SIZE]

#define PPUSH(x) vm->paramstack[(uint8_t)(vm->psp++) % cfg_PARAMSTACK_SIZE] = x
#define PPOP()   vm->paramstack[(uint8_t)(--vm->psp) % cfg_PARAMSTACK_SIZE]

/***************************************************************************************************
**    static function-prototypes
***************************************************************************************************/
static char *get_string(t_VM *vm, uint16_t addr);
#ifdef cfg_STRING_SUPPORT
static char *alloc_temp_string(t_VM *vm, uint16_t *p_addr);
static uint16_t realloc_string(t_VM *vm);
#endif

/***************************************************************************************************
**    global functions
***************************************************************************************************/
void nc_reset(void *pv_vm) {
    t_VM *vm = pv_vm;
    vm->pc = 1;
    vm->sp = 0;
    vm->psp = 0;
    vm->fp = 0;
    vm->ret_val = 0;
    vm->nested_loop_idx = 0;
    memset(vm->variables, 0, sizeof(vm->variables));
    memset(vm->stack, 0, sizeof(vm->stack));
    memset(vm->paramstack, 0, sizeof(vm->paramstack));
    memset(vm->heap, 0, sizeof(vm->heap));
    nc_mem_init(vm);
}

/*
** Debug Interface
*/
int32_t nc_get_number(void *pv_vm, uint8_t var) {
    t_VM *vm = pv_vm;
    if(var >= cfg_NUM_VARS) {
        return 0;
    }
    return vm->variables[var];
}

#ifdef cfg_STRING_SUPPORT
char *nc_get_string(void *pv_vm, uint8_t var) {
    t_VM *vm = pv_vm;
    if(var >= cfg_NUM_VARS) {
        return 0;
    }
    return get_string(vm, vm->variables[var]);
}
#endif

int32_t nc_get_arr_elem(void *pv_vm, uint8_t var, uint16_t idx) {
    t_VM *vm = pv_vm;
    if(var >= cfg_NUM_VARS) {
        return 0;
    }
    uint16_t addr = vm->variables[var];
    return ACS32(vm->heap[(addr & 0x7FFF) + idx * sizeof(uint32_t)]);
}

/*
** External function interface
*/
int32_t nc_pop_num(void *pv_vm) {
    t_VM *vm = pv_vm;
    if(vm->psp == 0) {
        return 0;
    }
    return PPOP();
}

// @param idx = stack position (1..n) 1 = top of stack
int32_t nc_peek_num(void *pv_vm, uint8_t idx) {
    t_VM *vm = pv_vm;
    if(vm->psp < idx) {
        return -1;
    }
    return vm->paramstack[(vm->psp - idx) % cfg_PARAMSTACK_SIZE];
}

void nc_push_num(void *pv_vm, int32_t value) {
    t_VM *vm = pv_vm;
    if(vm->psp < cfg_PARAMSTACK_SIZE) {
        PPUSH(value);
    }
}

#ifdef cfg_STRING_SUPPORT
char *nc_pop_str(void *pv_vm, char *str, uint8_t len) {
    t_VM *vm = pv_vm;
    if(vm->psp == 0) {
        return NULL;
    }
    uint16_t addr = PPOP();
    strncpy(str, get_string(vm, addr), len);
    return str;
}

void nc_push_str(void *pv_vm, char *str) {
    t_VM *vm = pv_vm;
    uint16_t addr;
    char *ptr;
    if(vm->psp < cfg_PARAMSTACK_SIZE) {
        ptr = alloc_temp_string(vm, &addr);
        strncpy(ptr, str, sizeof(vm->strbuf1));
        PPUSH(addr);
    }
}
#endif

uint16_t nc_pop_arr_ref(void *pv_vm) {
    t_VM *vm = pv_vm;
    if(vm->psp == 0) {
        return 0;
    }
    return (uint16_t)PPOP();
}

uint16_t nc_read_arr(void *pv_vm, uint16_t addr, uint8_t *arr, uint16_t bytes) {
    t_VM *vm = pv_vm;
    if(addr < 0x8000) {
    	// Const string reference (code segment)
        addr = MIN(addr, vm->code_size);
        uint16_t size = MIN(strlen((char*)&vm->code[addr]) + 1, bytes);
        memcpy(arr, &vm->code[addr], size);
        return size;
    }
    uint16_t size = nc_mem_get_blocksize(vm, addr);
    if(size == 0) {
        memset(arr, 0, bytes);
        return 0;
    }
    size = MIN(size, bytes);
    memcpy(arr, &vm->heap[addr & 0x7FFF], size);
    return size;
}

uint16_t nc_write_arr(void *pv_vm, uint16_t addr, uint8_t *arr, uint16_t bytes) {
    t_VM *vm = pv_vm;
    if(addr < 0x8000) {
        return 0;
    }
    uint16_t size = nc_mem_get_blocksize(vm, addr);
    if(size == 0) {
        return 0;
    }
    size = MIN(size, bytes);
    memcpy(&vm->heap[addr & 0x7FFF], arr, size);
    return size;
}

uint8_t nc_stack_depth(void *pv_vm) {
    t_VM *vm = pv_vm;
    return vm->psp;
}

void nc_set_pc(void * pv_vm, uint16_t addr) {
    t_VM *vm = pv_vm;
    PUSH(vm->pc);
    vm->pc = addr;
}

/*
** Run the programm
*/
uint16_t nc_run(void *pv_vm, uint16_t *p_cycles) {
    int32_t tmp1, tmp2;
    int32_t loopval;
    uint16_t idx;
    uint16_t addr, size;
    uint16_t offs1;
#ifdef cfg_DATA_ACCESS
    uint16_t offs2, size1, size2;
#endif
    uint8_t  var, val;
#ifdef cfg_STRING_SUPPORT
    char *ptr, *str1, *str2;
#endif
    t_VM *vm = pv_vm;
    
    // Cache critical variables in registers to avoid RAM access overhead
    register uint16_t pc = vm->pc;
    register uint16_t sp = vm->sp;
    register uint16_t fp = vm->fp;
    register uint16_t cycles = *p_cycles;
    register uint8_t *code = vm->code;
    register int32_t *stack = vm->stack;
    
    // Local macros using register-cached variables
    #undef PUSH
    #undef POP
    #undef TOP
    #undef PEEK
    #define PUSH(x) stack[(uint16_t)(sp++) % cfg_STACK_SIZE] = (x)
    #define POP()   stack[(uint16_t)(--sp) % cfg_STACK_SIZE]
    #define TOP()   stack[(uint16_t)(sp - 1) % cfg_STACK_SIZE]
    #define PEEK(x) stack[(uint16_t)(sp + (x)) % cfg_STACK_SIZE]
    #define RETURN_VM(ret) do { vm->pc = pc; vm->sp = sp; vm->fp = fp; *p_cycles = cycles; return (ret); } while(0)

    while(cycles-- > 1)
    {
        switch (code[pc])
        {
        case k_END:
            RETURN_VM(NB_END);
        case k_PUSH_STR_Nx:
            tmp1 = code[pc + 1]; // string length
            PUSH(pc + 2);  // push string address
            pc += tmp1 + 2;
            break;
        case k_PUSH_NUM_N5:
            PUSH(ACS32(code[pc + 1]));
            pc += 5;
            break;
        case k_PUSH_NUM_N2:
            PUSH(code[pc + 1]);
            pc += 2;
            break;
        case k_PUSH_VAR_N2:
            var = code[pc + 1];
            PUSH(vm->variables[var]);
            pc += 2;
            break;
        case k_POP_VAR_N2:
            var = code[pc + 1];
            vm->variables[var] = POP();
            pc += 2;
            break;
        case k_INC_VAR_N2:
            var = code[pc + 1];
            vm->variables[var]++;
            pc += 2;
            break;
        case k_DEC_VAR_N2:
            var = code[pc + 1];
            vm->variables[var]--;
            pc += 2;
            break;
        // Local variable opcodes (stack-based with frame pointer)
        case k_ENTER_N2:
            // Save old frame pointer on stack, set new fp, reserve space
            var = code[pc + 1];  // number of local variables
            PUSH(fp);            // save old frame pointer
            fp = sp;             // new frame pointer
            sp += var;           // reserve space for local variables
            pc += 2;
            break;
        case k_LEAVE_N1:
            // Restore frame and return
            // If there's a value on top of stack (from return statement), save it to ret_val
            if(sp > fp) {
                vm->ret_val = POP();  // save return value to register
            }
            sp = fp;             // discard local variables
            fp = POP();          // restore old frame pointer
            addr = POP();        // return address
            pc = addr;
            break;
        case k_PUSH_RET_N1:
            // Push return value register onto stack (for use in expressions)
            PUSH(vm->ret_val);
            pc += 1;
            break;
        case k_PUSH_LOCAL_N2:
            var = code[pc + 1];  // local variable offset
            PUSH(vm->stack[fp + var]);
            pc += 2;
            break;
        case k_POP_LOCAL_N2:
            var = code[pc + 1];  // local variable offset
            vm->stack[fp + var] = POP();
            pc += 2;
            break;
        case k_POP_N1:
            // discard a value (e.g. the result of a statement-level xfunc call)
            if(sp > 0) {
                (void)POP();
            }
            pc += 1;
            break;
        case k_INC_LOCAL_N2:
            var = code[pc + 1];
            vm->stack[fp + var]++;
            pc += 2;
            break;
        case k_DEC_LOCAL_N2:
            var = code[pc + 1];
            vm->stack[fp + var]--;
            pc += 2;
            break;
#ifdef cfg_STRING_SUPPORT
        case k_POP_STR_N2:
            vm->pc = pc; vm->sp = sp;  // sync for realloc_string
            var  = code[pc + 1];
            addr = realloc_string(vm);
            sp = vm->sp;               // read back: realloc_string decrements vm->sp
            vm->variables[var] = addr;
            pc += 2;
            break;
#endif
        case k_DIM_ARR_N2:
            var = code[pc + 1];
#ifdef cfg_STRING_SUPPORT
            if(vm->variables[var] > 0x7FFF) {
                nc_mem_free(vm, vm->variables[var]);
            }
#else
             if(vm->variables[var] > 0) {
                nc_print("Error: Array already dimensioned\n");
                RETURN_VM(NB_ERROR);
            }
#endif
            size = POP();
            addr = nc_mem_alloc(vm, (size + 1) * sizeof(uint32_t));
            if(addr == 0) {
                nc_print("Error: Out of memory\n");
                RETURN_VM(NB_ERROR);
            }
            memset(&vm->heap[addr & 0x7FFF], 0, (size + 1) * sizeof(uint32_t));
            vm->variables[var] = addr;
            pc += 2;
            break;
        case k_BREAK_INSTR_N3:
            tmp1 = ACS16(code[pc + 1]);
            PPUSH(tmp1);
            pc += 3; 
            RETURN_VM(NB_BREAK);
        case k_BREAK_FOR_N3:
            (void)POP();  // remove step value
            (void)POP();  // remove loop end value
            vm->nested_loop_idx--;
            pc = ACS16(code[pc + 1]);
            break;
        case k_FOR_LEAVE_N2:
            // The loop values on the stack are discarded with the stack frame
            vm->nested_loop_idx -= code[pc + 1];
            pc += 2;
            break;
        case k_ADD_N1:
            tmp2 = POP();
            TOP() = TOP() + tmp2;
            pc += 1;
            break;
        case k_SUB_N1:
            tmp2 = POP();
            TOP() = TOP() - tmp2;
            pc += 1;
            break;
        case k_MUL_N1:
            tmp2 = POP();
            TOP() = TOP() * tmp2;
            pc += 1;
            break;
        case k_DIV_N1:
            tmp2 = POP();
            if(tmp2 == 0) {
                nc_print("Error: Division by zero\n");
                TOP() = 0;
            } else {
                TOP() = TOP() / tmp2;
            }
            pc += 1;
            break;
        case k_MOD_N1:
            tmp2 = POP();
            if(tmp2 == 0) {
                TOP() = 0;
            } else {
                TOP() = TOP() % tmp2;
            }
            pc += 1;
            break;
        case k_AND_N1:
            tmp2 = POP();
            TOP() = TOP() && tmp2;
            pc += 1;
            break;
        case k_OR_N1:
            tmp2 = POP();
            TOP() = TOP() || tmp2;
            pc += 1;
            break;
        case k_NOT_N1:
            TOP() = !TOP();
            pc += 1;
            break;
        case k_NEG_N1:
            TOP() = -TOP();
            pc += 1;
            break;
        case k_BAND_N1:
            tmp2 = POP();
            TOP() = TOP() & tmp2;
            pc += 1;
            break;
        case k_BOR_N1:
            tmp2 = POP();
            TOP() = TOP() | tmp2;
            pc += 1;
            break;
        case k_BXOR_N1:
            tmp2 = POP();
            TOP() = TOP() ^ tmp2;
            pc += 1;
            break;
        case k_SHL_N1:
            tmp2 = POP();
            TOP() = TOP() << tmp2;
            pc += 1;
            break;
        case k_SHR_N1:
            tmp2 = POP();
            TOP() = (int32_t)((uint32_t)TOP() >> tmp2);
            pc += 1;
            break;
        case k_EQUAL_N1:
            tmp2 = POP();
            TOP() = TOP() == tmp2;
            pc += 1;
            break;
        case k_NOT_EQUAL_N1:
            tmp2 = POP();
            TOP() = TOP() != tmp2;
            pc += 1;
            break;
        case k_LESS_N1:
            tmp2 = POP();
            TOP() = TOP() < tmp2;
            pc += 1;
            break;
        case k_LESS_EQU_N1:
            tmp2 = POP();
            TOP() = TOP() <= tmp2;
            pc += 1;
            break;
        case k_GREATER_N1:
            tmp2 = POP();
            TOP() = TOP() > tmp2;
            pc += 1;
            break;
        case k_GREATER_EQU_N1:
            tmp2 = POP();
            TOP() = TOP() >= tmp2;
            pc += 1;
            break;
        case k_GOTO_N3:
            pc = ACS16(code[pc + 1]);
            break;
        case k_GOSUB_N3:
            if(sp < cfg_STACK_SIZE) {
                PUSH(pc + 3);
                pc = ACS16(code[pc + 1]);
            } else {
                nc_print("Error: Call stack overflow\n");
                RETURN_VM(NB_ERROR);
            }
            break;
        case k_RETI_N1:
            // Clean up stack frame like LEAVE before returning
            sp = fp;             // discard local variables
            fp = POP();          // restore old frame pointer
            pc = (uint16_t)POP();  // return address
            RETURN_VM(NB_RETI);
        case k_FOR_CHECK_N4:
        case k_FOR_CHECK_LOCAL_N4:
            // Skip the loop body when the range is empty.
            // Stack: [end, step] with step on top.
            tmp2 = ACS16(code[pc + 1]);   // target (after k_NEXT)
            var = code[pc + 3];
            if(code[pc] == k_FOR_CHECK_LOCAL_N4) {
                loopval = vm->stack[fp + var];
            } else {
                loopval = vm->variables[var];
            }
            tmp1 = TOP();                 // step
            if((tmp1 > 0 && loopval > PEEK(-2)) ||
                    (tmp1 < 0 && loopval < PEEK(-2))) {
                (void)POP();              // step
                (void)POP();              // end
                vm->nested_loop_idx--;
                pc = tmp2;
            } else {
                pc += 4;
            }
            break;
        case k_FOR_N1:
            if(++vm->nested_loop_idx > cfg_MAX_FOR_LOOPS) {
                nc_print("Error: too many nested 'for' loops");
                RETURN_VM(NB_ERROR);
            }
            pc += 1;
            break;
        case k_NEXT_N4:
            // ID = ID + stack[-1]
            // IF ID <= stack[-2] GOTO start
            tmp1 = ACS16(code[pc + 1]);
            var = code[pc + 3];
            tmp2 = TOP(); // step value
            vm->variables[var] = vm->variables[var] + tmp2;
            if(tmp2 < 0) {
                if(vm->variables[var] >= PEEK(-2)) {
                    pc = tmp1;
                    break;
                }
            } else {
                if(vm->variables[var] <= PEEK(-2)) {
                    pc = tmp1;
                    break;
                }
            }
            pc += 4;
            (void)POP();  // remove step value
            (void)POP();  // remove loop end value
            vm->nested_loop_idx--;
            break;
        case k_NEXT_LOCAL_N4:
            // local_var = local_var + stack[-1]
            // IF local_var <= stack[-2] GOTO start
            tmp1 = ACS16(code[pc + 1]);
            var = code[pc + 3];
            tmp2 = TOP(); // step value
            vm->stack[fp + var] = vm->stack[fp + var] + tmp2;
            if(tmp2 < 0) {
                if(vm->stack[fp + var] >= PEEK(-2)) {
                    pc = tmp1;
                    break;
                }
            } else {
                if(vm->stack[fp + var] <= PEEK(-2)) {
                    pc = tmp1;
                    break;
                }
            }
            pc += 4;
            (void)POP();  // remove step value
            (void)POP();  // remove loop end value
            vm->nested_loop_idx--;
            break;
        case k_IF_N3:
            if(POP() == 0) {
              pc = ACS16(code[pc + 1]);
            } else {
              pc += 3;
            }
            break;
        case k_SET_ARR_ELEM_N2:
            var = code[pc + 1];
            addr = vm->variables[var] & 0x7FFF;
            tmp1 = POP();
            tmp2 = POP() * sizeof(uint32_t);
            if(tmp2 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            ACS32(vm->heap[addr + tmp2]) = tmp1;
            pc += 2;
            break;
        case k_GET_ARR_ELEM_N2:
            var = code[pc + 1];
            addr = vm->variables[var] & 0x7FFF;
            tmp1 = POP() * sizeof(uint32_t);
            if(tmp1 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            PUSH(ACS32(vm->heap[addr + tmp1]));
            pc += 2;
            break;
        case k_GET_ARR_ELEM_S_N1: // stack-based: pop idx, pop addr, push arr[idx]
            tmp1 = POP() * sizeof(uint32_t);  // index
            addr = POP() & 0x7FFF;            // address from stack
            if(tmp1 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            PUSH(ACS32(vm->heap[addr + tmp1]));
            pc += 1;
            break;
        case k_SET_ARR_ELEM_S_N1: // stack-based: pop val, pop idx, pop addr, arr[idx]=val
            tmp1 = POP();                     // value
            tmp2 = POP() * sizeof(uint32_t);  // index
            addr = POP() & 0x7FFF;            // address from stack
            if(tmp2 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            ACS32(vm->heap[addr + tmp2]) = tmp1;
            pc += 1;
            break;
        case k_GET_ARR_1BYTE_S_N1: // stack-based: pop idx, pop addr, push u8
            tmp1 = POP();              // byte index
            addr = POP() & 0x7FFF;     // address from stack
            if(tmp1 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            PUSH(ACS8(vm->heap[addr + tmp1]));
            pc += 1;
            break;
        case k_SET_ARR_1BYTE_S_N1: // stack-based: pop val, pop idx, pop addr, set u8
            tmp1 = POP();              // value
            tmp2 = POP();              // byte index
            addr = POP() & 0x7FFF;     // address from stack
            if(tmp2 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            ACS8(vm->heap[addr + tmp2]) = tmp1;
            pc += 1;
            break;
        case k_GET_ARR_2BYTE_S_N1: // stack-based: pop idx, pop addr, push u16
            tmp1 = POP();              // byte index
            addr = POP() & 0x7FFF;     // address from stack
            if(tmp1 + 1 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            PUSH(ACS16(vm->heap[addr + tmp1]));
            pc += 1;
            break;
        case k_SET_ARR_2BYTE_S_N1: // stack-based: pop val, pop idx, pop addr, set u16
            tmp1 = POP();              // value
            tmp2 = POP();              // byte index
            addr = POP() & 0x7FFF;     // address from stack
            if(tmp2 + 1 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            ACS16(vm->heap[addr + tmp2]) = tmp1;
            pc += 1;
            break;
        case k_GET_ARR_4BYTE_S_N1: // stack-based: pop idx, pop addr, push u32
            tmp1 = POP();              // byte index
            addr = POP() & 0x7FFF;     // address from stack
            if(tmp1 + 3 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            PUSH(ACS32(vm->heap[addr + tmp1]));
            pc += 1;
            break;
        case k_SET_ARR_4BYTE_S_N1: // stack-based: pop val, pop idx, pop addr, set u32
            tmp1 = POP();              // value
            tmp2 = POP();              // byte index
            addr = POP() & 0x7FFF;     // address from stack
            if(tmp2 + 3 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            ACS32(vm->heap[addr + tmp2]) = tmp1;
            pc += 1;
            break;
        case k_DISPATCH_Nx: {
            uint8_t count = code[pc + 1];
            int32_t index = POP();
            uint16_t instr_len = 2 + count * 2;
            if(index >= 0 && index < count) {
                // Push return address (after this instruction) like GOSUB
                if(sp < cfg_STACK_SIZE) {
                    PUSH(pc + instr_len);
                    // Jump to target function
                    uint16_t target_pos = pc + 2 + index * 2;
                    pc = ACS16(code[target_pos]);
                } else {
                    nc_print("Error: Call stack overflow\n");
                    RETURN_VM(NB_ERROR);
                }
            } else {
                // Out of range: skip
                pc += instr_len;
            }
            break;
        }
        case k_DISPATCH_JMP_Nx: {
            uint8_t count = code[pc + 1];
            uint16_t table_addr = ACS16(code[pc + 2]);
            int32_t index = POP();
            if(index >= 0 && index < count) {
                uint16_t target = ACS16(code[table_addr + index * 2]);
                pc = target;
            } else {
                // Out of range: skip to end (after jump table)
                pc = table_addr + count * 2;
            }
            break;
        }
#ifdef cfg_DATA_ACCESS            
        case k_SET_ARR_1BYTE_N2:
            var = code[pc + 1];
            addr = vm->variables[var] & 0x7FFF;
            tmp1 = POP();
            tmp2 = POP();
            if(tmp2 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            ACS8(vm->heap[addr + tmp2]) = tmp1;
            pc += 2;
            break;
        case k_GET_ARR_1BYTE_N2:
            var = code[pc + 1];
            addr = vm->variables[var] & 0x7FFF;
            tmp1 = POP();
            if(tmp1 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            PUSH(ACS8(vm->heap[addr + tmp1]));
            pc += 2;
            break;
        case k_SET_ARR_2BYTE_N2:
            var = code[pc + 1];
            addr = vm->variables[var] & 0x7FFF;
            tmp1 = POP();
            tmp2 = POP();
            if(tmp2 + 1 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            ACS16(vm->heap[addr + tmp2]) = tmp1;
            pc += 2;
            break;
        case k_GET_ARR_2BYTE_N2:
            var = code[pc + 1];
            addr = vm->variables[var] & 0x7FFF;
            tmp1 = POP();
            if(tmp1 + 1 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            PUSH(ACS16(vm->heap[addr + tmp1]));
            pc += 2;
            break;
        case k_SET_ARR_4BYTE_N2:
            var = code[pc + 1];
            addr = vm->variables[var] & 0x7FFF;
            tmp1 = POP();
            tmp2 = POP();
            if(tmp2 + 3 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            ACS32(vm->heap[addr + tmp2]) = tmp1;
            pc += 2;
            break;
        case k_GET_ARR_4BYTE_N2:
            var = code[pc + 1];
            addr = vm->variables[var] & 0x7FFF;
            tmp1 = POP();
            if(tmp1 + 3 >= nc_mem_get_blocksize(vm, addr)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            PUSH(ACS32(vm->heap[addr + tmp1]));
            pc += 2;
            break;
        case k_COPY_N1:
            // copy(arr, offs, arr, offs, bytes)
            size = POP();  // number of bytes
            offs2 = POP();  // source offset
            tmp2 = POP() & 0x7FFF;  // source address
            offs1 = POP();  // destination offset
            tmp1 = POP() & 0x7FFF;  // destination address
            size1 = nc_mem_get_blocksize(vm, tmp1);
            size2 = nc_mem_get_blocksize(vm, tmp2);
            if(size + offs1 > size1 || size + offs2 > size2) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            memcpy(&vm->heap[tmp1 + offs1], &vm->heap[tmp2 + offs2], size);
            pc += 1;
            break;
#ifdef cfg_STRING_SUPPORT
        case k_STR2ARR_N1:
            // str2arr(arr, offs, str)
            str1 = get_string(vm, POP());
            offs1 = POP();  // destination offset
            tmp1 = POP() & 0x7FFF;  // destination address
            size = strlen(str1) + 1;
            if(size + offs1 > nc_mem_get_blocksize(vm, tmp1)) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            memcpy(&vm->heap[tmp1 + offs1], str1, size);
            pc += 1;
            break;
        case k_ARR2STR_N1:
            // arr2str$(arr, offs)
            offs1 = POP();  // source offset
            tmp1 = POP() & 0x7FFF;  // source address
            size1 = nc_mem_get_blocksize(vm, tmp1);
            if(offs1 >= size1) {
                nc_print("Error: Array index out of bounds\n");
                RETURN_VM(NB_ERROR);
            }
            size = MIN(size1 - offs1, k_MAX_LINE_LEN - 1);  // no '\0' in the array: stop at its end
            ptr = alloc_temp_string(vm, &addr);
            strncpy(ptr, (char*)&vm->heap[tmp1 + offs1], size);
            ptr[size] = 0;
            PUSH(addr);
            pc += 1;
            break;
#endif
#endif
        case k_PARAM_N1:
        case k_PARAMS_N1:
            if(vm->psp > 0) {
                    tmp1 = PPOP();
            } else {
                    tmp1 = 0;
            }
            PUSH(tmp1);
            pc += 1;
            break;
        case k_XFUNC_N2:
            val = code[pc + 1];
            pc += 2;
            RETURN_VM(NB_XFUNC + val);
        case k_PUSH_PARAM_N1:
            PPUSH(POP());
            pc += 1;
            break;
        case k_FREE_N1:
            nc_print(" %u/%u/%u bytes free (code/data/heap)", cfg_MAX_CODE_SIZE - vm->code_size,
                sizeof(vm->variables) - (vm->num_vars * sizeof(uint32_t)), nc_mem_get_free(vm));
            pc += 1;
            break;
        case k_RND_N1:
            tmp1 = POP();
            if(tmp1 == 0) {
                PUSH(0);
            } else {
                PUSH(rand() % (tmp1 + 1));
            }
            pc += 1;
            break;
        case k_ABS_N1:
            tmp1 = POP();
            PUSH(tmp1 < 0 ? -tmp1 : tmp1);
            pc += 1;
            break;
#ifdef cfg_STRING_SUPPORT
        case k_ADD_STR_N1:
            tmp2 = POP();
            tmp1 = POP();
            str1 = get_string(vm, tmp1);
            str2 = get_string(vm, tmp2);
            ptr = alloc_temp_string(vm, &addr);
            strncpy(ptr, str1, k_MAX_LINE_LEN-1);
            strncat(ptr, str2, k_MAX_LINE_LEN-1);
            PUSH(addr);
            pc += 1;
            break;
        case k_STR_EQUAL_N1:
            tmp2 = POP();
            tmp1 = POP();
            PUSH(strcmp(get_string(vm, tmp1), get_string(vm, tmp2)) == 0 ? 1 : 0);
            pc += 1;
            break;
        case k_STR_NOT_EQU_N1 :
            tmp2 = POP();
            tmp1 = POP();
            PUSH(strcmp(get_string(vm, tmp1), get_string(vm, tmp2)) == 0 ? 0 : 1);
            pc += 1;
            break;
        case k_STR_LESS_N1:
            tmp2 = POP();
            tmp1 = POP();
            PUSH(strcmp(get_string(vm, tmp1), get_string(vm, tmp2)) < 0 ? 1 : 0);
            pc += 1;
            break;
        case k_STR_LESS_EQU_N1:
            tmp2 = POP();
            tmp1 = POP();
            PUSH(strcmp(get_string(vm, tmp1), get_string(vm, tmp2)) <= 0 ? 1 : 0);
            pc += 1;
            break;
        case k_STR_GREATER_N1:
            tmp2 = POP();
            tmp1 = POP();
            PUSH(strcmp(get_string(vm, tmp1), get_string(vm, tmp2)) > 0 ? 1 : 0);
            pc += 1;
            break;
        case k_STR_GREATER_EQU_N1:
            tmp2 = POP();
            tmp1 = POP();
            PUSH(strcmp(get_string(vm, tmp1), get_string(vm, tmp2)) >= 0 ? 1 : 0);
            pc += 1;
            break;
        case k_LEFT_STR_N1:
            tmp2 = POP();  // number of characters
            tmp1 = POP();  // string address
            tmp2 = MIN(k_MAX_LINE_LEN - 1, tmp2);
            ptr = alloc_temp_string(vm, &addr);
            strncpy(ptr, get_string(vm, tmp1), tmp2);
            ptr[tmp2] = 0;
            PUSH(addr);
            pc += 1;
            break;
        case k_RIGHT_STR_N1:
            tmp2 = POP();  // number of characters
            tmp1 = POP();  // string address
            str1 = get_string(vm, tmp1);
            size = strlen(str1);
            tmp2 = MIN(size, tmp2);
            ptr = alloc_temp_string(vm, &addr);
            strncpy(ptr, str1 + size - tmp2, tmp2);
            ptr[tmp2] = 0;
            PUSH(addr);
            pc += 1;
            break;
        case k_MID_STR_N1:
            tmp2 = POP();  // number of characters
            tmp1 = POP();  // start position (0-based)
            idx = POP();   // string address
            str1 = get_string(vm, idx);
            size = strlen(str1);
            tmp1 = MIN(size, tmp1);
            tmp2 = MIN(size - tmp1, tmp2);
            ptr = alloc_temp_string(vm, &addr);
            strncpy(ptr, str1 + tmp1, tmp2);
            ptr[tmp2] = 0;
            PUSH(addr);
            pc += 1;
            break;
        case k_STR_LEN_N1:
            tmp1 = POP();
            PUSH(strlen(get_string(vm, tmp1)));
            pc += 1;
            break;
        case k_STR_TO_VAL_N1:
            tmp1 = POP();
            PUSH(atoi(get_string(vm, tmp1)));
            pc += 1;
            break;
        case k_VAL_TO_STR_N1:
            tmp1 = POP();
            snprintf(alloc_temp_string(vm, &addr), sizeof(vm->strbuf1), "%d", tmp1);
            PUSH(addr);
            pc += 1;
            break;
        case k_VAL_TO_HEX_N1:
            tmp1 = POP();
            snprintf(alloc_temp_string(vm, &addr), sizeof(vm->strbuf1), "%X", tmp1);
            PUSH(addr);
            pc += 1;
            break;
        case k_INSTR_N1:
            tmp2 = POP();  // string address
            tmp1 = POP();  // search string
            val = POP();   // start position (0-based)
            val = MAX(val, 0);
            str1 = get_string(vm, tmp1);
            str2 = get_string(vm, tmp2);
            val = MIN(val, strlen(str1));
            str2 = strstr(&str1[val], str2);
            if(str2 == NULL) {
                PUSH(-1);  // not found: return -1 (C-style)
            } else {
                PUSH(str2 - str1);  // 0-based position
            }
            pc += 1;
            break;
#endif
#ifdef cfg_STRING_SUPPORT
        case k_ALLOC_STR_N1:
            tmp2 = POP();  // address of the fill char
            tmp2 = get_string(vm, tmp2)[0];
            tmp1 = POP();  // string length
            tmp1 = MIN(k_MAX_LINE_LEN - 1, tmp1);
            ptr = alloc_temp_string(vm, &addr);
            memset(ptr, tmp2, tmp1);
            ptr[tmp1] = 0;
            PUSH(addr);
            pc += 1;
            break;
#endif
        case k_PRINTF_Nx:
        {
            uint8_t nargs = code[pc + 1];
            uint8_t slen = code[pc + 2];
            char *fmt = (char*)&code[pc + 3];
            // Arguments are on stack in reverse order, pop them to temp array
            int32_t args[8];
            for(int i = nargs - 1; i >= 0; i--) {
                args[i] = POP();
            }
            uint8_t arg_idx = 0;
            for(char *p = fmt; *p; p++) {
                if(*p == '%' && *(p+1)) {
                    p++;
                    switch(*p) {
                        case 'd': nc_print("%d", (int)args[arg_idx++]); break;
                        case 'u': nc_print("%u", (unsigned)args[arg_idx++]); break;
                        case 'h': nc_print("%X", (unsigned)args[arg_idx++]); break;
                        case 'c': nc_print("%c", (int)args[arg_idx++]); break;
                        case 's': nc_print("%s", get_string(vm, args[arg_idx++])); break;
                        case '%': nc_print("%%"); break;
                        default: nc_print("%%%c", *p); break;
                    }
                } else {
                    nc_print("%c", *p);
                }
            }
            pc += 3 + slen;
            break;
        }
#ifdef cfg_DEBUG_SUPPORT
        case k_DBG_TRAP_N1:
            RETURN_VM(NB_HALTED);
#endif
        default:
            nc_print("Error: unknown opcode '%u'\n", code[pc]);
            RETURN_VM(NB_ERROR);
        }
    }
    RETURN_VM(NB_BUSY);
    
    // Restore original macros for functions outside nc_run
    #undef PUSH
    #undef POP
    #undef TOP
    #undef PEEK
    #define PUSH(x) vm->stack[(uint16_t)(vm->sp++) % cfg_STACK_SIZE] = (x)
    #define POP()   vm->stack[(uint16_t)(--vm->sp) % cfg_STACK_SIZE]
    #define TOP()   vm->stack[(uint16_t)(vm->sp - 1) % cfg_STACK_SIZE]
    #define PEEK(x) vm->stack[(uint16_t)(vm->sp + (x)) % cfg_STACK_SIZE]
}

#pragma GCC pop_options

void nc_destroy(void * pv_vm) {
    free(pv_vm);
}

#ifdef cfg_DEBUG_SUPPORT
char *nc_dbg_get_string(void *pv_vm, uint16_t addr) {
    return get_string((t_VM *)pv_vm, addr);
}
#endif

/***************************************************************************************************
* Static functions
***************************************************************************************************/
static char *get_string(t_VM *vm, uint16_t addr) {
#ifdef cfg_STRING_SUPPORT
    if(addr == STRBUF1) {
        return vm->strbuf1;
    } else if(addr == STRBUF2) {
        return vm->strbuf2;
    } else 
#endif
    if(addr >= 0x8000) {
        if(vm->heap[addr & 0x7FFF] == 0) {
            return "";
        }
        return (char*)&vm->heap[addr & 0x7FFF];
    } else if(addr == 0) {
        return "";
    } else {
        if(addr >= cfg_MAX_CODE_SIZE || vm->code[addr] == 0) {
            return "";
        }
        return (char*)&vm->code[addr];
    }
}

#ifdef cfg_STRING_SUPPORT
static char *alloc_temp_string(t_VM *vm, uint16_t *p_addr) {
    if(vm->strbuf1_used) {
        vm->strbuf1_used = false;
        *p_addr = STRBUF2;
        return vm->strbuf2;
    } else {
        vm->strbuf1_used = true;
        *p_addr = STRBUF1;
        return vm->strbuf1;
    }
}

static uint16_t realloc_string(t_VM *vm) {
    uint8_t var  = vm->code[vm->pc + 1];
    uint16_t addr = POP();
    char *ptr = get_string(vm, addr);
    uint16_t len = strlen(ptr) + 1;

    if(vm->variables[var] > 0x7FFF) { // heap buffer
        if(addr >= STRBUF1) { // no static string
            // Allocate a new buffer and copy the string
            uint16_t addr1 = nc_mem_realloc(vm, vm->variables[var], len);
            if(addr1 == 0) {
                nc_print("Error: Out of memory\n");
                return NB_ERROR;
            }
            memcpy(&vm->heap[addr1 & 0x7FFF], ptr, len);
            return addr1;
        } else {
            // Free the old buffer and use the static string
            nc_mem_free(vm, vm->variables[var]);
            return addr;
        }
    }
    if(addr >= STRBUF1) { // no static string
        // Allocate a new buffer and copy the string
        uint16_t addr1 = nc_mem_alloc(vm, len);
        if(addr1 == 0) {
            nc_print("Error: Out of memory\n");
            return NB_ERROR;
        }
        memcpy(&vm->heap[addr1 & 0x7FFF], ptr, len);
        return addr1;
    } else {
        // Use the new buffer
        return addr;
    }
}
#endif
