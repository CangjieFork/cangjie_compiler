// Reproducer for the erased/generic ($withoutTI) wrapping else branch.
// This deliberately keeps T unconstrained so the compiler cannot see a
// reference field in the erased LLVM representation; the resulting IR is
// inspected even when backend verification rejects the current address space.
public struct GenericPayload<T> {
    var value: T

    init(value: T) {
        this.value = value
    }

    mut func update(value: T): Unit {
        this.value = value
    }
}

public func callGeneric<T>(payload: GenericPayload<T>, value: T): Unit {
    var local = payload
    local.update(value)
}

main() {
    var payload = GenericPayload<Int64>(1)
    callGeneric<Int64>(payload, 2)
}
