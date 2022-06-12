const fs = require('fs');
const path = require('path');
const utils = require('./utils.js');
const fridaDir = process.platform == 'darwin' ? 'frida-macos-x86_64' : 'frida_thin-linux-x86_64';
const frida = require(`../../build/${fridaDir}/lib/node_modules/frida`);
const addon = require('./node-addon-api');

let threadNames = new Map();
let events = [];
class StackTrace {
    static _stackByTid = new Map();
    static getStackTrace(pid, tid) {
        let stack = this._stackByTid.get(tid);
        if (stack) return stack;
        stack = new StackTrace(pid, tid);
        this._stackByTid.set(tid, stack);
        return stack;
    }
    constructor(pid, tid) {
        this.pid = pid;
        this.tid = tid;
        this.frames = [];
    }
    addEvent(begin, addr, ts) {
        if (begin) return this.frames.push({ addr, ts });

        const e = this.frames.pop();
        if (!e) return log.e('end-only event', addr, ts);

        const duration = ts - e.ts;
        events.push({ pid:this.pid, tid:this.tid, ph:'X', addr:e.addr, ts:e.ts, dur:duration });
    }
    handleBeginOnlyEvents() {
        if (this.frames.length) log.e('begin-only events', this.frames);
        for (const e of this.frames) {
            events.push({ pid:this.pid, tid:this.tid, ph:'B', addr:e.addr, ts:e.ts });
        }
    }
    finish(threadName) {
        this.handleBeginOnlyEvents();

        const threadInfo = { threadName, pid:this.pid, tid:this.tid };
        threadNames.set(this.tid, threadInfo);
        log.i(`thread ${this.tid}, ${threadName}`);
    }
}
function onMessageFromDebuggee(msg, bytes) {
    const payload = msg.payload;
    if (!payload) return log.e(...arguments);
    if (payload.type === 'events') {
        const { pid, tid } = msg.payload;
        const stack = StackTrace.getStackTrace(pid, Math.abs(tid));
        if (tid < 0) {
            const threadName = bytes.toString('utf8').trim();
            stack.finish(threadName);
        } else {
            for (let i = 0; i < bytes.length; ) {
                const addr = Number(bytes.readUInt16LE(i)); i += 2;
                const ts = Number(bytes.readInt32LE(i)); i += 4;
                stack.addEvent(ts > 0, addr, Math.abs(ts));
            }
        }
    } else {
        log.e(`unkown msg`, ...arguments);
    }
}

function writeChromeTracingFile(filename, functionMap) {
    if (!events.length) return log.i('no trace data.');
    events.sort((e1, e2) => {
        const ret = e1.ts - e2.ts;
        if (ret) return ret;
        return e2.dur - e1.dur;
    });
    log.i(`writing chrome tracing file ${filename}`);
    const traceFile = new utils.ChromeTracingFile(filename);
    for (const trace of events) {
        const fn = functionMap[trace.pid][trace.addr];
        if (!fn) return log.e(`can't find function info for event`, trace);
        if (fn.method) trace.name = fn.method;
        else trace.name = fn.demangledName = fn.demangledName || addon.demangleCppName(fn.name);
        trace.cat = fn.cat || fn.class;
        traceFile.writeObject(trace);
    }
    for (const [tid, info] of threadNames) {
        const { pid, threadName } = info;
        const name = `${threadName}/${tid}`;
        const entry = {"ts":0, "ph":"M", "name":"thread_name", pid, tid, "args":{name}};
        traceFile.writeObject(entry);
    }
    traceFile.close();
    utils.runCmd(`zip -r ${filename}.zip ${filename}`);
}

function validateFunctions(functionsLocal, functionsRemote) {
    const localFunctions = new Map();
    for (const fn of functionsLocal) localFunctions.set(fn.name, fn);

    if (functionsRemote.length == 0) throw new Error('no remote functions to validate against');
    for (const fn of functionsRemote) {
        fn.addr = parseInt(fn.address, 16);
        const local = localFunctions.get(fn.name);
        if (!local || fn.addr != local.addr) {
            log.e(fn, local);
            throw new Error(`no matching function found, ${fn.name}`);
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
        let base = parseInt(remoteModuleInfo.base, 16);
        // vaddr is not included in baseAddr on android.
        if (remoteModuleInfo.isAndroid) base -= localModuleInfo.vaddr;
        remoteModuleInfo.baseAddr = base;
        return new Module(lib, remoteModuleInfo);
    }
    constructor(elf, info) {
        this.elf = elf;
        this.info = info;
    }
    release() {
        this.elf.release();
    }
    getFunctions() {
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
            fn.file = info.src;
            fn.line = info.line;
            fn.addr = fn.address + baseAddr;
            ret.push(fn);
        }
        debugInfo.release();
        return ret;
    }
};

