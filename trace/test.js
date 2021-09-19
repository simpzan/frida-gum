const pid = Process.id;
function log(level, ...args) {
    let e = new Error();
    let frame = e.stack.split("\n")[2]; // change to 3 for grandparent func
    let lineNumber = frame.split(":").reverse()[1];
    let functionName = frame.split(" ")[5];
    const time = Date.now() / 1000;
    const srcline = `${time.toFixed(3)} ${level} ${pid} ${functionName}:${lineNumber}\t`;
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

function getNativeFunction(module, name, argumentTypes = [], returnType = 'void') {
    const ptr = module.getExportByName(name);
    const fn = new NativeFunction(ptr, returnType, argumentTypes);
    return fn;
}

function getBuildId(soPath) {
    const getBuidId_addr = Module.getExportByName('libtrace.so', 'getBuildId');
    const getBuildId = new NativeFunction(getBuidId_addr, 'int', ['pointer', 'pointer', 'int']);
    const soPath2 = Memory.allocUtf8String(soPath);
    const length = 64;
    const bytes = Memory.alloc(length);
    const size = getBuildId(soPath2, bytes, length);
    if (0 < size && size < length) return bytes.readUtf8String();
    log.e(`failed to getBuildId(${soPath}): ${size}`);
    return "";
}

function loadLibTrace(path, callback) {
    const module = Module.load(path);
    const sendDataFn = module.getExportByName('_sendDataFn');
    const sendData = new NativeCallback(callback, 'void', ['pointer', 'int', 'int']);
    log.d(`sendData ${sendData}`);
    sendDataFn.writePointer(sendData);
    const flushAll = getNativeFunction(module, 'flushAll');

    const attachCallbacks = {
        onEnter: module.getExportByName('onEnter'),
        onLeave: module.getExportByName('onLeave'),
        sendData,
        flushAll
    };
    return attachCallbacks;
}
const onTraceEvent = function(bytes, length, tid) {
    // const fn = bytes.readU64();
    // bytes = bytes.add(8);
    // const ts = bytes.readS64();
    const data = bytes.readByteArray(length);
    log.d(`event from tid ${tid}, ${length} bytes`);
    send({ type:'events', tid, pid }, data);
}

class Tracer {
    constructor() {
        const libtracePath = 'libtrace.so';
        this.attachCallbacks = loadLibTrace(libtracePath, onTraceEvent);
    }
    traceFunctions(functions) {
        functions.forEach((fn, index) => {
            const addr = new NativePointer(fn.addr);
            const functionId = new NativePointer(index);
            try {
                Interceptor.attach(addr, this.attachCallbacks, functionId);
            } catch (error) {
                log.e('attach failed', error.toString(), fn);
            }
        });
        log.i(`tracing ${functions.length} function addresses.`);
    }
    exit() {
        Interceptor.detachAll();
        this.attachCallbacks.flushAll();
        this.attachCallbacks = null;
        //todo: flush events from native side.
    }
}
const tracer = new Tracer();

rpc.exports = {
    getModuleByName(libName) {
        Module.load(libName);
        const module = Process.getModuleByName(libName);
        module.buildId = getBuildId(module.path);
        return module;
    },
    getImportedFunctions(libName) { return Module.enumerateImportsSync(libName); },
    getFunctionsOfModule(libName) {
        const module = Process.getModuleByName(libName);
        let functions = module.enumerateSymbols();
        // log.d(module, functions);
        functions = functions.filter(s => {
            return s.type === 'function';
        });
        log.i(`${libName} ${functions.length} functions`);
        return functions;
    },
    startTracing(functions) {
        log.i(`startTracing`)
        tracer.traceFunctions(functions);
    },
    stopTracing() {
        tracer.exit();
    }
};