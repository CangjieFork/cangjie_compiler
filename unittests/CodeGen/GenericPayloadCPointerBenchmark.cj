// Microbenchmark input for generic CPointer<Int64>.read.
func readLoop<T>(ptr: CPointer<T>, count: Int64): T where T <: CType {
    var i: Int64 = 0
    var checksum: T = unsafe { ptr.read(0) }
    while (i < count) {
        checksum = unsafe { ptr.read(0) }
        i += 1
    }
    return checksum
}

main() {
    let ptr = unsafe { LibC.malloc<Int64>() }
    println(readLoop<Int64>(ptr, 10000000))
    unsafe { LibC.free(ptr) }
}
