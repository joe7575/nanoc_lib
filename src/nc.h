/*

Copyright 2024-2026 Joachim Stolberg

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

#include <stdint.h>
#include <stdbool.h>
#include "nc_cfg.h"

#define SVERSION "2.3.4"

/*
** Data types for 'nc_define_external_function()'
*/
#define NB_NONE     (0)
#define NB_NUM      (1)
#define NB_STR      (2)
#define NB_ARR      (3)
#define NB_ANY      (4)
#define NB_REF      (5)

/*
** Return values of 'nc_run()'
*/
enum {
  NB_END = 0,  // programm end reached
  NB_ERROR,    // error in programm
  NB_BREAK,    // break command
  NB_BUSY,     // programm still running
  NB_RETI,     // return from interrupt
  NB_XFUNC,    // 'call' external function
};

#define NB_HALTED   (0x100)  // debugger breakpoint reached (outside the XFUNC range)

/*
** To be implemented by the user
*/
char *nc_get_code_line(void *fp, char *line, int max_line_len);
void nc_print(const char * format, ...);

/*
** Compiler / Interpreter
*/
void nc_init(void);
uint8_t nc_define_external_function(char *name, uint8_t num_params, uint8_t *types, uint8_t return_type);
void *nc_create(void);
uint16_t nc_compile(void *pv_vm, void *fp);
uint16_t nc_run(void *pv_vm, uint16_t *p_cycles);
void nc_reset(void *pv_vm);
void nc_destroy(void * pv_vm);

/*
** Helper functions
*/
void nc_dump_code(void *pv_vm);
void nc_output_symbol_table(void *pv_vm);

/*
** Call a function in the VM
*/
// return 0 if not found
uint16_t nc_get_label_address(void *pv_vm, char *name);
// return 255 if not found
void nc_set_pc(void * pv_vm, uint16_t addr);

/*
** Stack/parameter functions
*/
uint8_t nc_stack_depth(void *pv_vm);
int32_t nc_pop_num(void *pv_vm);
void nc_push_num(void *pv_vm, int32_t value);
char *nc_pop_str(void *pv_vm, char *str, uint8_t len);
void nc_push_str(void *pv_vm, char *str);

// @param idx = stack position (1..n) 1 = top of stack
int32_t nc_peek_num(void *pv_vm, uint8_t idx);

/*
** Array/String access functions
*/
uint16_t nc_pop_arr_ref(void *pv_vm);
uint16_t nc_read_arr(void *pv_vm, uint16_t ref, uint8_t *arr, uint16_t bytes);
uint16_t nc_write_arr(void *pv_vm, uint16_t ref, uint8_t *arr, uint16_t bytes);
