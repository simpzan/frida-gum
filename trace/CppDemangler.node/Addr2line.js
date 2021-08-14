
const { runCmd } = require("../utils.js");

function parseFunctionInfo(str) {
    const lines = str.split('\n');
    if (lines.length < 3) return null;
    if (lines.length > 3) log.w("warning, str is " + str);
    const name = lines[0];
    const parts = lines[1].split(':');
    const fn = { name, source: parts[0], line: parts[1] };
    return fn;
}
class Addr2line {
    constructor(filename) {
        const { spawn } = require('child_process');
        const p = spawn("stdbuf",       // the stdbuf command
            [   '-i0', '-o0', '-e0',    // disable all buffering
                'addr2line', '-e', filename, '-fC' ]);
        p.on('error', err => log.e("process error", err));
        p.stderr.on('data', data => log.e("stderr", data.toString()));
        this.p = p;

        let lastStdoutString = "";
        this.p.stdout.on('data', data => {
            // log.d(`output "${data.toString()}"`);
            lastStdoutString += data.toString();
            const fn = parseFunctionInfo(lastStdoutString);
            if (!fn) return;

            this.resolve(fn);
            this.resolve = null;
            lastStdoutString = "";
        });
        this.vaddr = 0;
        log.d(`vaddr ${this.vaddr}`);
    }
    async getName(addr) {
        return new Promise((resolve, reject) => {
            if (this.resolve) return reject("last request are still processing.");
            this.resolve = resolve;
            addr += this.vaddr;
            const input = addr.toString(16) + "\n";
            // log.d(`input "${input}"`);
            this.p.stdin.write(input);
        });
    }
    exit() {
        this.p.kill();
    }
}
async function testInteractively() {
    const binaryFile = './build/small-test';
    const addr2line = new Addr2line(binaryFile);
    const addrs = [ 0x55b27c5adab6, 0x55b27c5ad9a6, 0x55b27c5ad8f9 ].map(e => e - 0x55b27c5ab000);
    for (let addr of addrs) {
        const fn = await addr2line.getName(addr);
        log.i("fn", fn, "\n");
    }
    addr2line.exit();
}

if (require.main === module) {
    console.log('called directly');
    testInteractively();
} else {
    module.exports = Addr2line;
}


function parseOutput(output) {
    const lines = output.split('\n');
    const names = [];
    for (let i = 0; i < lines.length; i += 2) {
        const name = lines[i];
        const parts = lines[i+1].split(':');
        const fn = { name, source: parts[0], line: parts[1] };
        names.push(fn);
    }
    return names;
}
function main() {
    const addrs = [ 0x41315, 0x4A065, 0x3DA81 ];
    const input = addrs.map(a => a.toString(16)).join('\n');
    log.i("input", input);
    const cmd = `addr2line -e /workspace/g2/out/target/product/ghost/symbols/system/lib/libmediaplayerservice.so -f`;
    const output = runCmd(cmd, input);
    if (!output) return;
    log.i("output", output);
    const names = parseOutput(output);
    log.i("names", names);
}
// main();