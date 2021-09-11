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
const libuiFile = '~/blueline/aosp_9_r12/out/target/product/generic_arm64/symbols/system/lib/libui.so';
const libui = new addon.ELFFile(libuiFile);
log(libui.plusOne());