// Microbenchmark input for generic CPointer<Int64>.read.
func readLoop<T>(ptr: CPointer<T>, count: Int64): Unit where T <: CType {
    var i: Int64 = 0
    while (i < count) {
        let _ = unsafe { ptr.read(i) }
        i += 1
    }
}

main() {
    let ptr = unsafe { LibC.malloc<Int64>() }
    readLoop<Int64>(ptr, 10000000)
    unsafe { LibC.free(ptr) }
}
