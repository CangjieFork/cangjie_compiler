// Regression input for the debug-only withoutTI wrapper path.
public struct RefPayload<T> {
    var value: T

    init(value: T) {
        this.value = value
    }

    mut func update(value: T): Unit {
        this.value = value
    }
}

public func callGeneric<T>(payload: RefPayload<T>, value: T): Unit {
    var local = payload
    local.update(value)
}