async function getFunctionsToTrace(rpc, modules) {
    let functions = {};
    for (const rule of modules) {
        const lib = rule.name;
        const remoteModule = await rpc.getModuleByName(lib);
        const module = Module.create(remoteModule);
        let fns = module.getFunctions();
        module.release();
        if (!fns) {
            log.w('no functions to trace', lib);
            continue;
        }
        const remoteFunctions = await rpc.getFunctionsOfModule(lib);
        validateFunctions(fns, remoteFunctions);
        if (rule.src) fns = fns.filter(fn => fn.file.startsWith(rule.src));
        if (rule.function) fns = fns.filter(fn => fn.name.startsWith(rule.function));
        // fns.forEach(fn => log.i(fn.name));
        fns = fns.filter(fn => fn.size > 14);
        fns.forEach(fn => fn.cat = lib);
        log.i(`collected ${fns.length} functions from ${lib}`);
        for (const fn of fns) functions[fn.addr] = fn;
        if (rule.imported) {
            const imported = await rpc.getImportedFunctions(lib, rule.imported);
            log.i(`collected ${imported.length} imported functions`);
            for (const fn of imported) functions[fn.addr] = fn;
        }
    }
    const functionsToTrace = Object.values(functions);
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
    async startTracing(functionsToTrace, specs) {
        return await this.script.exports.startTracing(functionsToTrace, specs);
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
    async getOrSetBaseTimestamp(ts) {
        return await this.script.exports.getOrSetBaseTimestamp(ts);
    }
    async getImportedFunctions(libName, libs) {
        const imported = await this.script.exports.getImportedFunctions(libName);
        const functionsByModule = {};
        for (const fn of imported) {
            const lib = path.basename(fn.module, '.so');
            fn.addr = parseInt(fn.address, 16);
            fn.cat = lib;
            const fns = functionsByModule[lib] || [];
            fns.push(fn);
            functionsByModule[lib] = fns;
        }
        return libs.map(lib => {
            const fns = functionsByModule[lib] || [];
            log.i(`collected imported functions ${fns.length} from ${lib}`);
            // for (const fn of fns) log.d(fn.name);
            return fns;
        }).flat();
    }
};

async function main() {
    const argv = process.argv;
    log.i("argv", argv);

    const configFile = argv[2];
    const config = require(configFile);
    sysroot = config.sysroot;
    const deviceId = config.device;
    const sourceFilename = "./test.js";
    const processes = config.processes;

    let sharedBaseTimestamp = 0;
    const traceProcesses = {};
    const allFunctionsTracedByPid = {};
    for (const processName in processes) {
        const targetProcess = await Process.getOrSpawn(deviceId, processName);
        const script = await targetProcess.attach(sourceFilename);
        const pid = targetProcess.pid;
        traceProcesses[pid] = { pid, targetProcess, script, name: processName };

        sharedBaseTimestamp = await script.getOrSetBaseTimestamp(sharedBaseTimestamp);

        const specs = utils.partitionArray(processes[processName], s => s.type === 'java');
        const functionsToTrace = await getFunctionsToTrace(script, specs[false]);
        const javaMethods = await script.startTracing(functionsToTrace, specs[true]);
        const allFunctionsTraced = javaMethods.concat(functionsToTrace);
        allFunctionsTracedByPid[pid] = allFunctionsTraced;
    }
    for (const process of Object.values(traceProcesses)) {
        await process.targetProcess.resume();
        log.i(process.pid, process.name);
    }

    log.i('Tracing started, press enter to stop.');
    await utils.StdIn.readline();

    for (const process of Object.values(traceProcesses)) {
        await process.script.stopTracing();
    }

    writeChromeTracingFile(`${deviceId}.json`, allFunctionsTracedByPid);
    log.i('tracing done!');
};
main().catch(err => {
    log.e(err);
    process.exit(-1);
});
