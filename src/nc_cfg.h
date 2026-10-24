/*

Copyright 2024 Joachim Stolberg

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

#ifndef NC_CFG_H
#define NC_CFG_H

#define cfg_STRING_SUPPORT      // enable string support
#define cfg_DATA_ACCESS        // enable byte access to arrays
#define cfg_DEBUG_SUPPORT      // source level debugger: line/locals tables, k_DBG_TRAP_N1 (NB_HALTED)

#define cfg_MAX_FOR_LOOPS       (8)   // max nested FOR loops
#define cfg_STACK_SIZE          (128) // must be power of 2 (for fast modulo via bitmask)
#define cfg_PARAMSTACK_SIZE     (16)  // must be power of 2 (for fast modulo via bitmask)
#define cfg_NUM_VARS            (192) // must be in the range 8..256 (U8 index); incl. const values (e.g. injected SYS_*)
#define cfg_MAX_NUM_DATA        (200) // max number of constants (DATA section)
#define cfg_MAX_CODE_SIZE       (1024 * 16) // max code size in bytes
#define cfg_MEM_HEAP_SIZE       (1024 * 8) // must be a multiple of k_MEM_BLOCK_SIZE (8)
#define cfg_MAX_MEM_BLOCK_SIZE  (512) // max memory block size in bytes (multiple of 8)
#define cfg_MAX_NUM_XFUNC       (48)  // number of external function definitions
#define cfg_MAX_FW_DECL         (32)  // number of forward declarations (goto/gosub label)
#define cfg_NUM_KEYWORDS        (35)  // keywords in nc_init() (with STRING_SUPPORT and DATA_ACCESS)
#define cfg_MAX_NUM_SYM         (cfg_NUM_KEYWORDS + cfg_MAX_NUM_XFUNC + cfg_NUM_VARS + cfg_MAX_FW_DECL)
#define cfg_DBG_MAX_LINES       (512) // debugger: max. source lines with code (4 bytes each)
#define cfg_DBG_MAX_LOCALS      (48)  // debugger: max. local variable names (20 bytes each)

#endif /* NC_CFG_H */

