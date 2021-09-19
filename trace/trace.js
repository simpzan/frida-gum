const fs = require('fs');
const utils = require('./utils.js');
const fridaDir = process.platform == 'darwin' ? 'frida-macos-x86_64' : 'frida_thin-linux-x86_64';
const frida = require(`../../build/${fridaDir}/lib/node_modules/frida`);
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

function validateFunctions(functionsLocal, functionsRemote) {
    const remoteFunctions = new Map();
    for (const fn of functionsRemote) {
        const addr = fn.addr = parseInt(fn.address, 16);
        remoteFunctions.set(addr, fn);
        remoteFunctions.set(fn.name, fn);
    }
    for (const fn of functionsLocal) {
        let remote = remoteFunctions.get(fn.addr);
        if (!remote || remote.size != fn.size) {
            remote = remote || remoteFunctions.get(fn.name);
            const diff = remote.addr - fn.addr;
            log.e('invalid function', fn, remote, diff);
            throw new Error(`invalid function ${fn.name}`);
        }
    }
}

let sysroot = '';
class Module {
    static create(remoteModuleInfo) {
        log.i('module info from server', remoteModuleInfo);
        let lib = new addon.ELFWrap(sysroot + remoteModuleInfo.path);
        const localModuleInfo = lib.info();
        log.i('module info from local addon', localModuleInfo);
        if (localModuleInfo.buildId != remoteModuleInfo.buildId) {
            throw new Error(`${remoteModuleInfo.name} buildId mismatch`);
        }
        remoteModuleInfo.vaddr = localModuleInfo.vaddr;
        remoteModuleInfo.baseAddr = parseInt(remoteModuleInfo.base, 16);
        return new Module(lib, remoteModuleInfo);
    }
    constructor(elf, info) {
        this.elf = elf;
        this.info = info;
    }
    release() {
        this.elf.release();
    }
    getFunctions(srclinePrefix, name) {
        const functions = this.elf.functions().filter(fn => fn.address && fn.size);
        const debugInfo = new addon.DebugInfoWrap(this.elf);
        const { baseAddr } = this.info;
        const ret = [];
        for (const fn of functions) {
            const info = debugInfo.srcline(fn.address);
            if (!info.src) {
                log.w('no debug info', fn);
                continue;
            }
            if (srclinePrefix && !info.src.startsWith(srclinePrefix)) continue;
            fn.file = info.src;
            fn.line = info.line;
            fn.addr = fn.address + baseAddr;
            ret.push(fn);
        }
        debugInfo.release();
        return ret;
    }
};

async function getFunctionsToTrace(rpc, libName, srclinePrefix) {
    let functionsToTrace = [];
    for (const lib of libName.split(',')) {
        const remoteModule = await rpc.getModuleByName(lib);
        const module = Module.create(remoteModule);
        const fns = module.getFunctions(srclinePrefix);
        module.release();
        if (!fns) {
            log.w('no functions to trace', lib);
            continue;
        }
        const remoteFunctions = await rpc.getFunctionsOfModule(lib);
        validateFunctions(fns, remoteFunctions);
        log.i(`collected ${fns.length} functions from ${lib}`);
        functionsToTrace = fns.concat(functionsToTrace);
    }
    if (functionsToTrace.length > Math.pow(2, 16)) {
        throw new Error(`too many functions for uint16_t, ${functionsToTrace.length}`);
    }
    return functionsToTrace;
}

class Process {
    static async getOrSpawn(deviceId, name, path = null) {
        const device = await frida.getDevice(deviceId);
        if (!device) throw new Error(`device '${deviceId}' not found.`);

        const processes = await device.enumerateProcesses();
        const process = processes.find(p => p.name == name);
        if (process) return new Process(device, process.pid, name);

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
    sysroot = argv[6] || sysroot;
    const sourceFilename = "./test.js";

    const targetProcess = await Process.getOrSpawn(deviceId, processName);
    const script = await targetProcess.attach(sourceFilename);

    const functionsToTrace = await getFunctionsToTrace(script, libName, srclinePrefix);
    await script.startTracing(functionsToTrace);
    await targetProcess.resume();

    log.i('Tracing started, press enter to stop.');
    await utils.StdIn.readline();

    await script.stopTracing();

    if (!events.length) return log.i('no trace data.');

    writeChromeTracingFile(`${libName}.json`, functionsToTrace);
    log.i('tracing done!');
};
main();
