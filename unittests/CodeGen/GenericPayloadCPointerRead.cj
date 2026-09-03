// Regression input for generic CPointer.read code generation.
public func readGeneric<T>(ptr: CPointer<T>): T where T <: CType {
    unsafe { ptr.read() }
}

main() {
    let ptr = unsafe { LibC.malloc<Int64>() }
    let value = readGeneric<Int64>(ptr)
    println(value)
    unsafe { LibC.free(ptr) }
}
