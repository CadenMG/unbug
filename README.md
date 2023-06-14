# Features
- Launch, halt, and continue execution
    - Ex: `c`
- Set breakpoints on:
    - Memory addresses
        - Ex: `b 0x12345678`
    - Source code lines
    - Function entry
- Read and write registers and memory
    - Ex: `reg r r15`
    - Ex: `reg w r15 0x12345678`
    - Ex: `reg dump`
    - Ex: `mem r 0x12345678`
    - Ex: `mem w 0x12345678 0x12345678`
- Single stepping
    - Instruction
        - Ex: `stepi`
    - Step in
        - Ex: `step`
    - Step out
        - Ex: `finnish`
    - Step over
        - Ex: `next`
- Print current source location
- Print backtrace
- Print values of simple variables

# Supplemental
## How are breakpoints formed?
Two main kinds of breakpoints: hardware and software.  
Hardware breakpoints typically involve setting architecture-specific registers to produce your breaks for you. On x86, you can only have four hardware breakpoints set at a given time, but they give you the power to make them fire on reading from or writing to a given address rather than only executing code there.  
Software breakpoints are set by modifying the executing code on the fly. How do we do this? Through `ptrace`. On x86 this is accomplished by overwriting the instruction at that address with the `int 3` instruction ([interrupt 3](https://wiki.osdev.org/Interrupt_Vector_Table)). When the processor executes the interrupt instruction, control is passed to the breakpoint interrupt handler through a `SIGTRAP` signal. The debugger process then waits for this signal.
