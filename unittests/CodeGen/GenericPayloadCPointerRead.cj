// Regression input for generic CPointer.read code generation.
public func readGeneric<T>(ptr: CPointer<T>): T where T <: CType {
    unsafe { ptr.read() }
}
