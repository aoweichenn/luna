#!/usr/bin/env python3

from __future__ import annotations

import signal
import unittest

from machine_ir_reference import (
    MachineIrError,
    MachineTrap,
    ReferenceInterpreter,
    exit_code,
    parse_module,
)


HEADER = """\
target-machine x86_64
target-triple "x86_64-unknown-linux-gnu"
module-kind executable

"""


class MachineIrReferenceTest(unittest.TestCase):
    def test_executes_calls_arithmetic_and_control_flow(self) -> None:
        module = parse_module(
            HEADER
            + """\
define @f0 test.reference::twice linkage=internal (i32:gpr) -> i32 {
  stack $s0 type=i32 size=8 align=8 class=scalar
  vreg %v0 type=i32 class=gpr
  vreg %v1 type=i32 class=gpr
  vreg %v2 type=i32 class=gpr

  bb0 predecessors=0:
    %v0 = load.slot type=i32 slot=$s0
    %v1 = const.integer type=i32 imm=0x0000000000000002
    %v2 = mul.integer type=i32 uses=[%v0, %v1]
    return type=void uses=[%v2]
}

define @f1 test.reference::main linkage=internal () -> i32 {
  stack $s0 type=bool size=8 align=8 class=scalar
  vreg %v0 type=i32 class=gpr
  vreg %v1 type=i32 class=gpr
  vreg %v2 type=i32 class=gpr
  vreg %v3 type=bool class=gpr
  vreg %v4 type=i32 class=gpr
  vreg %v5 type=i32 class=gpr

  bb0 predecessors=0:
    %v0 = const.integer type=i32 imm=0x0000000000000015
    %v1 = call type=i32 uses=[%v0] callee=@f0
    %v2 = const.integer type=i32 imm=0x000000000000002a
    %v3 = compare.equal type=bool uses=[%v1, %v2]
    branch type=void uses=[%v3] true=bb1 false=bb2
  bb1 predecessors=1:
    %v4 = const.integer type=i32 imm=0x000000000000002a
    return type=void uses=[%v4]
  bb2 predecessors=1:
    %v5 = const.integer type=i32 imm=0x0000000000000001
    return type=void uses=[%v5]
}
"""
        )
        self.assertEqual(exit_code(ReferenceInterpreter(module).run_main()), 42)

    def test_executes_aggregate_copy_and_indirect_memory(self) -> None:
        module = parse_module(
            HEADER
            + """\
define @f0 test.reference::copy linkage=internal (aggregate[8,4]:memory) -> aggregate[8,4]:memory {
  stack $s0 type=void size=8 align=4 class=memory
  stack $s1 type=void size=8 align=4 class=memory
  vreg %v0 type=ptr64 class=gpr
  vreg %v1 type=ptr64 class=gpr
  vreg %v2 type=ptr64 class=gpr

  bb0 predecessors=0:
    %v0 = address.slot type=ptr64 slot=$s1
    %v1 = address.slot type=ptr64 slot=$s0
    memory.copy type=void uses=[%v0, %v1] imm=0x0000000000000008
    %v2 = address.slot type=ptr64 slot=$s1
    return type=void uses=[%v2]
}

define @f1 test.reference::main linkage=internal () -> i32 {
  stack $s0 type=void size=8 align=4 class=memory
  stack $s1 type=void size=8 align=4 class=memory
  vreg %v0 type=ptr64 class=gpr
  vreg %v1 type=i32 class=gpr
  vreg %v2 type=ptr64 class=gpr
  vreg %v3 type=ptr64 class=gpr
  vreg %v4 type=i32 class=gpr

  bb0 predecessors=0:
    zero.slot type=void slot=$s0
    %v0 = address.slot type=ptr64 slot=$s0
    %v1 = const.integer type=i32 imm=0x000000000000002a
    store.memory type=void uses=[%v0, %v1] memory=i32
    %v2 = address.slot type=ptr64 slot=$s0
    %v3 = call type=ptr64 uses=[%v2] callee=@f0 result-slot=$s1
    %v4 = load.memory type=i32 uses=[%v3]
    return type=void uses=[%v4]
}
"""
        )
        self.assertEqual(exit_code(ReferenceInterpreter(module).run_main()), 42)

    def test_reports_reference_traps_with_the_target_signal(self) -> None:
        module = parse_module(
            HEADER
            + """\
define @f0 test.reference::main linkage=internal () -> i32 {
  vreg %v0 type=i32 class=gpr
  vreg %v1 type=i32 class=gpr
  vreg %v2 type=i32 class=gpr

  bb0 predecessors=0:
    %v0 = const.integer type=i32 imm=0x0000000000000001
    %v1 = const.integer type=i32 imm=0x0000000000000000
    %v2 = div.integer type=i32 uses=[%v0, %v1]
    return type=void uses=[%v2]
}
"""
        )
        with self.assertRaises(MachineTrap) as raised:
            ReferenceInterpreter(module).run_main()
        self.assertEqual(raised.exception.signal_number, signal.SIGFPE)

    def test_rejects_undeclared_virtual_registers(self) -> None:
        with self.assertRaises(MachineIrError):
            parse_module(
                HEADER
                + """\
define @f0 test.reference::main linkage=internal () -> i32 {
  vreg %v0 type=i32 class=gpr

  bb0 predecessors=0:
    %v0 = add.integer type=i32 uses=[%v0, %v9]
    return type=void uses=[%v0]
}
"""
            )


if __name__ == "__main__":
    unittest.main()
