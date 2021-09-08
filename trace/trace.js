const fs = require('fs');
const utils = require('./utils.js');
// const CppDemangler = require('./CppDemangler.js');
const CppDemangler = require('./CppDemangler.node');
log("cwd", process.cwd())
const fridaMac = '../../build/frida-macos-x86_64/lib/node_modules/frida';
const fridaLinux = '../../build/frida_thin-linux-x86_64/lib/node_modules/frida';
const frida = require(fridaMac);

let threadNames = new Map();
let events = [];
function onMessageFromDebuggee(msg, bytes) {
    const payload = msg.payload;
    if (!payload) return log.e(...arguments);
    if (payload.type === 'events') {
        const { pid, tid } = msg.payload;
        if (tid < 0) {
            const name = bytes.toString('utf8').trim();
            const id = -tid;
            threadNames.set(id, name);
            return log.i(`thread ${id} ${name}`);
        }
        for (let i = 0; i < bytes.length; ) {
            const addr = Number(bytes.readUInt16LE(i)); i += 2;
            let ts = Number(bytes.readInt32LE(i)); i += 4;
            const ph = ts > 0 ? 'B' : 'E';
            ts = ts > 0 ? ts : -ts;
            const event = { ph, tid, pid, addr, ts: ts };
            events.push(event);
            // log.i(event);
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
    for (const trace of events) {
        const fn = functionMap[trace.addr]
        if (!fn) return log.e(`can't find function info for event: ${trace}`);
        trace.name = fn.demangledName || fn.name;
        traceFile.writeObject(trace);
    }
    const pid = events[0].pid;
    for (const [tid, threadName] of threadNames) {
        const name = `${threadName}/${tid}`;
        const entry = {"ts":0, "ph":"M", "name":"thread_name", pid, tid, "args":{name}};
        traceFile.writeObject(entry);
    }
    traceFile.close();
}
let targetDevice = null;
function isAndroid() { return targetDevice && targetDevice.type === 'usb'; }
function getBinaryLocalPath(module) {
    if (isAndroid()) return `/tmp/${module.name}`;
    return module.path;
}
async function attachProcess(deviceId, processName, sourceFilename) {
    const device = await frida.getDevice(deviceId);
    if (!device) return log.e(`device '${deviceId}' not found.`);

    targetDevice = device;
    log.i(`tracing process '${processName}' of device '${device.name}'`);
    const session = await device.attach(processName);
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

async function addImportedFunctions(rpc, libName, modules, functions) {
    const imported = await rpc.getImportedFunctions(libName);
    const symbolsByModule = new Map();
    for (const symbol of imported) {
        const symbols = symbolsByModule.get(symbol.module) || [];
        symbols.push(symbol);
        symbolsByModule.set(symbol.module, symbols);
    }
    for (const [module, symbols] of symbolsByModule) {
        log.v(`imported ${symbols.length} symbols from ${module}`);
        if (!modules.includes(module)) continue;

        for (const symbol of symbols) {
            if (symbol.type != 'function') continue;
            const addr = parseInt(symbol.address, 16);
            symbol.addr = addr;
            functions.push(symbol);
        }
        log.i(`added ${symbols.length} symbols from ${module}`);
    }
}

function validateFunctions(functionsLocal, functionsRemote) {
    const remoteFunctions = new Map();
    for (const fn of functionsRemote) {
        const addr = parseInt(fn.address, 16);
        remoteFunctions.set(addr, fn);
    }
    for (const fn of functionsLocal) {
        const remote = remoteFunctions.get(fn.addr);
        if (!remote) {
            log.e('invalid function', fn);
            return false;
        }
        if (remote.size != fn.size) log.e('invalid function size', fn, remote);
    }
    return true;
}
async function getFunctionsToTrace(rpc, libName, srclinePrefix) {
    const module = await rpc.getModuleByName(libName);
    log.i(module);
    const baseAddr = parseInt(module.base, 16);
    const srclineReader = new CppDemangler.SourceLineFinder(getBinaryLocalPath(module));
    const vaddr = isAndroid() ? srclineReader.getVirtualAddress() : 0;
    const buildIdLocal = srclineReader.getBuidId();
    const buildIdRemote = await rpc.getBuidId(module.path);
    if (buildIdLocal != buildIdRemote) {
        return log.e(`build id mismatch ${buildIdLocal} ${buildIdRemote}`);
    }
    let functions = srclineReader.getFunctions();
    const functionsToTrace = [];
    for (const fn of functions) {
        if (fn.name.includes('~')) continue;
        const info = srclineReader.srcline(fn.addr);
        if (srclinePrefix && !info.file.startsWith(srclinePrefix)) continue;
        fn.file = info.file;
        fn.line = info.line;
        const addr = fn.addr - vaddr + baseAddr;
        fn.addr = addr;
        functionsToTrace.push(fn);
    }
    const modules = ['/system/lib64/libGLESv2.so', '/system/lib64/libEGL.so'];
    await addImportedFunctions(rpc, libName, modules, functionsToTrace);
    if (functionsToTrace.length > Math.pow(2, 16)) {
        log.e(`too many functions for uint16_t, ${functionsToTrace.length}`);
    }
    const remoteFunctions = await rpc.getFunctionsOfModule(libName);
    validateFunctions(functionsToTrace, remoteFunctions);
    return functionsToTrace;
}

async function main() {
    const argv = process.argv;
    log.i("argv", argv);

    const deviceId = argv[2];
    const processName = argv[3] || "main";
    const libName = argv[4] || "libtest.so";
    const srclinePrefix = argv[5];
    const sourceFilename = "./test.js";

    process.env['LD_LIBRARY_PATH'] = '/home/simpzan/frida/cpp-example';
    const running = isRunning(processName);
    let pid = 0;
    if (!running) pid = await frida.spawn(['/home/simpzan/frida/cpp-example/main']);

    const script = await attachProcess(deviceId, processName, sourceFilename);

    const functionsToTrace = await getFunctionsToTrace(script.exports, libName, srclinePrefix);
    await script.exports.startTracing(functionsToTrace);

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
