const log = console.log.bind(console, "test.js");
const so = process.argv[2];
log(`so ${so}`);

const CppDemangler = require('./');
const srclineReader = new CppDemangler.SourceLineFinder(so);
const Addr2line = require('./Addr2line.js');
const addr2line = new Addr2line(so);


let { functions, base } = require('./libsf.json');
function filterMergedFunctions() {
    const map = new Map();
    for (const fn of functions) {
        const key = fn.address;
        const existing = map.get(key) || [];
        existing.push(fn);
        map.set(key, existing);
    }

    let i = 0;
    const uniqueFunctions = [];
    for (const [key, value] of map) {
        if (value.length > 1) {
            ++i;
            log(i, value.length, value);
        }
        else uniqueFunctions.push(value[0]);
    }
    const functionNumTotal = functions.length;
    functions = uniqueFunctions;
    const uniqueFunctionNum = uniqueFunctions.length
    log(`unique ${uniqueFunctionNum}, total ${functionNumTotal}`);
}
filterMergedFunctions();

let linenum = 667;
// functions = functions.slice(linenum-1, linenum);
log(`functions ${functions.length}`);

async function main() {
    let i = 0;
    const vaddr = 0x000000000001d000;
    for (const fn of functions) {
        ++i;
        const addr = parseInt(fn.address, 16) - base + vaddr;
        // log(fn, addr.toString(16));
        const symbol = await addr2line.getName(addr);
        const expected = symbol.source;
        const actual = srclineReader.srcline(addr);
        if (!actual.startsWith(expected) && !actual.endsWith(expected)) {
            log(`${i} ${addr.toString(16)} ${JSON.stringify(fn)}, expected: ${JSON.stringify(symbol)}, actual: ${JSON.stringify(actual)}`);
            break;
        }
    }
    process.exit();
    
}
main();