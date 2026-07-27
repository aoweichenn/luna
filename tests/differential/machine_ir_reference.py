#!/usr/bin/env python3

from __future__ import annotations

import dataclasses
import math
import re
import signal
import struct


ALL_MACHINE_OPCODES = frozenset(
    {
        "const.integer",
        "const.float",
        "const.bool",
        "const.null",
        "load.slot",
        "store.slot",
        "address.slot",
        "address.member",
        "address.global",
        "zero.slot",
        "memory.copy",
        "load.memory",
        "store.memory",
        "check.null",
        "check.bounds",
        "address.index",
        "neg.integer",
        "neg.float",
        "not.integer",
        "not.bool",
        "convert.integer",
        "convert.float",
        "convert.integer_to_float",
        "convert.float_to_integer",
        "convert.pointer_to_integer",
        "convert.integer_to_pointer",
        "add.integer",
        "sub.integer",
        "mul.integer",
        "div.integer",
        "rem.integer",
        "and.integer",
        "or.integer",
        "xor.integer",
        "shift_left.integer",
        "shift_right.integer",
        "add.float",
        "sub.float",
        "mul.float",
        "div.float",
        "compare.equal",
        "compare.not_equal",
        "compare.less.integer",
        "compare.less_equal.integer",
        "compare.greater.integer",
        "compare.greater_equal.integer",
        "compare.less.float",
        "compare.less_equal.float",
        "compare.greater.float",
        "compare.greater_equal.float",
        "call",
        "jump",
        "branch",
        "return",
    }
)

