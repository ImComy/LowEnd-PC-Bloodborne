"""Independent D015 fixture: the documented 393-byte allocator function.

Source: TASK-001 character-fault-static.json, function 0x26addc0. The function's bytes
are read from the player's own eboot.bin (BB_CPU_GAME_DIR) and checked against
FUNCTION_SHA256; the repository keeps only lengths and hashes. They are data for
signatures, never executed as original count instructions on this host.
"""
from cpu_harness import CF, ZF, SF, OF, ARITHMETIC_FLAGS

SOURCE_SHA256 = 'd65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9'
FUNCTION_SHA256 = '16729bb23b2f814ef311ef0a3f3aac2d91f16110a206885eae694f2b17f38133'
FUNCTION_START, FUNCTION_END = 0x26addc0, 0x26adf49

# (address, mnemonic, operands, source64, destination64, tail destination64,
#  immediate, span length, tail length, length of the SUB that follows (context))
SPANS = (
    (0x26adde5, 'lzcnt', 'eax, eax', 'rax', 'rax', 'rcx', 32, 9, 5, 2),
    (0x26addfe, 'tzcnt', 'ecx, eax', 'rax', 'rcx', None, 16, 7, 3, 0),
    (0x26adebe, 'lzcnt', 'edi, edx', 'rdx', 'rdi', 'rdx', 31, 9, 5, 2),
    (0x26adf12, 'lzcnt', 'esi, ecx', 'rcx', 'rsi', 'rcx', 31, 9, 5, 2),
)


def span_bytes(function, span):
    """(original span, its tail, the SUB context after it), cut from the function's bytes."""
    offset = span[0] - FUNCTION_START
    original = function[offset:offset + span[7]]
    return original, original[span[7] - span[8]:], function[offset + span[7]:offset + span[7] + span[9]]


def subtract32(left, right):
    """Independent SUB/CMP flags, including parity and half-carry."""
    left, right = left & 0xffffffff, right & 0xffffffff
    result = (left - right) & 0xffffffff
    flags = (CF if left < right else 0) | (ZF if result == 0 else 0)
    flags |= SF if result & 0x80000000 else 0
    flags |= OF if (left ^ right) & (left ^ result) & 0x80000000 else 0
    flags |= 16 if (left ^ right ^ result) & 16 else 0
    flags |= 4 if (result & 255).bit_count() % 2 == 0 else 0
    return result, flags


def expected_state(span, registers, guest_rsp, *, include_context=False):
    """Return every GPR, defined flags and mask after the complete sequence."""
    _, mnemonic, _, source, destination, tail_destination, immediate, *_ = span
    expected = dict(registers, rsp=guest_rsp)
    value = registers[source] & 0xffffffff
    if mnemonic == 'lzcnt':
        count = 32 - value.bit_length()
    else:
        count = (value & -value).bit_length() - 1 if value else 32
    expected[destination] = count
    flags, mask = (CF if value == 0 else 0) | (ZF if count == 0 else 0), CF | ZF
    if tail_destination is None:
        _, flags = subtract32(count, immediate)
        mask = ARITHMETIC_FLAGS
    else:
        expected[tail_destination] = immediate
        if include_context:
            expected[tail_destination], flags = subtract32(immediate, count)
            mask = ARITHMETIC_FLAGS
    return expected, flags, mask
