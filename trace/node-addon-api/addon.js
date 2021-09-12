const addon = require('bindings')('addon');
const log = console.log.bind(console);

function showBinaryInfo(file) {
    let lib = new addon.ELFWrap(file);
    const info = lib.info();
    log(info, info.vaddr.toString(16));
    const functions = lib.functions();
    log(functions.length);
    const debugInfo = new addon.DebugInfoWrap(lib);
    functions.slice(0, 5).forEach((fn, index) => {
        log(index, fn, addon.demangleCppName(fn.name), debugInfo.srcline(fn.address));
    });
    debugInfo.release();
    lib = null;
}

const libuiFile = '/home/simpzan/blueline/aosp_9_r12/out/target/product/generic_arm64/symbols/system/lib/libui.so';
showBinaryInfo(libuiFile);

// const libsfFile = '/home/simpzan/frida/libsf.so';
// showBinaryInfo(libsfFile);

// showBinaryInfo(libuiFile);

setTimeout(() => {

}, 3000);
