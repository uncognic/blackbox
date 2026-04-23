# Debugger
## Overview: 
The interpreter provides an interactive debugger when it is run with `-d` or `--debug`.

## Modes
- Step mode: Step mode shows a per-instruction prompt and allows stepping through every opcode.
- Breakpoint mode: Breakpoint mode shows the prompt when the BREAK instruction is detected.

## BREAK instruction
- Opcode: `BREAK` (`Opcode::BREAK`)
- Behavior: Triggers a breakpoint event. In breakpoint mode, execution pauses when `BREAK` is executed.
