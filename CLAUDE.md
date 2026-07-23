# CLAUDE.md

## Project Overview

This repository is a fork of OpenOCD focused on extending support for modern ARM CoreSight trace infrastructure. The primary goal is to add functionality for newer CoreSight components while preserving OpenOCD's existing architecture and design philosophy.

Current development targets include:

- Embedded Trace Macrocell (ETMv3)
- Embedded Trace Macrocell (ETMv4)
- Trace Memory Controller (TMC) [Done]
- Additional CoreSight infrastructure required to support these components

The primary target architecture is **ARM Cortex-M**. Support for Cortex-A, Cortex-R, RISC-V, MIPS, Xtensa, and unrelated architectures should not be modified unless a change is required to improve a shared CoreSight abstraction.

This fork should remain compatible with upstream OpenOCD wherever practical.

---

## Project Goals

The purpose of this fork is to provide production-quality support for modern ARM CoreSight trace components while respecting OpenOCD's architecture.

Development priorities are, in order:

1. Correctness
2. Reliability
3. Maintainability
4. Readability
5. Performance

Avoid introducing unnecessary abstractions or redesigning existing OpenOCD infrastructure.

---

## Scope

Preferred areas of development include:

```
src/target/
src/target/arm*
src/target/cortex_m*
src/target/adi_v5*
src/target/coresight*
src/helper/
```

Changes outside these areas should be minimal and well justified.

Large-scale refactoring unrelated to ARM CoreSight support is prohibited.

---

## High-Level Architecture

The architecture should always be viewed as:

```
Hardware Probe
    ├── STLink
    └── JLink
    ↓
Transport
    ├── SWD
    └── JTAG
    ↓
ARM Debug Layer (Debug Access Port) ADIv5/v6
    ├── Debug Port (DP)
    └── Access Port (AP)
    ↓
CoreSight Components
    ├── ROM Tables
    ├── DWT
    ├── FPB
    ├── ITM
    ├── ETMv3
    ├── ETMv4
    ├── ETF
    ├── ETB
    ├── ETR
    ├── TMC
    └── TPIU
```

New functionality should integrate naturally into this hierarchy.

---

## CoreSight Design Principles

CoreSight components are independent hardware peripherals.

Drivers should not directly manipulate each other's internal implementation.

Good:

- ETMv4 driver configuring ETMv4 registers
- TMC driver managing trace buffers
- Shared CoreSight utilities handling component discovery

Bad:

- ETMv4 driver accessing TMC internals directly
- TMC driver containing ETMv4-specific logic
- Hard-coded assumptions about trace topology

Interactions between components should occur through clearly defined interfaces.

---

## Driver Responsibilities

Each CoreSight driver should own:

- Register definitions
- Register access helpers
- Capability detection
- Configuration routines
- Status reporting
- Data extraction (where applicable)

Drivers should expose a minimal public interface.

Avoid exposing internal implementation details.

---

## Separation of Responsibilities

Hardware drivers are responsible for hardware.

They should:

- Discover hardware
- Configure registers
- Read status
- Transfer trace data
- Validate hardware capabilities

Hardware drivers must **not** perform protocol decoding or instruction reconstruction.

Trace decoding belongs to higher-level software.

For example:

ETMv4 driver:
- Program trace registers
- Enable tracing
- Disable tracing

Trace decoder:
- Decode packets
- Reconstruct instruction execution
- Correlate with executable images

These are separate responsibilities, and the trace decoder isn't a concern for this project.

---

## Register Access

Always use existing OpenOCD access helpers.

Do not bypass the ADIv5 infrastructure.
Avoid direct transport operations unless no suitable abstraction exists.

---

## Error Handling

Never ignore return values.

Every OpenOCD function returning an error code must be checked.

Preferred style:

```c
retval = foo();

if (retval != ERROR_OK)
    return retval;
```

Do not silently continue after failures.

Provide meaningful error messages where appropriate.

---

## Logging

Use OpenOCD logging macros.

Typical usage:

```c
LOG_DEBUG(...)
LOG_INFO(...)
LOG_WARNING(...)
LOG_ERROR(...)
```

Debug logs should contain sufficient information to diagnose hardware failures without requiring a debugger.

Avoid excessive logging in hot paths.

---

## Memory Management

Prefer existing OpenOCD allocation helpers.

Every allocation must have a clearly defined owner.

Avoid:

- Memory leaks
- Hidden ownership
- Mutable global state
- Static buffers unless justified

Resource cleanup should mirror allocation order.

---

## TCL Commands

New TCL commands should:

- Follow existing naming conventions
- Validate arguments
- Return meaningful error codes
- Produce useful diagnostic messages

Command handlers should remain thin wrappers around reusable C functions.

Avoid placing business logic directly inside TCL command handlers.

---

## Coding Style

Follow existing OpenOCD conventions.

Specifically:

- C99
- Four-space indentation
- Opening braces on their own line
- Small, focused functions
- Descriptive names
- Early returns where appropriate
- Minimize nesting

Consistency with surrounding code is preferred over stylistic improvements.

---

## Performance

CoreSight interactions are frequently latency-sensitive.

Prefer:

- Bulk memory transfers
- Cached capability detection
- Minimal AP transactions
- Efficient register access

Avoid unnecessary polling or repeated hardware discovery.

---

## Things To Never Do

Never:

- Bypass ADIv5 abstractions
- Duplicate register definitions
- Ignore hardware capability bits
- Assume a component exists
- Assume fixed ROM table layouts
- Hardcode addresses when discovery is available
- Mix hardware access with trace decoding
- Introduce unnecessary global state
- Ignore return values

---

## AI Development Guidelines

When implementing new functionality:

- Study surrounding code before making changes.
- Match existing coding patterns unless there is a clear architectural improvement.
- Never commit unless explicitly asked to.
- Avoid unrelated refactoring while implementing new features.
- Prefer extending existing infrastructure over introducing parallel implementations.
- Preserve backward compatibility whenever practical.
- If multiple implementation strategies exist, choose the one that aligns best with OpenOCD's existing architecture rather than the most modern or abstract solution.
- Treat OpenOCD as a mature systems project where stability and predictability take precedence over novelty.

Before submitting changes, verify:

- The project builds without warnings introduced by your changes.
- Existing functionality remains unaffected.
- New code follows the style of the surrounding source files.
- All error paths correctly release allocated resources.
- Logging is sufficient for debugging but not excessively verbose.
