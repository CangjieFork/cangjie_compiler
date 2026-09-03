// Not a benchmark: Int64 specialization removes the generic payload helper.
func readLoop<T>(ptr: CPointer<T>, count: Int64, fold: (Int64, T) -> Int64): Int64 where T <: CType {
    var i: Int64 = 0
    var checksum: Int64 = 0
    while (i < count) {
        checksum = fold(checksum, unsafe { ptr.read(i) })
        i += 1
    }
    return checksum
}

main() {
    let count: Int64 = 10000000
    let ptr = unsafe { LibC.malloc<Int64>(count: count) }
    var i: Int64 = 0
    while (i < count) {
        unsafe { ptr.write(i, i) }
        i += 1
    }
    println(readLoop<Int64>(ptr, count, {sum, value => sum + value}))
    unsafe { LibC.free(ptr) }
}
