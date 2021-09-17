const fs = require('fs');
const utils = require('./utils.js');
const fridaMac = '../../build/frida-macos-x86_64/lib/node_modules/frida';
const fridaLinux = '../../build/frida_thin-linux-x86_64/lib/node_modules/frida';
const fridaPath = process.platform == 'darwin' ? fridaMac : fridaLinux;
const frida = require(fridaPath);
const addon = require('./node-addon-api');

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

function writeChromeTracingFile(filename, functionMap) {
    log.i(`writing chrome tracing file ${filename}`);
    const traceFile = new utils.ChromeTracingFile(filename);
    for (const trace of events) {
        const fn = functionMap[trace.addr]
        if (!fn) return log.e(`can't find function info for event`, trace);
        trace.name = fn.demangledName = fn.demangledName || addon.demangleCppName(fn.name);
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
        remoteFunctions.set(fn.name, fn);
    }
    for (const fn of functionsLocal) {
        const remote = remoteFunctions.get(fn.addr);
        if (!remote) {
            log.e('invalid function', fn, remoteFunctions.get(fn.name));
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
    const modulePath = getBinaryLocalPath(module);
    let lib = new addon.ELFWrap(modulePath);
    const localModuleInfo = lib.info();
    log(localModuleInfo);
    const vaddr = isAndroid() ? localModuleInfo.vaddr : 0;
    const buildIdLocal = localModuleInfo.buildid;
    const buildIdRemote = await rpc.getBuidId(module.path);
    if (buildIdLocal != buildIdRemote) {
        return log.e(`build id mismatch ${buildIdLocal} ${buildIdRemote}`);
    }
    const functions = lib.functions().filter(fn => fn.address && fn.size);
    const debugInfo = new addon.DebugInfoWrap(lib);
    const functionsToTrace = [];
    for (const fn of functions) {
        if (fn.name.includes('~')) continue;
        const info = debugInfo.srcline(fn.address);
        if (!info.src) {
            log.w('no debug info', fn);
            continue;
        }
        info.file = info.src;
        if (srclinePrefix && !info.file.startsWith(srclinePrefix)) continue;
        fn.file = info.file;
        fn.line = info.line;
        const addr = fn.address - vaddr + baseAddr;
        fn.addr = addr;
        functionsToTrace.push(fn);
    }
    debugInfo.release();
    lib.release();
    const modules = ['/system/lib64/libGLESv2.so', '/system/lib64/libEGL.so'];
    await addImportedFunctions(rpc, libName, modules, functionsToTrace);
    if (functionsToTrace.length > Math.pow(2, 16)) {
        log.e(`too many functions for uint16_t, ${functionsToTrace.length}`);
    }
    const remoteFunctions = await rpc.getFunctionsOfModule(libName);
    validateFunctions(functionsToTrace, remoteFunctions);
    return functionsToTrace;
}

class Process {
    static async getOrSpawn(deviceId, name, path = null) {
        const device = await frida.getDevice(deviceId);
        if (!device) throw new Error(`device '${deviceId}' not found.`);

        const processes = await device.enumerateProcesses();
        const process = processes.find(p => p.name == name);
        if (process.pid) return new Process(device, process.pid, name);

        const pid = await device.spawn(path || name);
        if (pid) return new Process(device, pid, name, true);

        throw new Error(`spawn ${name} failed on device ${deviceId}`);
    }
    constructor(device, pid, name, spawned = false) {
        this.device = device;
        this.pid = pid;
        this.name = name;
        this.spawned = spawned;
    }
    async attach(sourceFilename) {
        const session = this.session = await this.device.attach(this.pid);
        const source = fs.readFileSync(sourceFilename, "utf8");
        const script = await session.createScript(source, { runtime: 'v8' });
        await script.load();
        return new Script(script);
    }
    async resume() {
        if (!this.spawned) return;
        await frida.resume(this.pid);
    }
};

class Script {
    constructor(script) {
        this.script = script;
        script.message.connect(onMessageFromDebuggee);
        script.destroyed.connect(() => this.unloaded = true);
    }
    async startTracing(functionsToTrace) {
        await this.script.exports.startTracing(functionsToTrace);
    }
    async stopTracing() {
        if (this.unloaded) return;
        await this.script.exports.stopTracing();
        await this.script.unload();
    }
    async getModuleByName(libName) {
        return await this.script.exports.getModuleByName(libName);
    }
    async getBuidId(path) { return await this.script.exports.getBuidId(path); }
    async getFunctionsOfModule(libName) {
        return await this.script.exports.getFunctionsOfModule(libName);
    }
    async getImportedFunctions(libName) {
        return await this.script.exports.getImportedFunctions(libName);
    }
};

async function main() {
    const argv = process.argv;
    log.i("argv", argv);

    const deviceId = argv[2];
    const processName = argv[3] || "main";
    const libName = argv[4] || "libtest.so";
    const srclinePrefix = argv[5];
    const sourceFilename = "./test.js";

    const targetProcess = await Process.getOrSpawn(deviceId, processName);
    const script = await targetProcess.attach(sourceFilename);

    let functionsToTrace = [];
    for (const lib of libName.split(',')) {
        const fns = await getFunctionsToTrace(script, lib, srclinePrefix);
        if (!fns) {
            log.w('no functions to trace', lib);
            continue;
        }
        log.i(`collected ${fns.length} functions from ${lib}`);
        functionsToTrace = fns.concat(functionsToTrace);
    }
    await script.startTracing(functionsToTrace);

    await targetProcess.resume();

    log.i('Tracing started, press enter to stop.');

    const stdin = new utils.StdIn();
    await stdin.getline();
    stdin.destroy();

    await script.stopTracing();

    if (!events.length) return log.i('no trace data.');

    writeChromeTracingFile(`${libName}.json`, functionsToTrace);
    log.i('tracing done!');
};
main();
