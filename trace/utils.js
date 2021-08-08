const log = console.log.bind(console);
// const log = {};
log.e = console.error.bind(console, "E");
log.w = console.warn.bind(console, "W");
log.i = console.info.bind(console, "I");
log.d = console.debug.bind(console, "D");
log.v = console.log.bind(console, "V");
const noop = () => { };
// log.d = noop;
log.v = noop;
global.log = log;

function delay(seconds) {
    return new Promise((resolve) => {
        setTimeout(resolve, seconds);
    });
}

class StdIn {
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

function testInteractively() {
    const names = getThreadNames(709, [709, 751, 856, 1862]);
    log(names);
}
if (require.main === module) {
    console.log('called directly');
    testInteractively();
}

