function log() {
    const args = [];
    for (const arg of arguments) {
        args.push(JSON.stringify(arg));
    }
    console.log('test.js', ...args);
}
const pid = Process.id;

function getNativeFunction(module, name, argumentTypes = [], returnType = 'void') {
    const ptr = module.getExportByName(name);
    const fn = new NativeFunction(ptr, returnType, argumentTypes);
    return fn;
}

function getBuidId(soPath) {
    const getBuidId_addr = Module.getExportByName('libtrace.so', 'getBuidId');
    const getBuidId = new NativeFunction(getBuidId_addr, 'int', ['pointer', 'pointer', 'int']);
    const soPath2 = Memory.allocUtf8String(soPath);
    const length = 64;
    const bytes = Memory.alloc(length);
    const size = getBuidId(soPath2, bytes, length);
    if (0 < size && size < length) return bytes.readUtf8String();
    log(`failed to getBuildId(${soPath}): ${size}`);
    return "";
}

function loadLibTrace(path, callback) {
    const module = Module.load(path);
    const sendDataFn = module.getExportByName('_sendDataFn');
    const sendData = new NativeCallback(callback, 'void', ['pointer', 'int', 'int']);
    log(`sendData ${sendData}`);
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
    log(`event from tid ${tid}, ${length} bytes`);
    send({ type:'events', tid, pid }, data);
}

class Tracer {
    constructor() {
        const libtracePath = 'libtrace.so';
        this.attachCallbacks = loadLibTrace(libtracePath, onTraceEvent);
    }
    traceFunctions(functionAddresses) {
        for (const fn of functionAddresses) {
            const addr = new NativePointer(fn);
            try {
                Interceptor.attach(addr, this.attachCallbacks, addr);
            } catch (error) {
                log(`attach failed, ${error}. ${DebugSymbol.fromAddress(addr)}`);
            }
        }
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
    getBuidId(path) { return getBuidId(path); },
    getModuleByName(libName) {
        return Process.getModuleByName(libName);
    },
    getImportedFunctions(libName) { return Module.enumerateImportsSync(libName); },
    getFunctionsOfModule(libName) {
        const module = Process.getModuleByName(libName);
        let functions = module.enumerateSymbols();
        // log(module, functions);
        functions = functions.filter(s => {
            return s.type === 'function';
        });
        log(`${pid} ${libName} ${functions.length} functions`);
        return functions;
    },
    startTracing(functionAddresses) {
        log(`startTracing`)
        tracer.traceFunctions(functionAddresses);
    },
    stopTracing() {
        tracer.exit();
    }
};