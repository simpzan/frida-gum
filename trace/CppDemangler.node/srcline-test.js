const log = console.log.bind(console, "test.js");
const so = process.argv[2];
log(`so ${so}`);

const CppDemangler = require('./');
const srclineReader = new CppDemangler.SourceLineFinder(so);
const Addr2line = require('./Addr2line.js');
const addr2line = new Addr2line(so);


let { functions, base } = require('./libsf.json');
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