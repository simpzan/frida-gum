var addon = require('bindings')('addon.node')
function demangle(name) {
    return addon.demangle(name);
}

class CppDemangler {
    async demangle(mangledName) {
        return demangle(mangledName);
    }
    exit() {}
}
module.exports = CppDemangler;
module.exports.demangle = demangle;

class SourceLineFinder {
    constructor(libName) {
        addon.startDwarf(libName);
    }
    srcline(addr) {
        const addrString = '0x' + addr.toString(16);
        return addon.srcline(addrString);
    }
    getVirtualAddress() {
        return addon.getVirtualAddress();
    }
    getBuidId() {
        return addon.getBuidId();
    }
}
module.exports.SourceLineFinder = SourceLineFinder;

function testInteractively() {
    const input = '_ZZZN7android4Hwc24impl8Composer7executeEvENK4$_15clINS_8hardware8graphics8composer4V2_15ErrorEbjNS5_8hidl_vecINS5_11hidl_handleEEEEEDaRKT_RKT0_RKT1_RKT2_ENKUlSG_SJ_E_clIS9_NS5_12MQDescriptorIjLNS5_8MQFlavorE1EEEEESD_SG_SJ_'
    const output = demangle(input);
    console.log(input);
    console.log(output);

    const input2 = '__on_dlclose';
    const output2 = demangle(input2);
    console.log(input2, output2);
}
function test() {
    console.log(process.argv)
    const soFile = process.argv[2]
    const reader = new SourceLineFinder(soFile);
    const vaddr = reader.getVirtualAddress();
    const buildid = reader.getBuidId();
    console.log(`vaddr ${vaddr.toString(16)}, buildid ${buildid}`);
    const inputs = process.argv.slice(3);
    for (const input of inputs) {
        const output = reader.srcline(input);
        console.log(`${input} -> ${output}`);
    }
}
if (require.main === module) {
    console.log('called directly');
    // testInteractively();
    test();
}
