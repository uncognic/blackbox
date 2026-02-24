# Debugger
## Overview: 
The interpreter provides an interactive debugger when it is ran with `-d` or `--debug`

## Modes
- Step mode: Step mode shows a per-instruction prompt and allows stepping through every opcode.
- Breakpoint mode: Breakpoint mode shows the prompt when the BREAK instruction is detected.

## BREAK instruction
- Opcode: `OPCODE_BREAK`
- Behavior: Sets breakpoints for use with the breakpoint mode