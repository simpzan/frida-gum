var addon = require('bindings')('addon');

var obj = new addon.MyObject(10);
console.log( obj.plusOne() ); // 11
console.log( obj.plusOne() ); // 12
console.log( obj.plusOne() ); // 13

console.log( obj.multiply().value() ); // 13
console.log( obj.multiply(10).value() ); // 130

var newobj = obj.multiply(-1);
console.log( newobj.value() ); // -13
console.log( obj === newobj ); // false

const log = console.log.bind(console);
function showBinaryInfo(file) {
    const lib = new addon.ELFFile(file);
    const info = lib.info();
    log(info, info.vaddr.toString(16));
}

const libuiFile = '/home/simpzan/blueline/aosp_9_r12/out/target/product/generic_arm64/symbols/system/lib/libui.so';
showBinaryInfo(libuiFile);

const libsfFile = '/home/simpzan/frida/libsf.so';
showBinaryInfo(libsfFile);