TYPE_WIDTH_BITS = {
    "bool": 8,
    "i8": 8,
    "i16": 16,
    "i32": 32,
    "i64": 64,
    "u8": 8,
    "u16": 16,
    "u32": 32,
    "u64": 64,
    "f32": 32,
    "f64": 64,
    "ptr64": 64,
}
INTEGER_TYPES = frozenset(
    {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64"}
)
SIGNED_INTEGER_TYPES = frozenset({"i8", "i16", "i32", "i64"})
FLOAT_TYPES = frozenset({"f32", "f64"})

FUNCTION_PATTERN = re.compile(
    r"^(declare|define) @f([0-9]+) ([^ ]+) linkage=([a-z-]+) "
    r"\((.*)\) -> ([^ ]+)"
    r"(?: metadata=0x[0-9a-f]+)?( \{)?$"
)
GLOBAL_PATTERN = re.compile(
    r"^global @g([0-9]+) align=([0-9]+) section=(rodata|data) "
    r"bytes=\[([0-9a-f ]*)\]$"
)
STACK_PATTERN = re.compile(
    r"^stack \$s([0-9]+) type=([a-z0-9]+) size=([0-9]+) "
    r"align=([0-9]+) class=(scalar|memory)$"
)
VREG_PATTERN = re.compile(
    r"^vreg %v([0-9]+) type=([a-z0-9]+) class=(gpr|fpr)$"
)
BLOCK_PATTERN = re.compile(r"^bb([0-9]+) predecessors=([0-9]+):$")
INSTRUCTION_PATTERN = re.compile(
    r"^(?:(%v([0-9]+)) = )?([a-z_.]+) type=([a-z0-9]+)(.*)$"
)


class MachineIrError(ValueError):
    pass


class MachineTrap(RuntimeError):
    def __init__(self, signal_number: int, reason: str):
        super().__init__(reason)
        self.signal_number = signal_number


@dataclasses.dataclass(frozen=True)
class TypeDescriptor:
    name: str
    aggregate_size_bytes: int | None = None
    aggregate_alignment_bytes: int | None = None

    @property
    def is_aggregate(self) -> bool:
        return self.aggregate_size_bytes is not None


@dataclasses.dataclass(frozen=True)
class StackSlot:
    type_name: str
    size_bytes: int
    alignment_bytes: int
    is_scalar: bool


@dataclasses.dataclass(frozen=True)
class Instruction:
    opcode: str
    type_name: str
    result: int | None
    uses: tuple[int, ...]
    memory_type: str | None
    slot: int | None
    global_id: int | None
    callee: int | None
    result_slot: int | None
    true_block: int | None
    false_block: int | None
    immediate: int | None


@dataclasses.dataclass
class BasicBlock:
    block_id: int
    instructions: list[Instruction] = dataclasses.field(default_factory=list)


@dataclasses.dataclass
class Function:
    function_id: int
    name: str
    linkage: str
    parameters: tuple[TypeDescriptor, ...]
    return_type: TypeDescriptor
    is_declaration: bool
    slots: dict[int, StackSlot] = dataclasses.field(default_factory=dict)
    value_types: dict[int, str] = dataclasses.field(default_factory=dict)
    blocks: dict[int, BasicBlock] = dataclasses.field(default_factory=dict)


@dataclasses.dataclass(frozen=True)
class Global:
    global_id: int
    alignment_bytes: int
    is_read_only: bool
    contents: bytes


@dataclasses.dataclass
class Module:
    kind: str
    globals: dict[int, Global] = dataclasses.field(default_factory=dict)
    functions: dict[int, Function] = dataclasses.field(default_factory=dict)

    def opcodes(self) -> set[str]:
        return {
            instruction.opcode
            for function in self.functions.values()
            for block in function.blocks.values()
            for instruction in block.instructions
        }


@dataclasses.dataclass(frozen=True)
class MachineValue:
    type_name: str
    bits: int


@dataclasses.dataclass
class MemoryRegion:
    base_address: int
    contents: bytearray
    is_read_only: bool
    is_alive: bool = True


@dataclasses.dataclass(frozen=True)
class CallResult:
    value: MachineValue | None
    aggregate: bytes | None


def _parse_type_descriptor(text: str) -> TypeDescriptor:
    aggregate_match = re.fullmatch(
        r"aggregate\[([0-9]+),([0-9]+)\]:memory", text
    )
    if aggregate_match is not None:
        return TypeDescriptor(
            "ptr64",
            int(aggregate_match.group(1)),
            int(aggregate_match.group(2)),
        )
    scalar_match = re.fullmatch(r"([a-z0-9]+)(?::(?:gpr|fpr))?", text)
    if scalar_match is None:
        raise MachineIrError(f"invalid machine type descriptor: {text}")
    name = scalar_match.group(1)
    if name != "void" and name not in TYPE_WIDTH_BITS:
        raise MachineIrError(f"unknown machine type: {name}")
    return TypeDescriptor(name)


def _split_parameters(text: str) -> tuple[TypeDescriptor, ...]:
    if text == "":
        return ()
    parameters: list[str] = []
    start = 0
    bracket_depth = 0
    for index, character in enumerate(text):
        if character == "[":
            bracket_depth += 1
        elif character == "]":
            bracket_depth -= 1
        elif character == "," and bracket_depth == 0:
            parameters.append(text[start:index].strip())
            start = index + 1
        if bracket_depth < 0:
            raise MachineIrError("unbalanced aggregate parameter descriptor")
    if bracket_depth != 0:
        raise MachineIrError("unbalanced aggregate parameter descriptor")
    parameters.append(text[start:].strip())
    return tuple(_parse_type_descriptor(item) for item in parameters)


def _parse_optional_id(
    remainder: str, field: str, prefix: str
) -> int | None:
    match = re.search(
        rf"(?:^| ){re.escape(field)}={re.escape(prefix)}([0-9]+)(?: |$)",
        remainder,
    )
    return None if match is None else int(match.group(1))


def _parse_instruction(text: str) -> Instruction:
    match = INSTRUCTION_PATTERN.fullmatch(text)
    if match is None:
        raise MachineIrError(f"invalid machine instruction: {text}")
    result = None if match.group(2) is None else int(match.group(2))
    opcode = match.group(3)
    type_name = match.group(4)
    remainder = match.group(5)
    if opcode not in ALL_MACHINE_OPCODES:
        raise MachineIrError(f"unknown machine opcode: {opcode}")
    if type_name != "void" and type_name not in TYPE_WIDTH_BITS:
        raise MachineIrError(f"unknown instruction type: {type_name}")

    uses_match = re.search(r"(?:^| )uses=\[([^\]]*)\](?: |$)", remainder)
    uses: tuple[int, ...] = ()
    if uses_match is not None and uses_match.group(1) != "":
        rendered_uses = uses_match.group(1).split(", ")
        if any(re.fullmatch(r"%v[0-9]+", item) is None for item in rendered_uses):
            raise MachineIrError(f"invalid use list: {uses_match.group(1)}")
        uses = tuple(int(item[2:]) for item in rendered_uses)

    memory_match = re.search(
        r"(?:^| )memory=([a-z0-9]+)(?: |$)", remainder
    )
    immediate_match = re.search(
        r"(?:^| )imm=(0x[0-9a-f]+)(?: |$)", remainder
    )
    return Instruction(
        opcode=opcode,
        type_name=type_name,
        result=result,
        uses=uses,
        memory_type=(
            None if memory_match is None else memory_match.group(1)
        ),
        slot=_parse_optional_id(remainder, "slot", "$s"),
        global_id=_parse_optional_id(remainder, "global", "@g"),
        callee=_parse_optional_id(remainder, "callee", "@f"),
        result_slot=_parse_optional_id(remainder, "result-slot", "$s"),
        true_block=_parse_optional_id(remainder, "true", "bb"),
        false_block=_parse_optional_id(remainder, "false", "bb"),
        immediate=(
            None if immediate_match is None else int(immediate_match.group(1), 16)
        ),
    )


def parse_module(text: str) -> Module:
    lines = text.splitlines()
    if len(lines) < 3 or lines[0] != "target-machine x86_64":
        raise MachineIrError("missing x86-64 target-machine header")
    if lines[1] != 'target-triple "x86_64-unknown-linux-gnu"':
        raise MachineIrError("unexpected target triple")
    if not lines[2].startswith("module-kind "):
        raise MachineIrError("missing module-kind header")
    module = Module(lines[2].removeprefix("module-kind "))
    current_function: Function | None = None
    current_block: BasicBlock | None = None

    for line_number, raw_line in enumerate(lines[3:], start=4):
        text_line = raw_line.strip()
        if text_line == "":
            continue
        if current_function is None:
            global_match = GLOBAL_PATTERN.fullmatch(text_line)
            if global_match is not None:
                global_id = int(global_match.group(1))
                if global_id in module.globals:
                    raise MachineIrError(f"duplicate global @g{global_id}")
                byte_text = global_match.group(4)
                contents = (
                    b""
                    if byte_text == ""
                    else bytes(int(item, 16) for item in byte_text.split())
                )
                module.globals[global_id] = Global(
                    global_id,
                    int(global_match.group(2)),
                    global_match.group(3) == "rodata",
                    contents,
                )
                continue

            function_match = FUNCTION_PATTERN.fullmatch(text_line)
            if function_match is None:
                raise MachineIrError(
                    f"line {line_number}: invalid top-level machine IR"
                )
            function_id = int(function_match.group(2))
            if function_id in module.functions:
                raise MachineIrError(f"duplicate function @f{function_id}")
            is_declaration = function_match.group(1) == "declare"
            has_body = function_match.group(7) is not None
            if is_declaration == has_body:
                raise MachineIrError(
                    f"function @f{function_id} has an invalid body marker"
                )
            function = Function(
                function_id=function_id,
                name=function_match.group(3),
                linkage=function_match.group(4),
                parameters=_split_parameters(function_match.group(5)),
                return_type=_parse_type_descriptor(function_match.group(6)),
                is_declaration=is_declaration,
            )
            module.functions[function_id] = function
            if not is_declaration:
                current_function = function
            continue

        if text_line == "}":
            if current_block is None or not current_block.instructions:
                raise MachineIrError(
                    f"function @f{current_function.function_id} has no body"
                )
            current_function = None
            current_block = None
            continue

        stack_match = STACK_PATTERN.fullmatch(text_line)
        if stack_match is not None:
            if current_block is not None:
                raise MachineIrError("stack slot declared after a basic block")
            slot_id = int(stack_match.group(1))
            if slot_id in current_function.slots:
                raise MachineIrError(f"duplicate stack slot $s{slot_id}")
            current_function.slots[slot_id] = StackSlot(
                type_name=stack_match.group(2),
                size_bytes=int(stack_match.group(3)),
                alignment_bytes=int(stack_match.group(4)),
                is_scalar=stack_match.group(5) == "scalar",
            )
            continue

        vreg_match = VREG_PATTERN.fullmatch(text_line)
        if vreg_match is not None:
            if current_block is not None:
                raise MachineIrError("virtual register declared after a block")
            value_id = int(vreg_match.group(1))
            if value_id in current_function.value_types:
                raise MachineIrError(f"duplicate virtual register %v{value_id}")
            current_function.value_types[value_id] = vreg_match.group(2)
            continue

        block_match = BLOCK_PATTERN.fullmatch(text_line)
        if block_match is not None:
            block_id = int(block_match.group(1))
            if block_id in current_function.blocks:
                raise MachineIrError(f"duplicate basic block bb{block_id}")
            current_block = BasicBlock(block_id)
            current_function.blocks[block_id] = current_block
            continue

        if current_block is None:
            raise MachineIrError(
                f"line {line_number}: instruction outside a basic block"
            )
        instruction = _parse_instruction(text_line)
        if (
            instruction.result is not None
            and instruction.result not in current_function.value_types
        ):
            raise MachineIrError(
                f"instruction defines undeclared %v{instruction.result}"
            )
        for use in instruction.uses:
            if use not in current_function.value_types:
                raise MachineIrError(f"instruction uses undeclared %v{use}")
        current_block.instructions.append(instruction)

    if current_function is not None:
        raise MachineIrError("unterminated machine function")
    if not module.functions:
        raise MachineIrError("machine module has no functions")
    return module


def _mask(type_name: str) -> int:
    return (1 << TYPE_WIDTH_BITS[type_name]) - 1


def _unsigned(value: MachineValue) -> int:
    return value.bits & _mask(value.type_name)


def _signed(value: MachineValue) -> int:
    raw = _unsigned(value)
    width = TYPE_WIDTH_BITS[value.type_name]
    sign_bit = 1 << (width - 1)
    return raw - (1 << width) if raw & sign_bit else raw


def _integer_value(value: MachineValue) -> int:
    if value.type_name in SIGNED_INTEGER_TYPES:
        return _signed(value)
    return _unsigned(value)


def _float_from_bits(value: MachineValue) -> float:
    if value.type_name == "f32":
        return struct.unpack("<f", struct.pack("<I", value.bits & 0xFFFFFFFF))[0]
    return struct.unpack("<d", struct.pack("<Q", value.bits & 0xFFFFFFFFFFFFFFFF))[0]


def _round_f32(value: float) -> float:
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def _float_value(type_name: str, value: float) -> MachineValue:
    if type_name == "f32":
        rounded = _round_f32(value)
        bits = struct.unpack("<I", struct.pack("<f", rounded))[0]
    else:
        bits = struct.unpack("<Q", struct.pack("<d", value))[0]
    return MachineValue(type_name, bits)


def _integer_result(type_name: str, value: int) -> MachineValue:
    return MachineValue(type_name, value & _mask(type_name))


class ReferenceInterpreter:
    def __init__(
        self,
        module: Module,
        *,
        instruction_budget: int = 1_000_000,
        call_depth_limit: int = 256,
    ):
        self.module = module
        self.instruction_budget = instruction_budget
        self.call_depth_limit = call_depth_limit
        self.executed_instruction_count = 0
        self.executed_opcodes: set[str] = set()
        self.call_depth = 0
        self.next_address = 0x10000
        self.regions: list[MemoryRegion] = []
        self.global_addresses: dict[int, int] = {}
        for global_id in sorted(module.globals):
            global_value = module.globals[global_id]
            self.global_addresses[global_id] = self._allocate(
                len(global_value.contents),
                global_value.alignment_bytes,
                global_value.is_read_only,
                global_value.contents,
            ).base_address

    def run_main(self) -> MachineValue:
        matches = [
            function
            for function in self.module.functions.values()
            if not function.is_declaration
            and (function.name == "main" or function.name.endswith("::main"))
        ]
        if len(matches) != 1:
            raise MachineIrError(
                f"expected one executable main function, found {len(matches)}"
            )
        result = self._execute_function(matches[0].function_id, ())
        if result.value is None or result.aggregate is not None:
            raise MachineIrError("main did not return a scalar value")
        return result.value

    def _align_address(self, alignment_bytes: int) -> int:
        if alignment_bytes <= 0 or alignment_bytes & (alignment_bytes - 1):
            raise MachineIrError("memory alignment is not a positive power of two")
        return (self.next_address + alignment_bytes - 1) & -alignment_bytes

    def _allocate(
        self,
        size_bytes: int,
        alignment_bytes: int,
        is_read_only: bool,
        initial_contents: bytes = b"",
    ) -> MemoryRegion:
        if size_bytes <= 0 or len(initial_contents) > size_bytes:
            raise MachineIrError("invalid virtual memory allocation")
        base_address = self._align_address(alignment_bytes)
        contents = bytearray(size_bytes)
        contents[: len(initial_contents)] = initial_contents
        region = MemoryRegion(base_address, contents, is_read_only)
        self.regions.append(region)
        self.next_address = base_address + size_bytes + 16
        return region

    def _resolve(
        self, address: int, size_bytes: int, *, for_write: bool = False
    ) -> tuple[MemoryRegion, int]:
        for region in reversed(self.regions):
            offset = address - region.base_address
            if (
                region.is_alive
                and offset >= 0
                and size_bytes <= len(region.contents) - offset
            ):
                if for_write and region.is_read_only:
                    raise MachineTrap(
                        signal.SIGILL, "write to read-only virtual memory"
                    )
                return region, offset
        raise MachineTrap(signal.SIGILL, "invalid virtual memory access")

    def _read(self, address: int, size_bytes: int) -> bytes:
        region, offset = self._resolve(address, size_bytes)
        return bytes(region.contents[offset : offset + size_bytes])

    def _write(self, address: int, contents: bytes) -> None:
        region, offset = self._resolve(
            address, len(contents), for_write=True
        )
        region.contents[offset : offset + len(contents)] = contents

    def _read_value(self, address: int, type_name: str) -> MachineValue:
        size_bytes = TYPE_WIDTH_BITS[type_name] // 8
        bits = int.from_bytes(self._read(address, size_bytes), "little")
        return MachineValue(type_name, bits)

    def _write_value(self, address: int, value: MachineValue) -> None:
        size_bytes = TYPE_WIDTH_BITS[value.type_name] // 8
        self._write(
            address,
            (_unsigned(value)).to_bytes(size_bytes, "little"),
        )

    @staticmethod
    def _value(
        values: dict[int, MachineValue], value_id: int
    ) -> MachineValue:
        try:
            return values[value_id]
        except KeyError as error:
            raise MachineIrError(
                f"virtual register %v{value_id} is read before definition"
            ) from error

    def _execute_function(
        self, function_id: int, arguments: tuple[MachineValue, ...]
    ) -> CallResult:
        try:
            function = self.module.functions[function_id]
        except KeyError as error:
            raise MachineIrError(f"call names missing @f{function_id}") from error
        if function.is_declaration:
            raise MachineIrError(
                f"reference execution cannot call declaration {function.name}"
            )
        if len(arguments) != len(function.parameters):
            raise MachineIrError(
                f"{function.name} received {len(arguments)} arguments, "
                f"expected {len(function.parameters)}"
            )
        if self.call_depth >= self.call_depth_limit:
            raise MachineIrError("reference call-depth limit exceeded")

        frame_regions: list[MemoryRegion] = []
        slot_addresses: dict[int, int] = {}
        for slot_id in sorted(function.slots):
            slot = function.slots[slot_id]
            region = self._allocate(
                slot.size_bytes, slot.alignment_bytes, False
            )
            frame_regions.append(region)
            slot_addresses[slot_id] = region.base_address

        for index, (descriptor, argument) in enumerate(
            zip(function.parameters, arguments, strict=True)
        ):
            if index not in slot_addresses:
                raise MachineIrError(
                    f"{function.name} has no incoming slot $s{index}"
                )
            if descriptor.is_aggregate:
                assert descriptor.aggregate_size_bytes is not None
                self._write(
                    slot_addresses[index],
                    self._read(argument.bits, descriptor.aggregate_size_bytes),
                )
            else:
                expected_type = descriptor.name
                if argument.type_name != expected_type:
                    raise MachineIrError(
                        f"{function.name} argument {index} has type "
                        f"{argument.type_name}, expected {expected_type}"
                    )
                self._write_value(slot_addresses[index], argument)

        self.call_depth += 1
        try:
            return self._execute_frame(function, slot_addresses)
        finally:
            self.call_depth -= 1
            for region in frame_regions:
                region.is_alive = False

    def _execute_frame(
        self, function: Function, slot_addresses: dict[int, int]
    ) -> CallResult:
        values: dict[int, MachineValue] = {}
        block_id = 0
        while True:
            try:
                block = function.blocks[block_id]
            except KeyError as error:
                raise MachineIrError(
                    f"{function.name} branches to missing bb{block_id}"
                ) from error
            transferred = False
            for instruction in block.instructions:
                self.executed_instruction_count += 1
                self.executed_opcodes.add(instruction.opcode)
                if self.executed_instruction_count > self.instruction_budget:
                    raise MachineIrError("reference instruction budget exceeded")
                transfer = self._execute_instruction(
                    function, instruction, values, slot_addresses
                )
                if transfer is None:
                    continue
                kind, payload = transfer
                if kind == "jump":
                    assert isinstance(payload, int)
                    block_id = payload
                    transferred = True
                    break
                if kind == "return":
                    assert isinstance(payload, CallResult)
                    return payload
                raise MachineIrError(f"unknown reference transfer: {kind}")
            if not transferred:
                raise MachineIrError(
                    f"{function.name} bb{block_id} did not terminate"
                )

    def _define(
        self,
        function: Function,
        instruction: Instruction,
        values: dict[int, MachineValue],
        value: MachineValue,
    ) -> None:
        if instruction.result is None:
            raise MachineIrError(f"{instruction.opcode} has no result")
        expected_type = function.value_types[instruction.result]
        if value.type_name != expected_type:
            raise MachineIrError(
                f"{instruction.opcode} produced {value.type_name}, "
                f"expected {expected_type}"
            )
        values[instruction.result] = value

    def _execute_instruction(
        self,
        function: Function,
        instruction: Instruction,
        values: dict[int, MachineValue],
        slot_addresses: dict[int, int],
    ) -> tuple[str, int | CallResult] | None:
        opcode = instruction.opcode

        if opcode in {"const.integer", "const.bool", "const.null"}:
            if instruction.immediate is None:
                raise MachineIrError(f"{opcode} has no immediate")
            self._define(
                function,
                instruction,
                values,
                _integer_result(instruction.type_name, instruction.immediate),
            )
            return None

        if opcode == "const.float":
            if instruction.immediate is None:
                raise MachineIrError("const.float has no immediate")
            self._define(
                function,
                instruction,
                values,
                MachineValue(instruction.type_name, instruction.immediate),
            )
            return None

        if opcode == "load.slot":
            if instruction.slot is None:
                raise MachineIrError("load.slot has no slot")
            self._define(
                function,
                instruction,
                values,
                self._read_value(
                    slot_addresses[instruction.slot], instruction.type_name
                ),
            )
            return None

        if opcode == "store.slot":
            if instruction.slot is None or len(instruction.uses) != 1:
                raise MachineIrError("store.slot has an invalid operand")
            self._write_value(
                slot_addresses[instruction.slot],
                self._value(values, instruction.uses[0]),
            )
            return None

        if opcode == "address.slot":
            if instruction.slot is None:
                raise MachineIrError("address.slot has no slot")
            self._define(
                function,
                instruction,
                values,
                MachineValue("ptr64", slot_addresses[instruction.slot]),
            )
            return None

        if opcode == "address.member":
            base = self._unary_value(instruction, values)
            if instruction.immediate is None:
                raise MachineIrError("address.member has no immediate")
            self._define(
                function,
                instruction,
                values,
                MachineValue(
                    "ptr64",
                    (base.bits + instruction.immediate) & ((1 << 64) - 1),
                ),
            )
            return None

        if opcode == "address.global":
            if instruction.global_id is None:
                raise MachineIrError("address.global has no global")
            self._define(
                function,
                instruction,
                values,
                MachineValue("ptr64", self.global_addresses[instruction.global_id]),
            )
            return None

        if opcode == "zero.slot":
            if instruction.slot is None:
                raise MachineIrError("zero.slot has no slot")
            slot = function.slots[instruction.slot]
            self._write(slot_addresses[instruction.slot], bytes(slot.size_bytes))
            return None

        if opcode == "memory.copy":
            left, right = self._binary_values(instruction, values)
            if instruction.immediate is None:
                raise MachineIrError("memory.copy has no byte count")
            snapshot = self._read(right.bits, instruction.immediate)
            self._write(left.bits, snapshot)
            return None

        if opcode == "load.memory":
            pointer = self._unary_value(instruction, values)
            self._define(
                function,
                instruction,
                values,
                self._read_value(pointer.bits, instruction.type_name),
            )
            return None

        if opcode == "store.memory":
            pointer, value = self._binary_values(instruction, values)
            if (
                instruction.memory_type is None
                or value.type_name != instruction.memory_type
            ):
                raise MachineIrError("store.memory has an invalid memory type")
            self._write_value(pointer.bits, value)
            return None

        if opcode == "check.null":
            if self._unary_value(instruction, values).bits == 0:
                raise MachineTrap(signal.SIGILL, "null pointer check failed")
            return None

        if opcode == "check.bounds":
            index = self._unary_value(instruction, values)
            if (
                instruction.immediate is None
                or _unsigned(index) >= instruction.immediate
            ):
                raise MachineTrap(signal.SIGILL, "bounds check failed")
            return None

        if opcode == "address.index":
            base, index = self._binary_values(instruction, values)
            if instruction.immediate is None:
                raise MachineIrError("address.index has no element size")
            address = (
                base.bits + _unsigned(index) * instruction.immediate
            ) & ((1 << 64) - 1)
            self._define(
                function,
                instruction,
                values,
                MachineValue("ptr64", address),
            )
            return None

        if opcode in {"neg.integer", "not.integer"}:
            operand = self._unary_value(instruction, values)
            result = (
                -_unsigned(operand)
                if opcode == "neg.integer"
                else ~_unsigned(operand)
            )
            self._define(
                function,
                instruction,
                values,
                _integer_result(instruction.type_name, result),
            )
            return None

        if opcode == "not.bool":
            operand = self._unary_value(instruction, values)
            self._define(
                function,
                instruction,
                values,
                MachineValue("bool", int(_unsigned(operand) == 0)),
            )
            return None

        if opcode == "neg.float":
            operand = self._unary_value(instruction, values)
            self._define(
                function,
                instruction,
                values,
                _float_value(instruction.type_name, -_float_from_bits(operand)),
            )
            return None

        if opcode == "convert.integer":
            operand = self._unary_value(instruction, values)
            self._define(
                function,
                instruction,
                values,
                _integer_result(instruction.type_name, _integer_value(operand)),
            )
            return None

        if opcode == "convert.float":
            operand = self._unary_value(instruction, values)
            self._define(
                function,
                instruction,
                values,
                _float_value(instruction.type_name, _float_from_bits(operand)),
            )
            return None

        if opcode == "convert.integer_to_float":
            operand = self._unary_value(instruction, values)
            self._define(
                function,
                instruction,
                values,
                _float_value(
                    instruction.type_name, float(_integer_value(operand))
                ),
            )
            return None

        if opcode == "convert.float_to_integer":
            operand = self._unary_value(instruction, values)
            converted = _float_from_bits(operand)
            if not math.isfinite(converted):
                raise MachineTrap(
                    signal.SIGILL, "non-finite float-to-integer conversion"
                )
            truncated = math.trunc(converted)
            width = TYPE_WIDTH_BITS[instruction.type_name]
            if instruction.type_name in SIGNED_INTEGER_TYPES:
                in_range = -(1 << (width - 1)) <= converted < (
                    1 << (width - 1)
                )
            else:
                in_range = 0 <= converted < (1 << width)
            if not in_range:
                raise MachineTrap(
                    signal.SIGILL, "out-of-range float-to-integer conversion"
                )
            self._define(
                function,
                instruction,
                values,
                _integer_result(instruction.type_name, truncated),
            )
            return None

        if opcode in {
            "convert.pointer_to_integer",
            "convert.integer_to_pointer",
        }:
            operand = self._unary_value(instruction, values)
            self._define(
                function,
                instruction,
                values,
                MachineValue(instruction.type_name, operand.bits),
            )
            return None

        if opcode in {
            "add.integer",
            "sub.integer",
            "mul.integer",
            "div.integer",
            "rem.integer",
            "and.integer",
            "or.integer",
            "xor.integer",
            "shift_left.integer",
            "shift_right.integer",
        }:
            left, right = self._binary_values(instruction, values)
            result = self._integer_operation(opcode, left, right)
            self._define(function, instruction, values, result)
            return None

        if opcode in {
            "add.float",
            "sub.float",
            "mul.float",
            "div.float",
        }:
            left, right = self._binary_values(instruction, values)
            result = self._float_operation(opcode, left, right)
            self._define(function, instruction, values, result)
            return None

        if opcode.startswith("compare."):
            left, right = self._binary_values(instruction, values)
            result = self._compare(opcode, left, right)
            self._define(
                function,
                instruction,
                values,
                MachineValue("bool", int(result)),
            )
            return None

        if opcode == "call":
            if instruction.callee is None:
                raise MachineIrError("call has no callee")
            call_arguments = tuple(
                self._value(values, value_id) for value_id in instruction.uses
            )
            call_result = self._execute_function(
                instruction.callee, call_arguments
            )
            callee = self.module.functions[instruction.callee]
            if callee.return_type.is_aggregate:
                if (
                    call_result.aggregate is None
                    or instruction.result_slot is None
                ):
                    raise MachineIrError("aggregate call has no result storage")
                destination = slot_addresses[instruction.result_slot]
                self._write(destination, call_result.aggregate)
                self._define(
                    function,
                    instruction,
                    values,
                    MachineValue("ptr64", destination),
                )
            elif callee.return_type.name != "void":
                if call_result.value is None:
                    raise MachineIrError("scalar call has no result")
                self._define(
                    function, instruction, values, call_result.value
                )
            return None

        if opcode == "jump":
            if instruction.true_block is None:
                raise MachineIrError("jump has no destination")
            return "jump", instruction.true_block

        if opcode == "branch":
            condition = self._unary_value(instruction, values)
            destination = (
                instruction.true_block
                if _unsigned(condition) != 0
                else instruction.false_block
            )
            if destination is None:
                raise MachineIrError("branch has no destination")
            return "jump", destination

        if opcode == "return":
            if function.return_type.is_aggregate:
                pointer = self._unary_value(instruction, values)
                assert function.return_type.aggregate_size_bytes is not None
                aggregate = self._read(
                    pointer.bits,
                    function.return_type.aggregate_size_bytes,
                )
                return "return", CallResult(None, aggregate)
            if function.return_type.name == "void":
                return "return", CallResult(None, None)
            return "return", CallResult(
                self._unary_value(instruction, values), None
            )

        raise MachineIrError(f"unimplemented reference opcode: {opcode}")

    @staticmethod
    def _unary_value(
        instruction: Instruction, values: dict[int, MachineValue]
    ) -> MachineValue:
        if len(instruction.uses) != 1:
            raise MachineIrError(
                f"{instruction.opcode} expected one operand"
            )
        return ReferenceInterpreter._value(values, instruction.uses[0])

    @staticmethod
    def _binary_values(
        instruction: Instruction, values: dict[int, MachineValue]
    ) -> tuple[MachineValue, MachineValue]:
        if len(instruction.uses) != 2:
            raise MachineIrError(
                f"{instruction.opcode} expected two operands"
            )
        return (
            ReferenceInterpreter._value(values, instruction.uses[0]),
            ReferenceInterpreter._value(values, instruction.uses[1]),
        )

    @staticmethod
    def _integer_operation(
        opcode: str, left: MachineValue, right: MachineValue
    ) -> MachineValue:
        type_name = left.type_name
        width = TYPE_WIDTH_BITS[type_name]
        left_unsigned = _unsigned(left)
        right_unsigned = _unsigned(right)
        if opcode == "add.integer":
            result = left_unsigned + right_unsigned
        elif opcode == "sub.integer":
            result = left_unsigned - right_unsigned
        elif opcode == "mul.integer":
            result = left_unsigned * right_unsigned
        elif opcode in {"div.integer", "rem.integer"}:
            if right_unsigned == 0:
                raise MachineTrap(signal.SIGFPE, "integer division by zero")
            if type_name in SIGNED_INTEGER_TYPES:
                left_value = _signed(left)
                right_value = _signed(right)
                minimum = -(1 << (width - 1))
                if left_value == minimum and right_value == -1:
                    raise MachineTrap(
                        signal.SIGFPE, "signed integer division overflow"
                    )
                quotient = abs(left_value) // abs(right_value)
                if (left_value < 0) != (right_value < 0):
                    quotient = -quotient
                remainder = left_value - quotient * right_value
            else:
                quotient = left_unsigned // right_unsigned
                remainder = left_unsigned % right_unsigned
            result = quotient if opcode == "div.integer" else remainder
        elif opcode == "and.integer":
            result = left_unsigned & right_unsigned
        elif opcode == "or.integer":
            result = left_unsigned | right_unsigned
        elif opcode == "xor.integer":
            result = left_unsigned ^ right_unsigned
        elif opcode == "shift_left.integer":
            result = left_unsigned << (right_unsigned & (width - 1))
        elif opcode == "shift_right.integer":
            shift = right_unsigned & (width - 1)
            result = (
                _signed(left) >> shift
                if type_name in SIGNED_INTEGER_TYPES
                else left_unsigned >> shift
            )
        else:
            raise MachineIrError(f"unknown integer operation: {opcode}")
        return _integer_result(type_name, result)

    @staticmethod
    def _float_operation(
        opcode: str, left: MachineValue, right: MachineValue
    ) -> MachineValue:
        left_value = _float_from_bits(left)
        right_value = _float_from_bits(right)
        if opcode == "add.float":
            result = left_value + right_value
        elif opcode == "sub.float":
            result = left_value - right_value
        elif opcode == "mul.float":
            result = left_value * right_value
        elif opcode == "div.float":
            if right_value == 0.0:
                if left_value == 0.0 or math.isnan(left_value):
                    result = math.nan
                else:
                    sign_value = math.copysign(1.0, left_value) * math.copysign(
                        1.0, right_value
                    )
                    result = math.copysign(math.inf, sign_value)
            else:
                result = left_value / right_value
        else:
            raise MachineIrError(f"unknown float operation: {opcode}")
        return _float_value(left.type_name, result)

    @staticmethod
    def _compare(
        opcode: str, left: MachineValue, right: MachineValue
    ) -> bool:
        if left.type_name in FLOAT_TYPES:
            left_value: int | float = _float_from_bits(left)
            right_value: int | float = _float_from_bits(right)
        elif left.type_name in SIGNED_INTEGER_TYPES:
            left_value = _signed(left)
            right_value = _signed(right)
        else:
            left_value = _unsigned(left)
            right_value = _unsigned(right)

        if opcode == "compare.equal":
            return left_value == right_value
        if opcode == "compare.not_equal":
            return left_value != right_value
        if opcode in {"compare.less.integer", "compare.less.float"}:
            return left_value < right_value
        if opcode in {
            "compare.less_equal.integer",
            "compare.less_equal.float",
        }:
            return left_value <= right_value
        if opcode in {"compare.greater.integer", "compare.greater.float"}:
            return left_value > right_value
        if opcode in {
            "compare.greater_equal.integer",
            "compare.greater_equal.float",
        }:
            return left_value >= right_value
        raise MachineIrError(f"unknown comparison: {opcode}")


def exit_code(value: MachineValue) -> int:
    if value.type_name not in INTEGER_TYPES | {"bool"}:
        raise MachineIrError(f"cannot convert {value.type_name} to an exit code")
    return _unsigned(value) & 0xFF
