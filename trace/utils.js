function log(level, ...args) {
    let e = new Error();
    let frame = e.stack.split("\n")[2]; // change to 3 for grandparent func
    let lineNumber = frame.split(":").reverse()[1];
    let functionName = frame.split(" ")[5];
    const time = Date.now() / 1000;
    const srcline = `${time.toFixed(3)} ${level} ${process.pid} ${functionName}:${lineNumber}\t`;
    console.log(srcline, ...args);
}
const noop = () => {};
log.e = log.bind(null, "E");
log.w = log.bind(null, "W");
log.i = log.bind(null, "I");
log.d = log.bind(null, "D");
log.v = log.bind(null, "V");
// log.d = noop;
log.v = noop;
global.log = log;

function delay(seconds) {
    return new Promise((resolve) => {
        setTimeout(resolve, seconds);
    });
}

class StdIn {
    static async readline() {
        const stdin = new StdIn();
        await stdin.getline();
        stdin.destroy();
    }
    constructor() {
        var readline = require('readline');
        this.rl = readline.createInterface({
            input: process.stdin,
            output: process.stdout,
            terminal: false
        });
    }
    getline = () => new Promise((resolve, reject) => {
        this.rl.once('line', resolve);
    });
    destroy() {
        this.rl.close();
    }
}

const fs = require('fs');
function saveObject(obj, filename) {
    const str = JSON.stringify(obj);
    fs.writeFileSync(filename, str);
}
function loadObject(filename) {
    try {
        const str = fs.readFileSync(filename, 'utf8');
        return JSON.parse(str);
    } catch (error) {
        return null;
    }
}

function runCmd(cmd) {
    try {
        console.info(`cmd: ${cmd}`);
        const cp = require("child_process");
        const output = cp.execSync(cmd);
        const result = output.toString("utf8");
        console.info(`result:\n${result}`);
        return result.trim();
    } catch (err) {
        return null;
    }
}
function makeMap(keys, values) {
    const klength = keys.length, vlength = values.length;
    if (klength !== vlength) throw new Error(`length not equal: ${klength} != ${vlength}`);
    const map = new Map();
    for (let i = 0; i < klength; ++i) map.set(keys[i], values[i]);
    return map;
}
function getThreadNames(pid, tids) {
    const tidFiles = tids.map(tid => `${tid}/comm`).join(' ');
    const cmd = `adb shell "cd /proc/${pid}/task/ && cat ${tidFiles}"`;
    const out = runCmd(cmd);
    if (!out) return new Map();
    const names = out.split('\n');
    return makeMap(tids, names);
}
module.exports = { delay, StdIn, saveObject, loadObject, getThreadNames, makeMap, runCmd };

function partitionArray(array, predicate) {
    const ret = { true: [], false: [] };
    for (const e of array) {
        const ok = !!predicate(e);
        ret[ok].push(e);
    }
    return ret;
}
module.exports.partitionArray = partitionArray;

class ChromeTracingFile {
    constructor(filename) {
        this.sink = fs.createWriteStream(filename);
        this.sink.write("[\n");
    }
    writeObject(obj) {
        this.sink.write(JSON.stringify(obj));
        this.sink.write(",\n");
    }
    close() {
        this.sink.write("{}]\n");
        this.sink.end();
    }
}
module.exports.ChromeTracingFile = ChromeTracingFile;

function testInteractively() {
    const names = getThreadNames(709, [709, 751, 856, 1862]);
    log(names);
}
if (require.main === module) {
    console.log('called directly');
    testInteractively();
}

