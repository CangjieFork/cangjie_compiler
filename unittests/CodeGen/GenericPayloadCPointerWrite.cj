// Regression input for generic CPointer.write code generation.
public func writeGeneric<T>(ptr: CPointer<T>, value: T): Unit where T <: CType {
    unsafe { ptr.write(0, value) }
}
