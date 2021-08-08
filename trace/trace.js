const fs = require('fs');
const utils = require('./utils.js');
// const CppDemangler = require('./CppDemangler.js');
const CppDemangler = require('./CppDemangler.node');
log("cwd", process.cwd())
const frida = require('../../build/frida_thin-linux-x86_64/lib/node_modules/frida');

let events = [];
function onMessageFromDebuggee(msg, bytes) {
    const payload = msg.payload;
    if (!payload) return log.e(...msg);
    if (payload.type === 'events') {
        const { pid, tid } = msg.payload;
        for (let i = 0; i < bytes.length; ) {
            const addr = '0x' + bytes.readBigUInt64LE(i).toString(16); i += 8;
            let ts = bytes.readBigInt64LE(i); i += 8;
            const ph = ts > 0 ? 'B' : 'E';
            ts = ts > 0 ? ts : -ts;
            ts = ts.toString(10);
            const event = { ph, tid, pid, addr, ts };
            events.push(event);
            log.i(event);
        }
    } else {
        log.e(`unkown msg`, ...arguments);
    }
}

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
function writeChromeTracingFile(filename, functionMap) {
    log.i(`writing chrome tracing file ${filename}`);
    const traceFile = new ChromeTracingFile(filename);
    const tids = new Set();
    for (const trace of events) {
        tids.add(trace.tid);
        const fn = functionMap.get(trace.addr);
        trace.name = fn.demangledName || fn.name;
        traceFile.writeObject(trace);
    }
    const pid = events[0].pid;
    const threadNames = utils.getThreadNames(pid, Array.from(tids));
    for (const [tid, threadName] of threadNames) {
        const name = `${threadName}/${tid}`;
        const entry = {"ts":0, "ph":"M", "name":"thread_name", pid, tid, "args":{name}};
        traceFile.writeObject(entry);
    }
    traceFile.close();
}
async function attachProcess(processName, sourceFilename) {
    // const device = await frida.getUsbDevice();
    // if (!device) return log.e('no usb device found.');

    log.i(`tracing process ${processName}`);
    const session = await frida.attach(processName);
    const source = fs.readFileSync(sourceFilename, "utf8");
    const script = await session.createScript(source, { runtime: 'v8' });
    script.message.connect(onMessageFromDebuggee);
    script.destroyed.connect(() => script.unloaded = true);
    await script.load();
    return script;
}

function isRunning(program) {
    const out = utils.runCmd(`pidof ${program}`);
    return out && out.length;
}

async function getFunctionsToTrace(rpc, libName) {
    const module = await rpc.getModuleByName(libName);
    const baseAddr = parseInt(module.base, 16);
    const srclineReader = new CppDemangler.SourceLineFinder(module.path);
    const demangler = new CppDemangler();
    let functions = await rpc.getFunctionsOfModule(libName);

    const functionsToTrace = new Map()
    for (const fn of functions) {
        const addr = parseInt(fn.address, 16) - baseAddr;
        const addr2 = '0x' + addr.toString(16);
        const srcline = srclineReader.srcline(addr2);
        if (!srcline || srcline.includes('/include/c++/')) continue;
        fn.demangledName = await demangler.demangle(fn.name);
        // log.d(addr2, (srcline), '\t\t', fn.demangledName);

        functionsToTrace.set(fn.address, fn);
    }
    demangler.exit();

    return functionsToTrace;
}

async function main() {
    const argv = process.argv;
    log.i("argv", argv);

    const processName = argv[2] || "main";
    const libName = argv[3] || "libtest.so";
    const sourceFilename = "./test.js";

    process.env['LD_LIBRARY_PATH'] = '/home/simpzan/frida/cpp-example';
    const running = isRunning(processName);
    let pid = 0;
    if (!running) pid = await frida.spawn(['/home/simpzan/frida/cpp-example/main']);

    const script = await attachProcess(processName, sourceFilename);

    const functionsToTrace = await getFunctionsToTrace(script.exports, libName);
    await script.exports.startTracing([...functionsToTrace.keys()]);

    if (pid) await frida.resume(pid);

    log.i('Tracing started, press enter to stop.');

    const stdin = new utils.StdIn();
    await stdin.getline();
    stdin.destroy();

    if (!script.unloaded) {
        await script.exports.stopTracing();
        script.unload();
    }

    if (!events.length) return log.i('no trace data.');

    writeChromeTracingFile(`${libName}.json`, functionsToTrace);
    log.i('tracing done!');
};
main();
